package com.bionicfish.controller.ui.model

/**
 * UI 层只依赖这些不可变视图状态，不直接依赖网络、蓝牙或协议实现。
 * ViewModel 负责将领域层 StateFlow 映射为 [AppUiState]。
 */
data class AppUiState(
    val connection: ConnectionUiState = ConnectionUiState(),
    val devices: List<DeviceUiModel> = emptyList(),
    val control: ControlUiState = ControlUiState(),
    val telemetry: TelemetryUiState = TelemetryUiState(),
    val settings: SettingsUiState = SettingsUiState(),
    val banner: BannerUiModel? = null,
    val initialDestination: AppDestination = AppDestination.DEVICES,
)

enum class AppDestination(val route: String) {
    DEVICES("devices"),
    CONTROL("control"),
    TELEMETRY("telemetry"),
    SETTINGS("settings"),
}

enum class TransportKind {
    WIFI_TCP,
    WIFI_UDP,
    BLUETOOTH_CLASSIC,
    BLUETOOTH_LE,
    SIMULATOR,
}

enum class ConnectionPhase {
    DISCONNECTED,
    SCANNING,
    CONNECTING,
    DISCONNECTING,
    CONNECTED,
    LOST,
    RECONNECTING,
}

enum class HandshakePhase {
    NOT_STARTED,
    VERIFYING,
    VERIFIED,
    FAILED,
}

data class ConnectionUiState(
    val phase: ConnectionPhase = ConnectionPhase.DISCONNECTED,
    val transport: TransportKind = TransportKind.WIFI_TCP,
    val selectedDeviceId: String? = null,
    val connectedDeviceName: String? = null,
    val detail: String? = null,
    val handshake: HandshakePhase = HandshakePhase.NOT_STARTED,
    val protocolVersion: String? = null,
    val deviceType: String? = null,
    val bluetoothHardwareAvailable: Boolean = false,
)

data class DeviceUiModel(
    val id: String,
    val name: String,
    val address: String,
    val signalDbm: Int? = null,
    val discoveredAt: String,
    val isSaved: Boolean = false,
    val isSelected: Boolean = false,
    val isConnecting: Boolean = false,
)

enum class MoveDirection {
    FORWARD,
    REVERSE,
    STOP,
}

enum class TurnDirection {
    LEFT,
    CENTER,
    RIGHT,
}

enum class ControlMode {
    HOLD_TO_RUN,
    EXPLICIT_STOP,
}

data class ControlUiState(
    val enabled: Boolean = false,
    val move: MoveDirection = MoveDirection.STOP,
    val turn: TurnDirection = TurnDirection.CENTER,
    val stepSpeedRpm: Int = 60,
    val servoAngleDegrees: Int = 0,
    val reverseSupported: Boolean = false,
    val stepperParametersConfirmed: Boolean = false,
    val mode: ControlMode = ControlMode.EXPLICIT_STOP,
    val stopPending: Boolean = false,
)

data class TelemetryUiState(
    val targetStepRpm: Int = 0,
    val estimatedStepRpm: Int? = null,
    val actualStepRpm: Int? = null,
    val servoAngleDegrees: Int = 0,
    val rollDegrees: Float? = null,
    val pitchDegrees: Float? = null,
    val yawDegrees: Float? = null,
    val linkAlive: Boolean = false,
    val latencyMs: Long? = null,
    val lastUpdatedLabel: String = "尚无数据",
    val isStale: Boolean = true,
    val faultCode: Int? = null,
    val faultSummary: String = "无数据",
    val lastSequence: Int? = null,
    val receivedFrameCount: Long = 0,
    val sentFrameCount: Long = 0,
    val logLines: List<String> = emptyList(),
)

data class SettingsUiState(
    val wifiHost: String = "",
    val wifiPort: String = "",
    val discoveryAddress: String = "",
    val discoveryPort: String = "",
    val discoveryPayload: String = "",
    val selectedTransport: TransportKind = TransportKind.WIFI_TCP,
    val controlPeriodMs: String = "",
    val heartbeatPeriodMs: String = "",
    val linkTimeoutMs: String = "",
    val bluetoothClassicUuid: String = "",
    val bluetoothLeServiceUuid: String = "",
    val bluetoothLeCharacteristicUuid: String = "",
    val simulatorEnabled: Boolean = false,
    val sendSafeStopOnBackground: Boolean = true,
    val centerServoOnSafeStop: Boolean = true,
    val reduceMotion: Boolean = false,
    val connectionParametersConfirmed: Boolean = false,
    val exportingLogs: Boolean = false,
)

enum class BannerLevel {
    INFO,
    SUCCESS,
    WARNING,
    ERROR,
}

data class BannerUiModel(
    val message: String,
    val level: BannerLevel,
    val actionLabel: String? = null,
)

/**
 * UI 事件集。空回调便于 Preview/UI test，正式运行时由 ViewModel 统一注入。
 */
data class UiActions(
    val onSelectTransport: (TransportKind) -> Unit = {},
    val onStartScan: () -> Unit = {},
    val onCancelScan: () -> Unit = {},
    val onConnect: (String) -> Unit = {},
    val onDisconnect: () -> Unit = {},
    val onRetryConnection: () -> Unit = {},
    val onForgetDevice: (String) -> Unit = {},
    val onDismissBanner: () -> Unit = {},
    val onBannerAction: () -> Unit = {},
    val onMoveChanged: (MoveDirection) -> Unit = {},
    val onStepSpeedChanged: (Int) -> Unit = {},
    /** turn 和 servo 必须原子更新，避免发出瞬时语义不一致的控制帧。 */
    val onSteeringChanged: (TurnDirection, Int) -> Unit = { _, _ -> },
    val onEmergencyStop: () -> Unit = {},
    val onControlModeChanged: (ControlMode) -> Unit = {},
    val onWifiHostChanged: (String) -> Unit = {},
    val onWifiPortChanged: (String) -> Unit = {},
    val onDiscoveryAddressChanged: (String) -> Unit = {},
    val onDiscoveryPortChanged: (String) -> Unit = {},
    val onDiscoveryPayloadChanged: (String) -> Unit = {},
    val onControlPeriodChanged: (String) -> Unit = {},
    val onHeartbeatPeriodChanged: (String) -> Unit = {},
    val onLinkTimeoutChanged: (String) -> Unit = {},
    val onBluetoothClassicUuidChanged: (String) -> Unit = {},
    val onBluetoothLeServiceUuidChanged: (String) -> Unit = {},
    val onBluetoothLeCharacteristicUuidChanged: (String) -> Unit = {},
    val onSimulatorEnabledChanged: (Boolean) -> Unit = {},
    val onSafeStopOnBackgroundChanged: (Boolean) -> Unit = {},
    val onCenterServoOnSafeStopChanged: (Boolean) -> Unit = {},
    val onReduceMotionChanged: (Boolean) -> Unit = {},
    val onSaveSettings: () -> Unit = {},
    val onExportLogs: () -> Unit = {},
    val onClearLogs: () -> Unit = {},
)
