package com.bionicfish.controller.transport

import com.bionicfish.controller.protocol.AsciiFrameDecoder
import com.bionicfish.controller.protocol.ControlCommand
import com.bionicfish.controller.protocol.DecodeResult
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.ProtocolFrame
import com.bionicfish.controller.protocol.ProtocolParseError
import com.bionicfish.controller.protocol.StepSpeed
import com.bionicfish.controller.protocol.Turn
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
        // 模拟器也通过生产分帧器校验 ASCII、字段重复和结束符；不再用独立宽松正则。
        val decoded = AsciiFrameDecoder().feed(bytes).singleOrNull()
        val frame = (decoded as? DecodeResult.Frame)?.value as? ProtocolFrame.Unknown
        if (frame?.type != "CMD") {
            val errorCode = if ((decoded as? DecodeResult.Malformed)?.reason == ProtocolParseError.DUPLICATE_FIELD) {
                "E_DUP_FIELD"
            } else {
                "E_FORMAT"
            }
            mutableIncomingBytes.emit("<ERR,seq=NA,code=$errorCode>\n".toByteArray(StandardCharsets.US_ASCII))
            return@withContext
        }
        val command = readCommand(frame.fields)
        if (command == null) {
            val sequence = frame.fields["seq"]?.toIntOrNull()
                ?.takeIf { it in ControlCommand.SEQUENCE_RANGE }?.toString() ?: "NA"
            mutableIncomingBytes.emit("<ERR,seq=$sequence,code=E_RANGE>\n".toByteArray(StandardCharsets.US_ASCII))
            return@withContext
        }
        val sequence = command.sequence
        // step_on 只表示 M1；M2 单独运行时这里仍必须为 0。
        val stepOn = if (command.move == Move.STOP) 0 else 1
        val servo = command.servoDegrees
        delay(responseDelayMillis)
        val response = buildString {
            append("<ACK,seq=$sequence,result=OK>\n")
            append(
                // 与最新固件相同：运行位与转速是否已知是两件事，不编造测量 RPM。
                "<STA,seq=$sequence,link=1,step_rpm=NA,step_est=NA," +
                    "step_actual=NA,step_on=$stepOn,servo=$servo,roll=1.2,pitch=-2.4,yaw=85.7," +
                    "m1=${command.move.wireValue},m2=${command.motor2.wireValue},err=0>\n",
            )
        }
        // 刻意拆成两段，持续覆盖真实网络拆包路径。
        val responseBytes = response.toByteArray(StandardCharsets.US_ASCII)
        val split = responseBytes.size / 2
        mutableIncomingBytes.emit(responseBytes.copyOfRange(0, split))
        mutableIncomingBytes.emit(responseBytes.copyOfRange(split, responseBytes.size))
    }

    private fun readCommand(fields: Map<String, String>): ControlCommand? {
        val sequence = fields["seq"]?.toIntOrNull() ?: return null
        val move = fields["move"]?.let { Move.fromWire(it) } ?: return null
        val turn = fields["turn"]?.let { Turn.fromWire(it) } ?: return null
        val speed = fields["step_speed"]?.toIntOrNull()?.let { StepSpeed.fromRpm(it) } ?: return null
        val servo = fields["servo"]?.toIntOrNull() ?: return null
        // 旧客户端五字段命令不携带 m2 时必须停止 M2，绝不是保持上一条目标。
        val motor2 = if ("m2" in fields) Move.fromWire(fields.getValue("m2")) ?: return null else Move.STOP
        return runCatching {
            // 复用真实发送模型的序号、±15° 舵角和 turn/servo 一致性约束。
            ControlCommand(sequence, move, turn, speed, servo, motor2)
        }.getOrNull()
    }
}
