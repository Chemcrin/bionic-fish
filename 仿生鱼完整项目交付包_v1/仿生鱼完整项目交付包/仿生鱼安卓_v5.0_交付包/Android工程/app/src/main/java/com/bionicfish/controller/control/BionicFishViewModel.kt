package com.bionicfish.controller.control

import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import com.bionicfish.controller.device.BionicFishRepository
import com.bionicfish.controller.device.HandshakeStatus
import com.bionicfish.controller.device.MessageSeverity
import com.bionicfish.controller.device.RepositoryConnectionPhase
import com.bionicfish.controller.device.RepositoryState
import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.protocol.ControlCommand
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.StepSpeed
import com.bionicfish.controller.protocol.Turn
import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.telemetry.DataFreshness
import com.bionicfish.controller.transport.TransportKind as DomainTransportKind
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.BannerLevel
import com.bionicfish.controller.ui.model.BannerUiModel
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.ControlMode
import com.bionicfish.controller.ui.model.ControlUiState
import com.bionicfish.controller.ui.model.DeviceUiModel
import com.bionicfish.controller.ui.model.HandshakePhase
import com.bionicfish.controller.ui.model.MoveDirection
import com.bionicfish.controller.ui.model.SettingsUiState
import com.bionicfish.controller.ui.model.TelemetryUiState
import com.bionicfish.controller.ui.model.TransportKind as UiTransportKind
import com.bionicfish.controller.ui.model.TurnDirection
import com.bionicfish.controller.ui.model.UiActions
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.util.Locale
import java.util.concurrent.atomic.AtomicInteger
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

sealed interface AppEvent {
    data class CreateLogDocument(val suggestedName: String) : AppEvent
    data object OpenWifiSettings : AppEvent
}

/**
 * 连接 UI 与领域仓库。所有套接字、协议解析和重连均留在仓库层，Composable 只发送意图。
 */
class BionicFishViewModel(
    private val repository: BionicFishRepository,
) : ViewModel() {
    private val controlDraft = MutableStateFlow(ControlUiState())
    private val settingsDraft = MutableStateFlow<SettingsUiState?>(null)
    private val localBanner = MutableStateFlow<BannerUiModel?>(null)
    private val stopPending = MutableStateFlow(false)
    private val mutableEvents = MutableSharedFlow<AppEvent>(extraBufferCapacity = 1)
    private val transportSwitchMutex = Mutex()
    private val safetySettingsMutex = Mutex()
    private val pendingStopRequests = AtomicInteger(0)
    private var appliedSafetyStopGeneration = repository.state.value.safetyStopGeneration
    private var settingsSavePending = false
    private var reverseDisablePending = false
    private var pendingExportText: String? = null

    val events: SharedFlow<AppEvent> = mutableEvents.asSharedFlow()

    val uiState: StateFlow<AppUiState> = combine(
        repository.state,
        controlDraft,
        settingsDraft,
        localBanner,
        stopPending,
    ) { repositoryState, localControl, localSettings, banner, stopping ->
        repositoryState.toUiState(
            controlDraft = localControl,
            settingsDraft = localSettings,
            localBanner = banner,
            stopPending = stopping,
        )
    }.stateIn(
        scope = viewModelScope,
        started = SharingStarted.WhileSubscribed(5_000L),
        initialValue = AppUiState(),
    )

    val actions = UiActions(
        onSelectTransport = ::selectTransport,
        onStartScan = { launchRepositoryAction("扫描失败") { repository.scan() } },
        onCancelScan = repository::cancelScan,
        onConnect = { id -> launchRepositoryAction("连接失败") { repository.connect(id) } },
        onConnectApDirect = ::connectApDirect,
        onOpenWifiSettings = ::openWifiSettings,
        onDisconnect = ::disconnect,
        onRetryConnection = ::retryConnection,
        onForgetDevice = ::forgetDevice,
        onDismissBanner = ::dismissBanner,
        onBannerAction = ::retryConnection,
        onMoveChanged = ::setMove,
        onMotor2Changed = ::setMotor2,
        onSteeringChanged = ::setSteering,
        onEmergencyStop = { emergencyStop("用户触发或离开控制页") },
        onControlModeChanged = ::setControlMode,
        onWifiHostChanged = { editSettings { copy(wifiHost = it) } },
        onWifiPortChanged = { editSettings { copy(wifiPort = it) } },
        onApSsidChanged = { editSettings { copy(apSsid = it) } },
        onApHostChanged = { editSettings { copy(apHost = it) } },
        onApPortChanged = { editSettings { copy(apPort = it) } },
        onDiscoveryAddressChanged = { editSettings { copy(discoveryAddress = it) } },
        onDiscoveryPortChanged = { editSettings { copy(discoveryPort = it) } },
        onDiscoveryPayloadChanged = { editSettings { copy(discoveryPayload = it) } },
        onControlPeriodChanged = { editSettings { copy(controlPeriodMs = it) } },
        onHeartbeatPeriodChanged = { editSettings { copy(heartbeatPeriodMs = it) } },
        onLinkTimeoutChanged = { editSettings { copy(linkTimeoutMs = it) } },
        onBluetoothClassicUuidChanged = { editSettings { copy(bluetoothClassicUuid = it) } },
        onBluetoothLeServiceUuidChanged = { editSettings { copy(bluetoothLeServiceUuid = it) } },
        onBluetoothLeCharacteristicUuidChanged = {
            editSettings { copy(bluetoothLeCharacteristicUuid = it) }
        },
        onSimulatorEnabledChanged = ::setSimulatorEnabled,
        onSafeStopOnBackgroundChanged = ::setLifecycleSafeStopEnabled,
        onCenterServoOnSafeStopChanged = ::setCenterServoOnSafeStop,
        onReduceMotionChanged = { editSettings { copy(reduceMotion = it) } },
        onAllowReverseCommandChanged = { editSettings { copy(allowReverseCommand = it) } },
        onSaveSettings = ::saveSettings,
        onExportLogs = ::prepareLogExport,
        onClearLogs = repository::clearLogs,
    )

    init {
        // 任何失联/重连/握手阶段都把本地控制显示恢复为停止，避免重连后误示为仍在运行。
        viewModelScope.launch {
            repository.state
                .map {
                    Triple(
                        it.connection.phase,
                        it.settings.centerServoOnSafeStop,
                        it.safetyStopGeneration,
                    )
                }
                .distinctUntilChanged()
                .collect { synchronizeControlDraft() }
        }
    }

    private fun synchronizeControlDraft() {
        val snapshot = repository.state.value
        val safetyStopObserved = snapshot.safetyStopGeneration != appliedSafetyStopGeneration
        appliedSafetyStopGeneration = snapshot.safetyStopGeneration
        // Application 的后台全停可能先于 Main 上的 collector 执行；按键必须先同步代次，
        // 再构造完整目标，否则仅改舵角也可能把另一路的旧 FORWARD 重新带回去。
        if (snapshot.connection.phase != RepositoryConnectionPhase.CONNECTED || safetyStopObserved) {
            controlDraft.update {
                if (snapshot.settings.centerServoOnSafeStop) {
                    it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP,
                        turn = TurnDirection.CENTER, servoAngleDegrees = 0)
                } else {
                    it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP)
                }
            }
        }
    }

    fun consumePendingExportText(): String? = pendingExportText.also { pendingExportText = null }

    fun reportWifiSettingsFailure() = showError("无法打开系统 Wi-Fi 设置，请手动打开设置并加入鱼端热点")

    private fun openWifiSettings() {
        launchRepositoryAction("打开 Wi-Fi 设置失败") {
            // 已失联但仍有重连任务时，先取消会话再离开应用，避免旧连接在后台复活。
            val phase = repository.state.value.connection.phase
            if (phase == RepositoryConnectionPhase.LOST) repository.disconnect("重新选择 AP 热点")
            else check(phase in setOf(RepositoryConnectionPhase.DISCONNECTED,
                RepositoryConnectionPhase.FAILED, RepositoryConnectionPhase.UNSUPPORTED)) {
                "请先安全断开或取消扫描"
            }
            mutableEvents.emit(AppEvent.OpenWifiSettings)
        }
    }

    private fun connectApDirect() {
        launchRepositoryAction("AP 直连失败") {
            transportSwitchMutex.withLock {
                val saved = repository.state.value.settings
                val draft = settingsDraft.value
                check(draft == null || (draft.apSsid == saved.apSsid &&
                    draft.apHost == saved.apHost && draft.apPort == saved.apPort.toString())) {
                    "AP 参数尚未保存，请先到设置页保存后再连接"
                }
                repository.connectApDirect()
                settingsDraft.value?.let {
                    editSettings { copy(selectedTransport = repository.state.value.settings.transportKind.toUi(),
                        simulatorEnabled = repository.state.value.settings.transportKind == DomainTransportKind.MOCK) }
                }
            }
        }
    }

    fun reportExportResult(success: Boolean, cancelled: Boolean = false, detail: String? = null) {
        localBanner.value = when {
            success -> BannerUiModel("通信日志已导出", BannerLevel.SUCCESS)
            cancelled -> BannerUiModel("已取消导出", BannerLevel.INFO)
            else -> BannerUiModel("日志导出失败：${detail ?: "无法写入所选位置"}", BannerLevel.ERROR)
        }
    }

    private fun setMove(value: MoveDirection) {
        synchronizeControlDraft()
        // 单路停止与全局急停分开：M1 按键/松手只停 M1，完整命令保留 M2 与舵角。
        submitControl(controlDraft.value.copy(move = value), priorityStop = value == MoveDirection.STOP)
    }

    private fun setMotor2(value: MoveDirection) {
        synchronizeControlDraft()
        submitControl(controlDraft.value.copy(motor2 = value), priorityStop = value == MoveDirection.STOP)
    }

    private fun setSteering(turn: TurnDirection, requestedDegrees: Int) {
        synchronizeControlDraft()
        val degrees = requestedDegrees.coerceIn(ControlCommand.SERVO_RANGE)
        val normalizedTurn = when {
            degrees < 0 -> TurnDirection.LEFT
            degrees > 0 -> TurnDirection.RIGHT
            else -> TurnDirection.CENTER
        }
        if (turn != normalizedTurn) {
            showError("转向与舵机角度不一致，命令未发送")
            return
        }
        submitControl(controlDraft.value.copy(turn = normalizedTurn, servoAngleDegrees = degrees))
    }

    private fun submitControl(next: ControlUiState, priorityStop: Boolean = false) {
        if (reverseDisablePending) {
            showError("正在双路停止并保存反向设置，请等待完成；全局安全停止仍可使用")
            return
        }
        val generationAtRequest = appliedSafetyStopGeneration
        if (generationAtRequest != repository.state.value.safetyStopGeneration) {
            synchronizeControlDraft()
            return
        }
        if (!repository.state.value.settings.allowReverseCommand &&
            (next.move == MoveDirection.REVERSE || next.motor2 == MoveDirection.REVERSE)
        ) {
            showError("电机反向已关闭，请在设置中允许 M1/M2 反向后保存")
            return
        }
        controlDraft.value = next.copy(stepSpeedRpm = StepSpeed.SLOW.rpm)
        val input = ControlInput(
            move = next.move.toDomain(),
            motor2 = next.motor2.toDomain(),
            turn = next.turn.toDomain(),
            // 双直流电机占空比由固件固定；60 只让旧协议字段合法，不改变输出。
            stepSpeed = StepSpeed.SLOW,
            servoDegrees = next.servoAngleDegrees.coerceIn(ControlCommand.SERVO_RANGE),
        )
        launchRepositoryAction("控制指令发送失败") {
            // launch 尚未开始时发生全停或关闭反向，不得让已捕获的旧完整目标越过屏障。
            if (reverseDisablePending || generationAtRequest != repository.state.value.safetyStopGeneration) {
                synchronizeControlDraft()
                return@launchRepositoryAction
            }
            if (priorityStop) repository.sendMotorStop(input) else repository.sendControl(input)
        }
    }

    private fun emergencyStop(reason: String) {
        val centerServo = repository.state.value.settings.centerServoOnSafeStop
        controlDraft.update {
            if (centerServo) {
                it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP, turn = TurnDirection.CENTER, servoAngleDegrees = 0)
            } else {
                it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP)
            }
        }
        pendingStopRequests.incrementAndGet()
        stopPending.value = true
        viewModelScope.launch {
            try {
                runCatching { repository.sendSafeStop(reason) }
                    .onFailure { showError("安全停止发送失败：${it.message ?: "链路不可用"}") }
            } finally {
                // 多次快速触发停止时，必须等最后一项真正结束后才隐藏“正在停止”。
                stopPending.value = pendingStopRequests.decrementAndGet().coerceAtLeast(0) > 0
            }
        }
    }

    private fun disconnect() {
        val centerServo = repository.state.value.settings.centerServoOnSafeStop
        controlDraft.update {
            if (centerServo) {
                it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP, turn = TurnDirection.CENTER, servoAngleDegrees = 0)
            } else {
                it.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP)
            }
        }
        launchRepositoryAction("断开失败") { repository.disconnect("用户安全断开") }
    }

    private fun retryConnection() {
        val id = repository.state.value.connection.selectedDeviceId
        if (id == null) {
            showError("没有可重试的已选设备，请重新扫描")
            return
        }
        launchRepositoryAction("重连失败") { repository.connect(id) }
    }

    private fun forgetDevice(deviceId: String) {
        val state = repository.state.value
        val device = state.devices.firstOrNull { it.id == deviceId } ?: return
        val settings = state.settings
        val matchesSaved = device.endpoint.address == settings.lastDeviceAddress &&
            (device.endpoint.port ?: 0) == settings.lastDevicePort
        if (!matchesSaved) return
        launchRepositoryAction("忘记设备失败") {
            repository.updateSettings(
                settings.copy(lastDeviceName = "", lastDeviceAddress = "", lastDevicePort = 0, lastDeviceApDirect = false),
            )
        }
    }

    private fun setControlMode(mode: ControlMode) {
        // 运行中切换控制手势会使原按压生命周期失效；先停机，避免留下持续运动意图。
        if (controlDraft.value.move != MoveDirection.STOP || controlDraft.value.motor2 != MoveDirection.STOP) {
            emergencyStop("切换控制模式")
        }
        controlDraft.update { it.copy(mode = mode) }
        val settings = repository.state.value.settings.copy(
            pressAndHoldToMove = mode == ControlMode.HOLD_TO_RUN,
        )
        launchRepositoryAction("控制模式保存失败") { repository.updateSettings(settings) }
    }

    private fun setLifecycleSafeStopEnabled(enabled: Boolean) {
        viewModelScope.launch {
            safetySettingsMutex.withLock {
                try {
                    repository.updateSettings(
                        repository.state.value.settings.copy(
                            safeStopOnBackground = enabled,
                            safeStopOnControlExit = enabled,
                        ),
                    )
                    // 安全策略只在仓库持久化成功后反映到 UI，保证控制页退出和
                    // Application 后台回调始终读取同一个有效值，不出现 draft 分裂。
                    settingsDraft.update { draft ->
                        (draft ?: repository.state.value.settings.toUiSettings()).copy(
                            sendSafeStopOnBackground = enabled,
                        )
                    }
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (error: Exception) {
                    showError("安全停止策略保存失败：${error.message ?: "未知错误"}")
                }
            }
        }
    }

    private fun setCenterServoOnSafeStop(enabled: Boolean) {
        viewModelScope.launch {
            safetySettingsMutex.withLock {
                try {
                    repository.updateSettings(
                        repository.state.value.settings.copy(centerServoOnSafeStop = enabled),
                    )
                    settingsDraft.update { draft ->
                        (draft ?: repository.state.value.settings.toUiSettings()).copy(
                            centerServoOnSafeStop = enabled,
                        )
                    }
                } catch (cancelled: CancellationException) {
                    throw cancelled
                } catch (error: Exception) {
                    showError("舵机安全策略保存失败：${error.message ?: "未知错误"}")
                }
            }
        }
    }

    private fun selectTransport(kind: UiTransportKind) {
        // 当前鱼端只有 ESP-01S；蓝牙入口默认隐藏，这里仍防止外部调用绕过 UI。
        if (kind == UiTransportKind.BLUETOOTH_CLASSIC || kind == UiTransportKind.BLUETOOTH_LE) {
            showError("鱼端未确认外接蓝牙硬件，不能启用蓝牙传输")
            return
        }
        viewModelScope.launch {
            transportSwitchMutex.withLock {
                val before = repository.state.value
                runCatching {
                    when (before.connection.phase) {
                        RepositoryConnectionPhase.SCANNING -> repository.cancelScan()
                        RepositoryConnectionPhase.DISCONNECTED,
                        RepositoryConnectionPhase.FAILED,
                        RepositoryConnectionPhase.UNSUPPORTED,
                        -> Unit
                        else -> {
                            // 传输类型与当前 Socket 必须原子切换：先停机并断开旧链路。
                            repository.disconnect("切换传输方式")
                        }
                    }
                    repository.updateSettings(
                        repository.state.value.settings.copy(transportKind = kind.toDomain()),
                    )
                }.onSuccess {
                    editSettings {
                        copy(
                            selectedTransport = kind,
                            simulatorEnabled = kind == UiTransportKind.SIMULATOR,
                        )
                    }
                    localBanner.value = BannerUiModel("传输方式已切换，请重新扫描并连接", BannerLevel.INFO)
                }.onFailure {
                    showError("切换传输方式失败：${it.message ?: "未知错误"}")
                }
            }
        }
    }

    private fun setSimulatorEnabled(enabled: Boolean) {
        selectTransport(if (enabled) UiTransportKind.SIMULATOR else UiTransportKind.WIFI_TCP)
    }

    private fun editSettings(transform: SettingsUiState.() -> SettingsUiState) {
        val current = settingsDraft.value ?: repository.state.value.settings.toUiSettings()
        settingsDraft.value = current.transform().withConfirmationState()
    }

    private fun saveSettings() {
        if (settingsSavePending) {
            showError("设置正在保存，请等待当前保存完成")
            return
        }
        val draft = settingsDraft.value ?: repository.state.value.settings.toUiSettings()
        val controlPeriod = draft.controlPeriodMs.toLongOrNull()
        val heartbeatPeriod = draft.heartbeatPeriodMs.toLongOrNull()
        val linkTimeout = draft.linkTimeoutMs.toLongOrNull()
        val wifiPort = draft.wifiPort.toIntOrNull() ?: if (draft.wifiPort.isBlank()) 0 else -1
        val apPort = draft.apPort.toIntOrNull() ?: -1
        val discoveryPort = draft.discoveryPort.toIntOrNull()
            ?: if (draft.discoveryPort.isBlank()) 0 else -1
        if (controlPeriod == null || heartbeatPeriod == null || linkTimeout == null) {
            showError("控制周期、心跳周期和失联超时必须填写有效整数")
            return
        }
        val updated = repository.state.value.settings.copy(
            transportKind = draft.selectedTransport.toDomain(),
            wifiHost = draft.wifiHost.trim(),
            wifiPort = wifiPort,
            apSsid = draft.apSsid.trim(),
            apHost = draft.apHost.trim(),
            apPort = apPort,
            discoveryAddress = draft.discoveryAddress.trim(),
            discoveryPort = discoveryPort,
            discoveryPayload = draft.discoveryPayload,
            bluetoothClassicUuid = draft.bluetoothClassicUuid.trim(),
            bluetoothLeServiceUuid = draft.bluetoothLeServiceUuid.trim(),
            bluetoothLeCharacteristicUuid = draft.bluetoothLeCharacteristicUuid.trim(),
            controlSendPeriodMillis = controlPeriod,
            heartbeatPeriodMillis = heartbeatPeriod,
            linkTimeoutMillis = linkTimeout,
            safeStopOnBackground = draft.sendSafeStopOnBackground,
            safeStopOnControlExit = draft.sendSafeStopOnBackground,
            centerServoOnSafeStop = draft.centerServoOnSafeStop,
            reduceMotion = draft.reduceMotion,
            allowReverseCommand = draft.allowReverseCommand,
        )
        settingsSavePending = true
        // 必须在 launch 前关入口；等待 STOP ACK / DataStore 写入期间也不允许新运动插入。
        reverseDisablePending = !updated.allowReverseCommand && repository.state.value.settings.allowReverseCommand
        viewModelScope.launch {
            try {
                runCatching {
                    updated.validated()
                    // 关闭反向时先确认双停，不能仅隐藏按钮却继续保活上一条反转目标。
                    if (reverseDisablePending &&
                        repository.state.value.connection.phase == RepositoryConnectionPhase.CONNECTED
                    ) {
                        repository.sendSafeStop("关闭电机反向前停止两路")
                    }
                    repository.updateSettings(updated)
                }
                    .onSuccess {
                        settingsDraft.value = null
                        localBanner.value = BannerUiModel("设置已保存", BannerLevel.SUCCESS)
                    }
                    .onFailure {
                        if (it is CancellationException) throw it
                        showError("设置无效：${it.message ?: "请检查输入范围"}")
                    }
            } finally {
                reverseDisablePending = false
                settingsSavePending = false
            }
        }
    }

    private fun prepareLogExport() {
        viewModelScope.launch {
            runCatching { repository.exportLogs() }
                .onSuccess { text ->
                    pendingExportText = text
                    val timestamp = FILE_NAME_FORMATTER.format(Instant.now().atZone(ZoneId.systemDefault()))
                    mutableEvents.emit(AppEvent.CreateLogDocument("bionic-fish-log-$timestamp.txt"))
                }
                .onFailure { showError("日志准备失败：${it.message ?: "未知错误"}") }
        }
    }

    private fun dismissBanner() {
        if (localBanner.value != null) {
            localBanner.value = null
        } else {
            repository.state.value.latestMessage?.id?.let(repository::dismissMessage)
        }
    }

    private fun launchRepositoryAction(prefix: String, block: suspend () -> Unit) {
        viewModelScope.launch {
            runCatching { block() }
                .onFailure {
                    // 取消扫描/切换页面属于正常协程取消，不能误报成操作失败。
                    if (it is CancellationException) throw it
                    showError("$prefix：${it.message ?: "未知错误"}")
                }
        }
    }

    private fun showError(message: String) {
        localBanner.value = BannerUiModel(message, BannerLevel.ERROR)
    }

    class Factory(
        private val repository: BionicFishRepository,
    ) : ViewModelProvider.Factory {
        @Suppress("UNCHECKED_CAST")
        override fun <T : ViewModel> create(modelClass: Class<T>): T {
            require(modelClass.isAssignableFrom(BionicFishViewModel::class.java))
            return BionicFishViewModel(repository) as T
        }
    }

    private companion object {
        val TIME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss", Locale.CHINA)
        val FILE_NAME_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("yyyyMMdd-HHmmss", Locale.ROOT)
    }
}

private fun RepositoryState.toUiState(
    controlDraft: ControlUiState,
    settingsDraft: SettingsUiState?,
    localBanner: BannerUiModel?,
    stopPending: Boolean,
): AppUiState {
    val connected = connection.phase == RepositoryConnectionPhase.CONNECTED
    val selected = connection.selectedDeviceId
    val savedAddress = settings.lastDeviceAddress
    val savedPort = settings.lastDevicePort
    val telemetrySnapshot = telemetry.snapshot
    return AppUiState(
        connection = ConnectionUiState(
            phase = connection.phase.toUi(),
            transport = settings.transportKind.toUi(),
            selectedDeviceId = selected,
            connectedDeviceName = connection.connectedDevice?.name,
            detail = connection.detail,
            handshake = connection.handshakeStatus.toUi(),
            protocolVersion = if (connection.handshakeStatus == HandshakeStatus.VERIFIED_V1_COMPATIBLE) {
                "V${connection.expectedProtocolVersion} 兼容"
            } else {
                null
            },
            // V1 固件没有设备类型字段，不能把连接成功当成设备自报身份。
            deviceType = connection.deviceReportedType,
            isApDirect = connection.isApDirect,
            // 手机具备蓝牙并不代表鱼端有蓝牙；ESP-01S-only 版本必须保持隐藏。
            bluetoothHardwareAvailable = false,
        ),
        devices = devices.map { device ->
            val port = device.endpoint.port
            DeviceUiModel(
                id = device.id,
                name = device.name,
                address = if (port == null) device.endpoint.address else "${device.endpoint.address}:$port",
                signalDbm = device.signalDbm,
                discoveredAt = if (device.endpoint.wifiOnly) "固定预置（非扫描）"
                    else formatTimestamp(device.discoveredAtEpochMillis),
                isSaved = device.endpoint.wifiOnly == settings.lastDeviceApDirect &&
                    device.endpoint.address == savedAddress && (port ?: 0) == savedPort,
                isApDirect = device.endpoint.wifiOnly,
                isSelected = selected == device.id,
                isConnecting = selected == device.id && connection.phase in setOf(
                    RepositoryConnectionPhase.CONNECTING,
                    RepositoryConnectionPhase.HANDSHAKING,
                    RepositoryConnectionPhase.RECONNECTING,
                ),
            )
        },
        control = controlDraft.copy(
            enabled = connected,
            reverseSupported = reverseCommandAllowed,
            stepperParametersConfirmed = stepperParametersConfirmed,
            stopPending = stopPending,
            mode = if (settings.pressAndHoldToMove) {
                ControlMode.HOLD_TO_RUN
            } else {
                ControlMode.EXPLICIT_STOP
            },
        ),
        telemetry = TelemetryUiState(
            targetStepRpm = telemetrySnapshot?.stepTargetRpm,
            estimatedStepRpm = telemetrySnapshot?.stepEstimatedRpm,
            actualStepRpm = telemetrySnapshot?.stepActualRpm,
            stepRunning = telemetrySnapshot?.stepRunning,
            motor1Direction = telemetrySnapshot?.motor1Direction?.toUi(),
            motor2Direction = telemetrySnapshot?.motor2Direction?.toUi(),
            servoAngleDegrees = telemetrySnapshot?.servoDegrees ?: 0,
            rollDegrees = telemetrySnapshot?.rollDegrees?.toFloat(),
            pitchDegrees = telemetrySnapshot?.pitchDegrees?.toFloat(),
            yawDegrees = telemetrySnapshot?.yawDegrees?.toFloat(),
            linkAlive = telemetrySnapshot?.stm32LinkAlive == true,
            latencyMs = protocolLatencyMillis,
            lastUpdatedLabel = telemetrySnapshot?.let {
                val time = formatTimestamp(it.receivedAtEpochMillis)
                val age = telemetry.ageMillis?.let { millis -> "，${millis} ms 前" }.orEmpty()
                "$time$age"
            } ?: "尚无数据",
            isStale = telemetry.freshness != DataFreshness.FRESH,
            faultCode = telemetrySnapshot?.faultBits?.toInt(),
            faultSummary = when {
                telemetrySnapshot == null -> "无数据"
                telemetrySnapshot.activeFaults.isEmpty() -> "无故障"
                else -> telemetrySnapshot.activeFaults.joinToString("；") {
                    "${it.code}：${it.description}"
                }
            },
            lastSequence = telemetrySnapshot?.sequence,
            receivedFrameCount = receivedFrameCount,
            sentFrameCount = sentFrameCount,
            logLines = logs.map {
                "${formatTimestamp(it.timestampEpochMillis)} ${it.direction.name.padEnd(6)} ${it.text}"
            },
        ),
        settings = settingsDraft ?: settings.toUiSettings(),
        banner = localBanner ?: latestMessage?.let {
            BannerUiModel(
                message = it.text,
                level = when (it.severity) {
                    MessageSeverity.INFO -> BannerLevel.INFO
                    MessageSeverity.SUCCESS -> BannerLevel.SUCCESS
                    MessageSeverity.WARNING -> BannerLevel.WARNING
                    MessageSeverity.ERROR -> BannerLevel.ERROR
                },
                actionLabel = it.actionLabel,
            )
        },
    )
}

private fun AppSettings.toUiSettings(): SettingsUiState = SettingsUiState(
    apSsid = apSsid,
    apHost = apHost,
    apPort = apPort.toString(),
    wifiHost = wifiHost,
    wifiPort = wifiPort.takeIf { it != 0 }?.toString().orEmpty(),
    discoveryAddress = discoveryAddress,
    discoveryPort = discoveryPort.takeIf { it != 0 }?.toString().orEmpty(),
    discoveryPayload = discoveryPayload,
    selectedTransport = transportKind.toUi(),
    controlPeriodMs = controlSendPeriodMillis.toString(),
    heartbeatPeriodMs = heartbeatPeriodMillis.toString(),
    linkTimeoutMs = linkTimeoutMillis.toString(),
    bluetoothClassicUuid = bluetoothClassicUuid,
    bluetoothLeServiceUuid = bluetoothLeServiceUuid,
    bluetoothLeCharacteristicUuid = bluetoothLeCharacteristicUuid,
    simulatorEnabled = transportKind == DomainTransportKind.MOCK,
    sendSafeStopOnBackground = safeStopOnBackground && safeStopOnControlExit,
    centerServoOnSafeStop = centerServoOnSafeStop,
    reduceMotion = reduceMotion,
    allowReverseCommand = allowReverseCommand,
    connectionParametersConfirmed = when (transportKind) {
        DomainTransportKind.MOCK -> true
        DomainTransportKind.TCP, DomainTransportKind.UDP -> wifiHost.isNotBlank() && wifiPort in 1..65535
        DomainTransportKind.BLUETOOTH_CLASSIC, DomainTransportKind.BLUETOOTH_LE -> false
    },
).withConfirmationState()

private fun SettingsUiState.withConfirmationState(): SettingsUiState = copy(
    connectionParametersConfirmed = when (selectedTransport) {
        UiTransportKind.SIMULATOR -> true
        UiTransportKind.WIFI_TCP, UiTransportKind.WIFI_UDP ->
            wifiHost.isNotBlank() && wifiPort.toIntOrNull() in 1..65535
        UiTransportKind.BLUETOOTH_CLASSIC, UiTransportKind.BLUETOOTH_LE -> false
    },
)

private fun RepositoryConnectionPhase.toUi(): ConnectionPhase = when (this) {
    RepositoryConnectionPhase.SCANNING -> ConnectionPhase.SCANNING
    RepositoryConnectionPhase.CONNECTING, RepositoryConnectionPhase.HANDSHAKING -> ConnectionPhase.CONNECTING
    RepositoryConnectionPhase.DISCONNECTING -> ConnectionPhase.DISCONNECTING
    RepositoryConnectionPhase.CONNECTED -> ConnectionPhase.CONNECTED
    RepositoryConnectionPhase.LOST -> ConnectionPhase.LOST
    RepositoryConnectionPhase.RECONNECTING -> ConnectionPhase.RECONNECTING
    RepositoryConnectionPhase.DISCONNECTED,
    RepositoryConnectionPhase.FAILED,
    RepositoryConnectionPhase.UNSUPPORTED,
    -> ConnectionPhase.DISCONNECTED
}

private fun HandshakeStatus.toUi(): HandshakePhase = when (this) {
    HandshakeStatus.NOT_STARTED -> HandshakePhase.NOT_STARTED
    HandshakeStatus.SAFE_STOP_PROBE -> HandshakePhase.VERIFYING
    HandshakeStatus.VERIFIED_V1_COMPATIBLE -> HandshakePhase.VERIFIED
    HandshakeStatus.FAILED -> HandshakePhase.FAILED
}

private fun DomainTransportKind.toUi(): UiTransportKind = when (this) {
    DomainTransportKind.TCP -> UiTransportKind.WIFI_TCP
    DomainTransportKind.UDP -> UiTransportKind.WIFI_UDP
    DomainTransportKind.MOCK -> UiTransportKind.SIMULATOR
    DomainTransportKind.BLUETOOTH_CLASSIC -> UiTransportKind.BLUETOOTH_CLASSIC
    DomainTransportKind.BLUETOOTH_LE -> UiTransportKind.BLUETOOTH_LE
}

private fun UiTransportKind.toDomain(): DomainTransportKind = when (this) {
    UiTransportKind.WIFI_TCP -> DomainTransportKind.TCP
    UiTransportKind.WIFI_UDP -> DomainTransportKind.UDP
    UiTransportKind.SIMULATOR -> DomainTransportKind.MOCK
    UiTransportKind.BLUETOOTH_CLASSIC -> DomainTransportKind.BLUETOOTH_CLASSIC
    UiTransportKind.BLUETOOTH_LE -> DomainTransportKind.BLUETOOTH_LE
}

private fun MoveDirection.toDomain(): Move = when (this) {
    MoveDirection.FORWARD -> Move.FORWARD
    MoveDirection.REVERSE -> Move.REVERSE
    MoveDirection.STOP -> Move.STOP
}

private fun Move.toUi(): MoveDirection = when (this) {
    Move.FORWARD -> MoveDirection.FORWARD
    Move.REVERSE -> MoveDirection.REVERSE
    Move.STOP -> MoveDirection.STOP
}

private fun TurnDirection.toDomain(): Turn = when (this) {
    TurnDirection.LEFT -> Turn.LEFT
    TurnDirection.RIGHT -> Turn.RIGHT
    TurnDirection.CENTER -> Turn.CENTER
}

private fun formatTimestamp(epochMillis: Long): String = TIME_DISPLAY_FORMATTER.format(
    Instant.ofEpochMilli(epochMillis).atZone(ZoneId.systemDefault()),
)

private val TIME_DISPLAY_FORMATTER: DateTimeFormatter = DateTimeFormatter.ofPattern("HH:mm:ss", Locale.CHINA)
