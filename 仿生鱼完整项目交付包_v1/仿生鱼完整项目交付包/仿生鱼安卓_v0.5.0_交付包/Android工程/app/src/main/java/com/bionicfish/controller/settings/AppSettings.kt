package com.bionicfish.controller.settings

import com.bionicfish.controller.transport.ScanRequest
import com.bionicfish.controller.transport.TransportEndpoint
import com.bionicfish.controller.transport.TransportKind

data class AppSettings(
    val transportKind: TransportKind = TransportKind.MOCK,
    val wifiHost: String = "",
    /** 0 表示待确认，不能用于真实连接。 */
    val wifiPort: Int = 0,
    val apSsid: String = ApDirectDefaults.SSID,
    val apHost: String = ApDirectDefaults.HOST,
    val apPort: Int = ApDirectDefaults.PORT,
    val discoveryAddress: String = "",
    val discoveryPort: Int = 0,
    val discoveryPayload: String = "",
    val bluetoothClassicUuid: String = "",
    val bluetoothLeServiceUuid: String = "",
    val bluetoothLeCharacteristicUuid: String = "",
    val protocolVersionExpected: Int = 1,
    val connectTimeoutMillis: Long = 5_000L,
    val scanTimeoutMillis: Long = 3_000L,
    val handshakeTimeoutMillis: Long = 2_000L,
    /** 显式 CMD 等待同序号 ACK/DUP 的最长时间；须早于 STM32/应用失联判定。 */
    val commandAckTimeoutMillis: Long = 800L,
    /** STM32 V1 固件 1000ms 失联保护至少保留 250ms 调度/网络余量。 */
    val controlSendPeriodMillis: Long = 200L,
    val heartbeatPeriodMillis: Long = 500L,
    val linkTimeoutMillis: Long = 1_000L,
    val telemetryStaleMillis: Long = 1_200L,
    val reconnectEnabled: Boolean = true,
    val reconnectDelayMillis: Long = 1_000L,
    val reconnectMaxAttempts: Int = 3,
    val safeStopOnDisconnect: Boolean = true,
    val safeStopOnBackground: Boolean = true,
    val safeStopOnControlExit: Boolean = true,
    val centerServoOnSafeStop: Boolean = true,
    val reduceMotion: Boolean = false,
    val pressAndHoldToMove: Boolean = false,
    /** 双电机固件支持 M1/M2 换向；新安装默认开启，旧设置中的明确关闭仍保留。 */
    val allowReverseCommand: Boolean = true,
    val lastDeviceName: String = "",
    val lastDeviceAddress: String = "",
    val lastDevicePort: Int = 0,
    val lastDeviceApDirect: Boolean = false,
) {
    fun validated(): AppSettings = apply {
        require(protocolVersionExpected >= 1)
        require(connectTimeoutMillis in 100L..60_000L)
        require(scanTimeoutMillis in 100L..60_000L)
        require(handshakeTimeoutMillis in 100L..30_000L)
        require(commandAckTimeoutMillis in 100L..MAX_COMMAND_ACK_TIMEOUT_MILLIS)
        require(controlSendPeriodMillis in 50L..MAX_FIRMWARE_SAFE_SEND_PERIOD_MILLIS)
        require(heartbeatPeriodMillis in 50L..MAX_FIRMWARE_SAFE_SEND_PERIOD_MILLIS)
        require(linkTimeoutMillis > maxOf(controlSendPeriodMillis, heartbeatPeriodMillis)) {
            "失联超时必须大于控制和心跳周期"
        }
        require(commandAckTimeoutMillis < linkTimeoutMillis) {
            "命令 ACK 超时必须早于失联超时"
        }
        require(telemetryStaleMillis >= 200L)
        require(reconnectDelayMillis in 100L..60_000L)
        require(reconnectMaxAttempts in 0..20)
        require(wifiPort == 0 || wifiPort in 1..65535)
        require(apSsid.isNotBlank() && apSsid.toByteArray(Charsets.UTF_8).size <= 32) {
            "AP 热点名称必须为 1..32 个 UTF-8 字节"
        }
        val octets = apHost.split('.').map { part ->
            part.takeIf { it.length in 1..3 && it.all { ch -> ch in '0'..'9' } }?.toIntOrNull()
        }
        require(octets.size == 4 && octets.all { it != null && it in 0..255 } &&
            octets.first() in 1..223 && octets.first() != 127) { "AP 地址必须为有效的单播 IPv4 地址" }
        require(apPort in 1..65535) { "AP 端口必须在 1..65535" }
        require(discoveryPort == 0 || discoveryPort in 1..65535)
        require(lastDevicePort == 0 || lastDevicePort in 1..65535)
    }

    fun selectedEndpoint(): TransportEndpoint? = when (transportKind) {
        TransportKind.MOCK -> TransportEndpoint("mock://bionic-fish")
        TransportKind.TCP, TransportKind.UDP -> wifiHost
            .takeIf { it.isNotBlank() && wifiPort in 1..65535 }
            ?.let { TransportEndpoint(it, wifiPort) }
        TransportKind.BLUETOOTH_CLASSIC, TransportKind.BLUETOOTH_LE -> null
    }

    fun scanRequest(): ScanRequest = ScanRequest(
        timeoutMillis = scanTimeoutMillis,
        discoveryAddress = discoveryAddress,
        discoveryPort = discoveryPort,
        discoveryPayload = discoveryPayload,
        configuredEndpoint = selectedEndpoint(),
        configuredName = if (lastDeviceApDirect) "已配置 Wi-Fi 设备"
            else lastDeviceName.ifBlank { "已配置 Wi-Fi 设备" },
    )

    fun apEndpoint(): TransportEndpoint = TransportEndpoint(apHost, apPort, wifiOnly = true)

    companion object {
        /** 对应 STM32 V1 CFG_LINK_TIMEOUT_MS=1000，预留 250ms 安全余量。 */
        const val MAX_FIRMWARE_SAFE_SEND_PERIOD_MILLIS = 750L
        /** STM32 V1 1000ms watchdog 前至少留出 100ms 进入失联保护。 */
        const val MAX_COMMAND_ACK_TIMEOUT_MILLIS = 900L
    }
}
