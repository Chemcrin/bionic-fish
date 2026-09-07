package com.bionicfish.controller.transport

import java.nio.charset.StandardCharsets
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.withContext

/** 无硬件演示模式；生成的数据仍走真实 ASCII 分帧/解析器。 */
class MockTransport(
    private val responseDelayMillis: Long = 15L,
    private val dispatcher: CoroutineDispatcher = Dispatchers.Default,
) : Transport {
    override val kind = TransportKind.MOCK
    private val mutableConnectionState = MutableStateFlow<TransportConnectionState>(TransportConnectionState.Disconnected)
    private val mutableIncomingBytes = MutableSharedFlow<ByteArray>(extraBufferCapacity = 32)
    private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 4)
    private var connected = false

    override val connectionState: StateFlow<TransportConnectionState> = mutableConnectionState.asStateFlow()
    override val incomingBytes: SharedFlow<ByteArray> = mutableIncomingBytes.asSharedFlow()
    override val failures: SharedFlow<TransportFailure> = mutableFailures.asSharedFlow()

    override suspend fun scan(request: ScanRequest): List<TransportCandidate> {
        delay(responseDelayMillis)
        return listOf(
            TransportCandidate(
                name = "仿生鱼模拟器",
                endpoint = TransportEndpoint("mock://bionic-fish"),
                kind = kind,
                signalDbm = null,
                discoverySource = DiscoverySource.MOCK,
            ),
        )
    }

    override fun cancelScan() = Unit

    override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long) {
        mutableConnectionState.value = TransportConnectionState.Connecting(endpoint)
        delay(responseDelayMillis)
        connected = true
        mutableConnectionState.value = TransportConnectionState.Connected(endpoint)
    }

    override suspend fun disconnect(reason: String) {
        if (connected) mutableConnectionState.value = TransportConnectionState.Disconnecting(reason)
        connected = false
        mutableConnectionState.value = TransportConnectionState.Disconnected
    }

    override suspend fun send(bytes: ByteArray) = withContext(dispatcher) {
        check(connected) { "模拟设备尚未连接" }
        val text = String(bytes, StandardCharsets.US_ASCII)
        val match = CONTROL_REGEX.matchEntire(text)
        if (match == null) {
            mutableIncomingBytes.emit("<ERR,seq=NA,code=E_FORMAT>\n".toByteArray(StandardCharsets.US_ASCII))
            return@withContext
        }
        val sequence = match.groupValues[1].toInt()
        val move = match.groupValues[2]
        val stepRpm = if (move == "S") 0 else match.groupValues[4].toInt()
        val servo = match.groupValues[5].toInt()
        delay(responseDelayMillis)
        val response = buildString {
            append("<ACK,seq=$sequence,result=OK>\n")
            append(
                "<STA,seq=$sequence,link=1,step_rpm=$stepRpm,step_est=$stepRpm," +
                    "step_actual=NA,servo=$servo,roll=1.2,pitch=-2.4,yaw=85.7,err=0>\n",
            )
        }
        // 刻意拆成两段，持续覆盖真实网络拆包路径。
        val responseBytes = response.toByteArray(StandardCharsets.US_ASCII)
        val split = responseBytes.size / 2
        mutableIncomingBytes.emit(responseBytes.copyOfRange(0, split))
        mutableIncomingBytes.emit(responseBytes.copyOfRange(split, responseBytes.size))
    }

    private companion object {
        val CONTROL_REGEX = Regex(
            "<CMD,seq=(\\d+),move=([FRS]),turn=([LRC]),step_speed=(60|100),servo=(-?\\d+)>\\n",
        )
    }
}
