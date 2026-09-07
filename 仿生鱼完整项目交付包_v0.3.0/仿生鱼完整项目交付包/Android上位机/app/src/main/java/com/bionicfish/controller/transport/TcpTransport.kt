package com.bionicfish.controller.transport

import java.net.InetSocketAddress
import java.net.Socket
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

class TcpTransport(
    private val scanner: NetworkScanner = UdpDiscoveryScanner(),
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : Transport {
    override val kind = TransportKind.TCP
    private val scope = CoroutineScope(SupervisorJob() + ioDispatcher)
    private val writeMutex = Mutex()
    private val mutableConnectionState = MutableStateFlow<TransportConnectionState>(TransportConnectionState.Disconnected)
    private val mutableIncomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 16)
    private var socket: Socket? = null
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
            val connected = withContext(ioDispatcher) {
                Socket().apply {
                    tcpNoDelay = true
                    keepAlive = true
                    connect(InetSocketAddress(endpoint.address, endpoint.port!!), timeoutMillis.toInt())
                }
            }
            socket = connected
            mutableConnectionState.value = TransportConnectionState.Connected(endpoint)
            readerJob = scope.launch { readLoop(connected, endpoint) }
        } catch (cancelled: CancellationException) {
            mutableConnectionState.value = TransportConnectionState.Disconnected
            throw cancelled
        } catch (error: Exception) {
            mutableConnectionState.value = TransportConnectionState.Failed(
                message = error.message ?: "TCP 连接失败",
                recoverable = true,
            )
            mutableFailures.tryEmit(TransportFailure("connect", error.message ?: "TCP 连接失败", true, error))
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
        withContext(ioDispatcher) { runCatching { socket?.close() } }
        socket = null
        if (job != null && job != kotlinx.coroutines.currentCoroutineContext()[Job]) job.cancelAndJoin()
        mutableConnectionState.value = TransportConnectionState.Disconnected
    }

    override suspend fun send(bytes: ByteArray) {
        require(bytes.isNotEmpty()) { "不能发送空数据" }
        writeMutex.withLock {
            val active = socket ?: throw IllegalStateException("TCP 尚未连接")
            try {
                withContext(ioDispatcher) {
                    active.getOutputStream().write(bytes)
                    active.getOutputStream().flush()
                }
            } catch (error: Exception) {
                mutableFailures.tryEmit(TransportFailure("send", error.message ?: "TCP 发送失败", true, error))
                throw error
            }
        }
    }

    private suspend fun readLoop(activeSocket: Socket, endpoint: TransportEndpoint) {
        val buffer = ByteArray(1024)
        try {
            val input = activeSocket.getInputStream()
            while (true) {
                val count = input.read(buffer)
                if (count < 0) throw IllegalStateException("TCP 对端已关闭")
                if (count > 0) mutableIncomingBytes.emit(buffer.copyOf(count))
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            if (!expectedDisconnect) {
                mutableConnectionState.value = TransportConnectionState.Failed(
                    error.message ?: "TCP 连接中断",
                    recoverable = true,
                )
                mutableFailures.emit(TransportFailure("receive", error.message ?: "TCP 连接中断", true, error))
            }
        } finally {
            runCatching { activeSocket.close() }
            if (socket === activeSocket) socket = null
            if (!expectedDisconnect && mutableConnectionState.value is TransportConnectionState.Connected) {
                mutableConnectionState.value = TransportConnectionState.Failed(
                    "TCP 连接中断：${endpoint.address}",
                    recoverable = true,
                )
            }
        }
    }
}
