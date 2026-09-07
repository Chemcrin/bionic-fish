package com.bionicfish.controller.protocol

/** 固件当前接受的五字段控制命令。序号由仓库层生成，UI 不应自行维护。 */
data class ControlCommand(
    val sequence: Int,
    val move: Move,
    val turn: Turn,
    val stepSpeed: StepSpeed,
    val servoDegrees: Int,
) {
    init {
        require(sequence in SEQUENCE_RANGE) { "seq 必须在 0..65535" }
        require(servoDegrees in SERVO_RANGE) { "servo 必须在 -30..30" }
        require(turn.matches(servoDegrees)) { "turn 与 servo 符号不一致" }
    }

    companion object {
        val SEQUENCE_RANGE = 0..65535
        val SERVO_RANGE = -30..30
    }
}

/** 不含序号的用户控制意图。 */
data class ControlInput(
    val move: Move = Move.STOP,
    val turn: Turn = Turn.CENTER,
    val stepSpeed: StepSpeed = StepSpeed.SLOW,
    val servoDegrees: Int = 0,
) {
    fun validated(): ControlInput {
        require(servoDegrees in ControlCommand.SERVO_RANGE) { "舵机角度超出 -30..30 度" }
        require(turn.matches(servoDegrees)) { "左转须为负角度、右转须为正角度、直行须为 0 度" }
        return this
    }

    companion object {
        val SAFE_STOP = ControlInput()
    }
}

enum class Move(val wireValue: String) {
    FORWARD("F"),
    REVERSE("R"),
    STOP("S");

    companion object {
        fun fromWire(value: String): Move? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class Turn(val wireValue: String) {
    LEFT("L"),
    RIGHT("R"),
    CENTER("C");

    fun matches(servoDegrees: Int): Boolean = when (this) {
        LEFT -> servoDegrees < 0
        RIGHT -> servoDegrees > 0
        CENTER -> servoDegrees == 0
    }

    companion object {
        fun fromWire(value: String): Turn? = entries.firstOrNull { it.wireValue == value }
    }
}

enum class StepSpeed(val rpm: Int) {
    SLOW(60),
    FAST(100);

    companion object {
        fun fromRpm(value: Int): StepSpeed? = entries.firstOrNull { it.rpm == value }
    }
}

sealed interface ProtocolFrame {
    data class Status(
        val sequence: Int,
        val linkAlive: Boolean,
        val stepTargetRpm: Int,
        val stepEstimatedRpm: Int,
        /** 当前板卡无编码器，固件固定发送 NA；保留数值分支供未来兼容。 */
        val stepActualRpm: Double?,
        val servoDegrees: Int,
        val rollDegrees: Double?,
        val pitchDegrees: Double?,
        val yawDegrees: Double?,
        val faultBits: Long,
    ) : ProtocolFrame

    data class Acknowledgement(
        val sequence: Int,
        val duplicate: Boolean,
    ) : ProtocolFrame

    data class Error(
        val sequence: Int?,
        val code: String,
    ) : ProtocolFrame

    /** 对未知类型只记录，不据此宣称设备支持某项能力。 */
    data class Unknown(
        val type: String,
        val fields: Map<String, String>,
    ) : ProtocolFrame
}

sealed interface DecodeResult {
    data class Frame(val value: ProtocolFrame) : DecodeResult
    data class Malformed(val reason: ProtocolParseError, val rawPayload: String? = null) : DecodeResult
}

enum class ProtocolParseError {
    INVALID_ASCII,
    INVALID_EOL,
    FRAME_TIMEOUT,
    FRAME_TOO_LONG,
    EMPTY_FRAME,
    INVALID_FIELD,
    DUPLICATE_FIELD,
    MISSING_FIELD,
    INVALID_VALUE,
}
