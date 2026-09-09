package com.bionicfish.controller.telemetry

import com.bionicfish.controller.protocol.ProtocolFrame

enum class DataFreshness { NO_DATA, FRESH, STALE }

data class FaultDescription(
    val bit: Int,
    val mask: Long,
    val code: String,
    val description: String,
)

data class TelemetrySnapshot(
    val sequence: Int,
    val stm32LinkAlive: Boolean,
    val stepTargetRpm: Int?,
    val stepEstimatedRpm: Int?,
    val stepActualRpm: Double?,
    val servoDegrees: Int,
    val rollDegrees: Double?,
    val pitchDegrees: Double?,
    val yawDegrees: Double?,
    val faultBits: Long,
    val activeFaults: List<FaultDescription>,
    val receivedAtEpochMillis: Long,
    val stepCommandedOn: Boolean? = stepTargetRpm?.let { it > 0 },
)

data class TelemetryState(
    val snapshot: TelemetrySnapshot? = null,
    val freshness: DataFreshness = DataFreshness.NO_DATA,
    val ageMillis: Long? = null,
) {
    val isUsable: Boolean get() = snapshot != null && freshness == DataFreshness.FRESH
}

object Stm32Faults {
    private val known = listOf(
        FaultDescription(0, 1L, "LINK_TIMEOUT", "有效控制帧超时或接收溢出后断链"),
        FaultDescription(1, 2L, "ESP_RX_LOST", "ESP/USART2 接收数据丢失"),
        FaultDescription(2, 4L, "IMU_DATA_TIMEOUT", "JY61P 数据过期或不可用"),
        FaultDescription(3, 8L, "IMU_I2C_RECOVERY_FAILED", "JY61P I²C 总线恢复失败"),
        FaultDescription(4, 16L, "STEPPER_DISABLED", "步进驱动未启用"),
        FaultDescription(5, 32L, "UART_TX_DROPPED", "串口发送队列发生丢弃"),
        // bit6 = 64 为固件保留位，不映射成旧 N20 故障。
        FaultDescription(7, 128L, "OLED_I2C", "OLED 软件 I²C 运行异常"),
        FaultDescription(8, 256L, "CONFIGURATION", "关键配置与外设初始化不一致"),
    )

    fun decode(bits: Long): List<FaultDescription> {
        val decoded = known.filter { bits and it.mask != 0L }.toMutableList()
        val knownMask = known.fold(0L) { value, fault -> value or fault.mask }
        var unknown = bits and knownMask.inv()
        var bit = 0
        while (unknown != 0L && bit < Long.SIZE_BITS - 1) {
            val mask = 1L shl bit
            if (unknown and mask != 0L) {
                decoded += FaultDescription(bit, mask, "UNKNOWN_BIT_$bit", "未知或保留故障位")
                unknown = unknown and mask.inv()
            }
            bit++
        }
        return decoded
    }
}

fun ProtocolFrame.Status.toTelemetry(receivedAtEpochMillis: Long): TelemetrySnapshot = TelemetrySnapshot(
    sequence = sequence,
    stm32LinkAlive = linkAlive,
    stepTargetRpm = stepTargetRpm,
    stepEstimatedRpm = stepEstimatedRpm,
    stepActualRpm = stepActualRpm,
    servoDegrees = servoDegrees,
    rollDegrees = rollDegrees,
    pitchDegrees = pitchDegrees,
    yawDegrees = yawDegrees,
    faultBits = faultBits,
    activeFaults = Stm32Faults.decode(faultBits),
    receivedAtEpochMillis = receivedAtEpochMillis,
    stepCommandedOn = stepCommandedOn,
)
