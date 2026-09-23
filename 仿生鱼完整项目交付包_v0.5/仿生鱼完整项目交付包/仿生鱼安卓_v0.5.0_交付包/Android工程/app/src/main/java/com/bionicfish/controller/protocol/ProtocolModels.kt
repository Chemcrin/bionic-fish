package com.bionicfish.controller.protocol

/** 每帧携带 M1、M2 和舵机的完整目标；序号由仓库层生成。 */
data class ControlCommand(
    val sequence: Int,
    val move: Move,
    val turn: Turn,
    val stepSpeed: StepSpeed,
    val servoDegrees: Int,
    val motor2: Move = Move.STOP,
) {
    init {
        require(sequence in SEQUENCE_RANGE) { "seq 必须在 0..65535" }
        require(servoDegrees in SERVO_RANGE) { "servo 必须在 -15..15" }
        require(turn.matches(servoDegrees)) { "turn 与 servo 符号不一致" }
    }

    companion object {
        val SEQUENCE_RANGE = 0..65535
        val SERVO_RANGE = -15..15
    }
}

/** 不含序号的用户控制意图。 */
data class ControlInput(
    val move: Move = Move.STOP,
    val turn: Turn = Turn.CENTER,
    val stepSpeed: StepSpeed = StepSpeed.SLOW,
    val servoDegrees: Int = 0,
    val motor2: Move = Move.STOP,
) {
    fun validated(): ControlInput {
        require(servoDegrees in ControlCommand.SERVO_RANGE) { "舵机角度超出 -15..15 度" }
        require(turn.matches(servoDegrees)) { "左转须为负角度、右转须为正角度、直行须为 0 度" }
        return this
    }

    companion object {
        // 安全停止明确停止两路，不能依赖枚举顺序或当前控制草稿。
        val SAFE_STOP = ControlInput(move = Move.STOP, motor2 = Move.STOP)
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

/** 遗留线协议必填值；双直流电机固件中不控制占空比，也不表示实测转速。 */
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
        /** NA 是合法状态：固件未提供目标/估算转速，不能用 0 代替。 */
        val stepTargetRpm: Int?,
        val stepEstimatedRpm: Int?,
        /** 当前板卡无编码器，固件固定发送 NA；保留数值分支供未来兼容。 */
        val stepActualRpm: Double?,
        val servoDegrees: Int,
        val rollDegrees: Double?,
        val pitchDegrees: Double?,
        val yawDegrees: Double?,
        val faultBits: Long,
        /** step_on 只表示 M1；旧固件按已知目标转速推断，无依据时保持 null。 */
        val stepRunning: Boolean? = stepTargetRpm?.let { it > 0 },
        /** 固件当前下达的方向，不是编码器反馈；缺失意味着旧固件未提供。 */
        val motor1Direction: Move? = null,
        val motor2Direction: Move? = null,
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
