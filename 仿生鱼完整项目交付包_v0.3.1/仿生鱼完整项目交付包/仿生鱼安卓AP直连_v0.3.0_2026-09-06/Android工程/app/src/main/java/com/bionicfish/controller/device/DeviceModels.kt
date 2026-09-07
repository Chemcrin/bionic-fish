package com.bionicfish.controller.device

import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.telemetry.TelemetryState
import com.bionicfish.controller.transport.BluetoothCapabilities
import com.bionicfish.controller.transport.DiscoverySource
import com.bionicfish.controller.transport.TransportCandidate
import com.bionicfish.controller.transport.TransportEndpoint
import com.bionicfish.controller.transport.TransportKind

data class DiscoveredDevice(
    val id: String,
    val name: String,
    val endpoint: TransportEndpoint,
    val transportKind: TransportKind,
    val signalDbm: Int?,
    val discoveredAtEpochMillis: Long,
    val discoverySource: DiscoverySource,
    val isSaved: Boolean,
) {
    companion object {
        /** 固定预置并非扫描发现：不发送 UDP，不杜撰 RSSI 或发现时间。 */
        fun apDirect(settings: AppSettings): DiscoveredDevice = DiscoveredDevice(
            id = "AP_DIRECT:TCP:${settings.apHost}:${settings.apPort}",
            name = settings.apSsid,
            endpoint = settings.apEndpoint(),
            transportKind = TransportKind.TCP,
            signalDbm = null,
            discoveredAtEpochMillis = 0L,
            discoverySource = DiscoverySource.AP_DIRECT,
            isSaved = settings.lastDeviceApDirect && settings.apHost == settings.lastDeviceAddress &&
                settings.apPort == settings.lastDevicePort,
        )

        fun from(candidate: TransportCandidate, settings: AppSettings): DiscoveredDevice {
            val id = "${candidate.kind}:${candidate.endpoint.address}:${candidate.endpoint.port ?: 0}"
            return DiscoveredDevice(
                id = id,
                name = candidate.name,
                endpoint = candidate.endpoint,
                transportKind = candidate.kind,
                signalDbm = candidate.signalDbm,
                discoveredAtEpochMillis = candidate.foundAtEpochMillis,
                discoverySource = candidate.discoverySource,
                isSaved = candidate.endpoint.wifiOnly == settings.lastDeviceApDirect &&
                    candidate.endpoint.address == settings.lastDeviceAddress &&
                    (candidate.endpoint.port ?: 0) == settings.lastDevicePort,
            )
        }
    }
}

enum class RepositoryConnectionPhase {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    HANDSHAKING,
    CONNECTED,
    DISCONNECTING,
    LOST,
    RECONNECTING,
    FAILED,
    UNSUPPORTED,
}

enum class HandshakeStatus {
    NOT_STARTED,
    SAFE_STOP_PROBE,
    VERIFIED_V1_COMPATIBLE,
    FAILED,
}

data class ConnectionInfo(
    val phase: RepositoryConnectionPhase = RepositoryConnectionPhase.DISCONNECTED,
    val detail: String? = null,
    val selectedDeviceId: String? = null,
    val connectedDevice: DiscoveredDevice? = null,
    val handshakeStatus: HandshakeStatus = HandshakeStatus.NOT_STARTED,
    /** 当前 STM32 V1 没有版本字段，因此只能表示 APK 期望值，不能表示设备自报版本。 */
    val expectedProtocolVersion: Int = 1,
    val deviceReportedProtocolVersion: Int? = null,
    val deviceReportedType: String? = null,
    val isApDirect: Boolean = false,
)

enum class MessageSeverity { INFO, SUCCESS, WARNING, ERROR }

data class RepositoryMessage(
    val id: Long,
    val severity: MessageSeverity,
    val text: String,
    val actionLabel: String? = null,
)

enum class LogDirection { TX, RX, SYSTEM }

data class CommunicationLogEntry(
    val timestampEpochMillis: Long,
    val direction: LogDirection,
    val text: String,
)

data class RepositoryState(
    val connection: ConnectionInfo = ConnectionInfo(),
    val devices: List<DiscoveredDevice> = emptyList(),
    val telemetry: TelemetryState = TelemetryState(),
    val settings: AppSettings = AppSettings(),
    val bluetoothCapabilities: BluetoothCapabilities = BluetoothCapabilities(false, false),
    val latestMessage: RepositoryMessage? = null,
    val logs: List<CommunicationLogEntry> = emptyList(),
    val protocolLatencyMillis: Long? = null,
    val sentFrameCount: Long = 0L,
    val receivedFrameCount: Long = 0L,
    val reverseCommandAllowed: Boolean = false,
    val stepperParametersConfirmed: Boolean = false,
    /** 本地安全停止意图代数；意图建立即递增，供 UI 清除残留 FORWARD 草稿。 */
    val safetyStopGeneration: Long = 0L,
)
