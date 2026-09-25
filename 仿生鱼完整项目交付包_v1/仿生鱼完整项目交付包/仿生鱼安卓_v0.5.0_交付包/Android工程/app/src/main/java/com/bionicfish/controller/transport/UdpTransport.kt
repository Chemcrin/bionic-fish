package com.bionicfish.controller.transport

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

/** UDP 是无连接协议，此处 Connected 仅表示本地套接字已绑定到指定远端。 */
class UdpTransport(
    private val scanner: NetworkScanner = UdpDiscoveryScanner(),
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Transport {
    override val kind = TransportKind.UDP
    private val scope = CoroutineScope(SupervisorJob() + ioDispatcher)
    private val writeMutex = Mutex()
    private val mutableConnectionState = MutableStateFlow<TransportConnectionState>(TransportConnectionState.Disconnected)
    private val mutableIncomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 16)
    private var socket: DatagramSocket? = null
    private var readerJob: Job? = null
    private var expectedDisconnect = false

    override val connectionState: StateFlow<TransportConnectionState> = mutableConnectionState.asStateFlow()
    override val incomingBytes: SharedFlow<ByteArray> = mutableIncomingBytes.asSharedFlow()
    override val failures: SharedFlow<TransportFailure> = mutableFailures.asSharedFlow()

    override suspend fun scan(request: ScanRequest): List<TransportCandidate> = scanner.scan(kind, request)

    override fun cancelScan() {
        (scanner as? UdpDiscoveryScanner)?.cancel()
    }

    override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long) {
        endpoint.requireNetworkEndpoint()
        require(timeoutMillis in 100L..60_000L) { "连接超时须为 100..60000 ms" }
        disconnect("切换连接")
        mutableConnectionState.value = TransportConnectionState.Connecting(endpoint)
        expectedDisconnect = false
        try {
            val datagramSocket = withContext(ioDispatcher) {
                DatagramSocket().apply {
                    connect(InetAddress.getByName(endpoint.address), endpoint.port!!)
                }
            }
            socket = datagramSocket
            mutableConnectionState.value = TransportConnectionState.Connected(endpoint)
            readerJob = scope.launch { readLoop(datagramSocket, endpoint) }
        } catch (cancelled: CancellationException) {
            mutableConnectionState.value = TransportConnectionState.Disconnected
            throw cancelled
        } catch (error: Exception) {
            mutableConnectionState.value = TransportConnectionState.Failed(
                error.message ?: "UDP 初始化失败",
                recoverable = true,
            )
            mutableFailures.tryEmit(TransportFailure("connect", error.message ?: "UDP 初始化失败", true, error))
            throw error
        }
    }

    override suspend fun disconnect(reason: String) {
        expectedDisconnect = true
        if (socket == null && readerJob == null) {
            mutableConnectionState.value = TransportConnectionState.Disconnected
            return
        }
        mutableConnectionState.value = TransportConnectionState.Disconnecting(reason)
        val job = readerJob
        readerJob = null
        withContext(ioDispatcher) { socket?.close() }
        socket = null
        if (job != null && job != kotlinx.coroutines.currentCoroutineContext()[Job]) job.cancelAndJoin()
        mutableConnectionState.value = TransportConnectionState.Disconnected
    }

    override suspend fun send(bytes: ByteArray) {
        require(bytes.isNotEmpty()) { "不能发送空数据" }
        writeMutex.withLock {
            val active = socket ?: throw IllegalStateException("UDP 尚未初始化")
            try {
                withContext(ioDispatcher) { active.send(DatagramPacket(bytes, bytes.size)) }
            } catch (error: Exception) {
                mutableFailures.tryEmit(TransportFailure("send", error.message ?: "UDP 发送失败", true, error))
                throw error
            }
        }
    }

    private suspend fun readLoop(activeSocket: DatagramSocket, endpoint: TransportEndpoint) {
        val buffer = ByteArray(2048)
        try {
            while (true) {
                val packet = DatagramPacket(buffer, buffer.size)
                activeSocket.receive(packet)
                if (packet.length > 0) {
                    mutableIncomingBytes.emit(packet.data.copyOfRange(packet.offset, packet.offset + packet.length))
                }
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            if (!expectedDisconnect) {
                mutableConnectionState.value = TransportConnectionState.Failed(
                    error.message ?: "UDP 接收中断",
                    recoverable = true,
                )
                mutableFailures.emit(TransportFailure("receive", error.message ?: "UDP 接收中断", true, error))
            }
        } finally {
            activeSocket.close()
            if (socket === activeSocket) socket = null
            if (!expectedDisconnect && mutableConnectionState.value is TransportConnectionState.Connected) {
                mutableConnectionState.value = TransportConnectionState.Failed(
                    "UDP 接收中断：${endpoint.address}",
                    recoverable = true,
                )
            }
        }
    }
}
