package com.bionicfish.controller.transport

import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.cancelAndJoin
import kotlinx.coroutines.currentCoroutineContext
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
    private val connector: TcpSocketConnector = DefaultTcpSocketConnector(ioDispatcher),
) : Transport {
    override val kind = TransportKind.TCP
    private val scope = CoroutineScope(SupervisorJob() + ioDispatcher)
    private val writeMutex = Mutex()
    private val mutableConnectionState = MutableStateFlow<TransportConnectionState>(TransportConnectionState.Disconnected)
    private val mutableIncomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 16)
    private val lifecycleLock = Any()
    private var connection: TcpSocketConnection? = null
    private var readerJob: Job? = null
    private var connectingJob: Job? = null
    private var generation = 0L

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
        val caller = currentCoroutineContext()[Job]
        val attempt = synchronized(lifecycleLock) {
            generation += 1L
            connectingJob = caller
            mutableConnectionState.value = TransportConnectionState.Connecting(endpoint)
            generation
        }
        try {
            val connected = connector.connect(endpoint, timeoutMillis)
            synchronized(lifecycleLock) {
                if (generation != attempt) {
                    connected.close()
                    throw CancellationException("TCP 连接已取消或已切换")
                }
                connection = connected
                connectingJob = null
                mutableConnectionState.value = TransportConnectionState.Connected(endpoint)
                readerJob = scope.launch { readLoop(connected, endpoint) }
            }
        } catch (cancelled: CancellationException) {
            synchronized(lifecycleLock) {
                if (generation == attempt) {
                    connectingJob = null
                    mutableConnectionState.value = TransportConnectionState.Disconnected
                }
            }
            throw cancelled
        } catch (error: Exception) {
            synchronized(lifecycleLock) {
                if (generation == attempt) {
                    connectingJob = null
                    mutableConnectionState.value = TransportConnectionState.Failed(
                        message = error.message ?: "TCP 连接失败",
                        recoverable = true,
                    )
                    mutableFailures.tryEmit(TransportFailure("connect", error.message ?: "TCP 连接失败", true, error))
                }
            }
            throw error
        }
    }

    override suspend fun disconnect(reason: String) {
        val caller = currentCoroutineContext()[Job]
        val disconnecting: DisconnectSnapshot
        synchronized(lifecycleLock) {
            generation += 1L
            disconnecting = DisconnectSnapshot(generation, connection, readerJob, connectingJob)
            connection = null
            readerJob = null
            connectingJob = null
            mutableConnectionState.value = TransportConnectionState.Disconnecting(reason)
        }
        // 回收不能被调用方取消打断，否则 Socket/网络回调可能残留。
        withContext(NonCancellable + ioDispatcher) {
            disconnecting.connection?.close()
            disconnecting.connectingJob?.takeIf { it != caller }?.cancelAndJoin()
            disconnecting.readerJob?.takeIf { it != caller }?.cancelAndJoin()
        }
        synchronized(lifecycleLock) {
            if (generation == disconnecting.generation) mutableConnectionState.value = TransportConnectionState.Disconnected
        }
    }

    override suspend fun send(bytes: ByteArray) {
        require(bytes.isNotEmpty()) { "不能发送空数据" }
        writeMutex.withLock {
            val active = synchronized(lifecycleLock) { connection } ?: throw IllegalStateException("TCP 尚未连接")
            try {
                withContext(ioDispatcher) {
                    active.socket.getOutputStream().write(bytes)
                    active.socket.getOutputStream().flush()
                }
            } catch (error: Exception) {
                mutableFailures.tryEmit(TransportFailure("send", active.failureMessage ?: error.message ?: "TCP 发送失败", true, error))
                throw error
            }
        }
    }

    private suspend fun readLoop(activeConnection: TcpSocketConnection, endpoint: TransportEndpoint) {
        val buffer = ByteArray(1024)
        try {
            val input = activeConnection.socket.getInputStream()
            while (true) {
                val count = input.read(buffer)
                if (count < 0) throw IllegalStateException("TCP 对端已关闭")
                if (count > 0) mutableIncomingBytes.emit(buffer.copyOf(count))
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            synchronized(lifecycleLock) {
                // 旧 Socket 的 onLost/读失败只清理旧会话，不能覆盖新连接的状态。
                if (connection === activeConnection) {
                    val message = activeConnection.failureMessage ?: error.message ?: "TCP 连接中断"
                    mutableConnectionState.value = TransportConnectionState.Failed(message, recoverable = true)
                    mutableFailures.tryEmit(TransportFailure("receive", message, true, error))
                }
            }
        } finally {
            activeConnection.close()
            synchronized(lifecycleLock) {
                if (connection === activeConnection) {
                    connection = null
                    if (mutableConnectionState.value is TransportConnectionState.Connected) {
                        mutableConnectionState.value = TransportConnectionState.Failed(
                            activeConnection.failureMessage ?: "TCP 连接中断：${endpoint.address}",
                            recoverable = true,
                        )
                    }
                }
            }
        }
    }

    private data class DisconnectSnapshot(
        val generation: Long,
        val connection: TcpSocketConnection?,
        val readerJob: Job?,
        val connectingJob: Job?,
    )
}
