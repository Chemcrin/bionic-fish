package com.bionicfish.controller.protocol

import java.nio.charset.StandardCharsets

object AsciiProtocol {
    const val MAX_PAYLOAD_BYTES = 191
    /** 仅接收旧固件状态时兼容 ±30°；发送命令始终使用 ControlCommand.SERVO_RANGE。 */
    val LEGACY_STATUS_SERVO_RANGE = -30..30

    fun encodeControl(command: ControlCommand): ByteArray {
        val payload = buildString(80) {
            append("<CMD,seq=")
            append(command.sequence)
            append(",move=")
            append(command.move.wireValue)
            append(",turn=")
            append(command.turn.wireValue)
            append(",step_speed=")
            append(command.stepSpeed.rpm)
            append(",servo=")
            append(command.servoDegrees)
            append(",m2=")
            append(command.motor2.wireValue)
            append(">\n")
        }
        return payload.toByteArray(StandardCharsets.US_ASCII)
    }

    fun decodePayload(payload: String): DecodeResult {
        if (payload.isEmpty()) return DecodeResult.Malformed(ProtocolParseError.EMPTY_FRAME, payload)
        val tokens = payload.split(',')
        val type = tokens.first()
        if (type.isBlank() || tokens.drop(1).any { it.count { char -> char == '=' } != 1 }) {
            return DecodeResult.Malformed(ProtocolParseError.INVALID_FIELD, payload)
        }

        val fields = linkedMapOf<String, String>()
        for (token in tokens.drop(1)) {
            val separator = token.indexOf('=')
            val key = token.substring(0, separator)
            val value = token.substring(separator + 1)
            if (key.isEmpty() || value.isEmpty()) {
                return DecodeResult.Malformed(ProtocolParseError.INVALID_FIELD, payload)
            }
            if (fields.put(key, value) != null) {
                return DecodeResult.Malformed(ProtocolParseError.DUPLICATE_FIELD, payload)
            }
        }

        return when (type) {
            "STA" -> parseStatus(fields, payload)
            "ACK" -> parseAcknowledgement(fields, payload)
            "ERR" -> parseError(fields, payload)
            else -> DecodeResult.Frame(ProtocolFrame.Unknown(type, fields))
        }
    }

    private fun parseStatus(fields: Map<String, String>, raw: String): DecodeResult {
        val required = setOf(
            "seq", "link", "step_rpm", "step_est", "step_actual", "servo",
            "roll", "pitch", "yaw", "err",
        )
        if (!fields.keys.containsAll(required)) {
            return DecodeResult.Malformed(ProtocolParseError.MISSING_FIELD, raw)
        }

        val sequence = fields["seq"]?.toIntOrNull()?.takeIf { it in ControlCommand.SEQUENCE_RANGE }
        val link = when (fields["link"]) {
            "0" -> false
            "1" -> true
            else -> null
        }
        val stepTarget = fields["step_rpm"]?.toIntOrNull()?.takeIf { it in setOf(0, 60, 100) }
        val stepEstimate = fields["step_est"]?.toIntOrNull()?.takeIf { it in setOf(0, 60, 100) }
        val stepActual = nullableNumber(fields["step_actual"])
        val servo = fields["servo"]?.toIntOrNull()?.takeIf { it in LEGACY_STATUS_SERVO_RANGE }
        val roll = nullableNumber(fields["roll"])
        val pitch = nullableNumber(fields["pitch"])
        val yaw = nullableNumber(fields["yaw"])
        val faults = fields["err"]?.toLongOrNull()?.takeIf { it >= 0L }
        val motor1 = fields["m1"]?.let { Move.fromWire(it) }
        val motor2 = fields["m2"]?.let { Move.fromWire(it) }

        if (
            sequence == null || link == null ||
            (stepTarget == null && fields["step_rpm"] != "NA") ||
            (stepEstimate == null && fields["step_est"] != "NA") ||
            stepActual === InvalidNumber || servo == null || roll === InvalidNumber ||
            pitch === InvalidNumber || yaw === InvalidNumber || faults == null ||
            ("m1" in fields && motor1 == null) || ("m2" in fields && motor2 == null)
        ) {
            return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
        }
        // NA 表示固件不声明转速，不表示停止；新固件以 step_on 明确报告运行状态。
        // 旧固件缺省 step_on 时只按已知目标转速推断，目标转速未知就保留未知。
        val stepRunning = when (fields["step_on"]) {
            "0" -> false
            "1" -> true
            null -> stepTarget?.let { it > 0 }
            else -> return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
        }
        return DecodeResult.Frame(
            ProtocolFrame.Status(
                sequence = sequence,
                linkAlive = link,
                stepTargetRpm = stepTarget,
                stepEstimatedRpm = stepEstimate,
                stepActualRpm = stepActual as Double?,
                servoDegrees = servo,
                rollDegrees = roll as Double?,
                pitchDegrees = pitch as Double?,
                yawDegrees = yaw as Double?,
                faultBits = faults,
                stepRunning = stepRunning,
                motor1Direction = motor1,
                motor2Direction = motor2,
            ),
        )
    }

    private fun parseAcknowledgement(fields: Map<String, String>, raw: String): DecodeResult {
        val sequence = fields["seq"]?.toIntOrNull()?.takeIf { it in ControlCommand.SEQUENCE_RANGE }
        val duplicate = when (fields["result"]) {
            "OK" -> false
            "DUP" -> true
            else -> null
        }
        return if (sequence == null || duplicate == null) {
            DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
        } else {
            DecodeResult.Frame(ProtocolFrame.Acknowledgement(sequence, duplicate))
        }
    }

    private fun parseError(fields: Map<String, String>, raw: String): DecodeResult {
        val sequence = when (val value = fields["seq"]) {
            "NA" -> null
            null -> return DecodeResult.Malformed(ProtocolParseError.MISSING_FIELD, raw)
            else -> value.toIntOrNull()?.takeIf { it in ControlCommand.SEQUENCE_RANGE }
                ?: return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
        }
        val code = fields["code"]?.takeIf { it.matches(Regex("E_[A-Z0-9_]+")) }
            ?: return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
        return DecodeResult.Frame(ProtocolFrame.Error(sequence, code))
    }

    private object InvalidNumber

    /** 返回 Double?（NA -> null）或 InvalidNumber。 */
    private fun nullableNumber(value: String?): Any? = when {
        value == "NA" -> null
        value == null -> InvalidNumber
        else -> value.toDoubleOrNull()?.takeIf { it.isFinite() } ?: InvalidNumber
    }
}

/**
 * 增量分帧器。支持拆包、粘包、CRLF、噪声重同步和超长帧丢弃；不在网络线程外保存 ByteArray 引用。
 */
class AsciiFrameDecoder(
    private val maxPayloadBytes: Int = AsciiProtocol.MAX_PAYLOAD_BYTES,
) {
    private enum class State { WAIT_START, COLLECT, EXPECT_EOL, EXPECT_LF, DISCARD }

    private var state = State.WAIT_START
    private val payload = StringBuilder(maxPayloadBytes)
    private var frameStartedAtMillis: Long? = null

    @Synchronized
    fun feed(bytes: ByteArray, nowMillis: Long = System.currentTimeMillis()): List<DecodeResult> {
        val results = mutableListOf<DecodeResult>()
        for (unsigned in bytes) {
            val value = unsigned.toInt() and 0xFF
            when (state) {
                State.WAIT_START -> if (value == '<'.code) beginFrame(nowMillis)
                State.COLLECT -> when {
                    value == '<'.code -> beginFrame(nowMillis)
                    value == '>'.code && payload.isNotEmpty() -> state = State.EXPECT_EOL
                    value == '>'.code -> {
                        results += DecodeResult.Malformed(ProtocolParseError.EMPTY_FRAME)
                        reset()
                    }
                    value == '\r'.code || value == '\n'.code || value !in 0x20..0x7E -> {
                        results += DecodeResult.Malformed(ProtocolParseError.INVALID_ASCII)
                        reset()
                    }
                    payload.length >= maxPayloadBytes -> {
                        results += DecodeResult.Malformed(ProtocolParseError.FRAME_TOO_LONG)
                        state = State.DISCARD
                    }
                    else -> payload.append(value.toChar())
                }
                State.EXPECT_EOL -> when (value) {
                    '\n'.code -> finish(results)
                    '\r'.code -> state = State.EXPECT_LF
                    '<'.code -> {
                        results += DecodeResult.Malformed(ProtocolParseError.INVALID_EOL)
                        beginFrame(nowMillis)
                    }
                    else -> {
                        results += DecodeResult.Malformed(ProtocolParseError.INVALID_EOL)
                        reset()
                    }
                }
                State.EXPECT_LF -> if (value == '\n'.code) {
                    finish(results)
                } else {
                    results += DecodeResult.Malformed(ProtocolParseError.INVALID_EOL)
                    if (value == '<'.code) beginFrame(nowMillis) else reset()
                }
                State.DISCARD -> when (value) {
                    '<'.code -> beginFrame(nowMillis)
                    '\r'.code, '\n'.code -> reset()
                }
            }
        }
        return results
    }

    @Synchronized
    fun reset() {
        payload.clear()
        state = State.WAIT_START
        frameStartedAtMillis = null
    }

    /** 网络静默时由会话层推进，确保拆到一半的帧不会永久占用解析状态。 */
    @Synchronized
    fun onTime(nowMillis: Long, frameTimeoutMillis: Long): DecodeResult.Malformed? {
        require(frameTimeoutMillis > 0L)
        val started = frameStartedAtMillis ?: return null
        return if (nowMillis - started >= frameTimeoutMillis && state != State.WAIT_START) {
            reset()
            DecodeResult.Malformed(ProtocolParseError.FRAME_TIMEOUT)
        } else {
            null
        }
    }

    private fun beginFrame(nowMillis: Long) {
        payload.clear()
        state = State.COLLECT
        frameStartedAtMillis = nowMillis
    }

    private fun finish(results: MutableList<DecodeResult>) {
        results += AsciiProtocol.decodePayload(payload.toString())
        reset()
    }
}
