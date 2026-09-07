package com.bionicfish.controller.device

import com.bionicfish.controller.protocol.AsciiFrameDecoder
import com.bionicfish.controller.protocol.AsciiProtocol
import com.bionicfish.controller.protocol.ControlCommand
import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.protocol.DecodeResult
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.ProtocolFrame
import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.settings.SettingsStore
import com.bionicfish.controller.telemetry.EpochClock
import com.bionicfish.controller.telemetry.SystemEpochClock
import com.bionicfish.controller.telemetry.TelemetryTracker
import com.bionicfish.controller.transport.BluetoothCapabilities
import com.bionicfish.controller.transport.BluetoothCapabilityProvider
import com.bionicfish.controller.transport.Transport
import com.bionicfish.controller.transport.TransportConnectionState
import com.bionicfish.controller.transport.TransportFactory
import com.bionicfish.controller.transport.TransportKind
import java.nio.charset.StandardCharsets
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicLong
import java.util.concurrent.atomic.AtomicReference
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.selects.select
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withTimeout

/**
 * 聚合真实/模拟传输、ASCII 协议、保活、超时与遥测。
 * V1 固件不支持 HELLO/PING：连接握手使用安全停止 CMD，并要求同序号 ACK 与 STA。
 */
class DefaultBionicFishRepository(
    private val settingsStore: SettingsStore,
    private val transportFactory: TransportFactory,
    bluetoothCapabilityProvider: BluetoothCapabilityProvider,
    private val scope: CoroutineScope,
    private val clock: EpochClock = SystemEpochClock,
    private val monotonicNanos: () -> Long = { System.nanoTime() },
) : BionicFishRepository {
    private data class Outbound(
        val bytes: ByteArray,
        val sequence: Int,
        val label: String,
        val commandTicket: Long,
        val survivesSafetyCutoff: Boolean,
        val requiresConnectedPhase: Boolean,
        val safetyTicket: Long? = null,
        val completion: CompletableDeferred<Unit>? = null,
        val acknowledgement: CompletableDeferred<Unit>? = null,
    )

    private data class DesiredCommand(
        val ticket: Long,
        val command: ControlCommand,
        val isSafetyStop: Boolean,
    )

    private class SupersededBySafetyStopException : IllegalStateException(
        "普通控制命令已被更新的安全停止命令取代",
    )

    private class ConnectionLostBeforeSendException : IllegalStateException(
        "普通控制命令发送前连接已失联",
    )

    private class CommandAcknowledgementTimeoutException(sequence: Int, timeoutMillis: Long) :
        IllegalStateException("命令 ACK 超时：seq=$sequence，等待 ${timeoutMillis} ms")

    private class HandshakeProbe(val sequence: Int) {
        var ackSeen = false
        var statusSeen = false
        val completion = CompletableDeferred<Unit>()

        fun completeIfReady() {
            if (ackSeen && statusSeen && !completion.isCompleted) completion.complete(Unit)
        }
    }

    private val capabilities = runCatching { bluetoothCapabilityProvider.detect() }
        .getOrDefault(BluetoothCapabilities(false, false))
    private val mutableState = MutableStateFlow(
        RepositoryState(bluetoothCapabilities = capabilities),
    )
    override val state: StateFlow<RepositoryState> = mutableState.asStateFlow()

    private val connectionMutex = Mutex()
    private val decoder = AsciiFrameDecoder()
    private val telemetryTracker = TelemetryTracker(scope, AppSettings().telemetryStaleMillis, clock)
    private val messageIds = AtomicLong(0L)
    private val requestTickets = AtomicLong(0L)
    private val safetyCutoffTicket = AtomicLong(0L)
    private val requiredSafetyTicket = AtomicLong(0L)
    private val completedSafetyTicket = AtomicLong(0L)
    private val sequenceCounter = AtomicInteger(0)
    /** 每次用户连接/断开都会递增；自动重连只能服务于创建它的连接意图。 */
    private val connectionIntentEpoch = AtomicLong(0L)
    private val desiredCommand = AtomicReference<DesiredCommand?>(null)
    private val pendingSendNanos = ConcurrentHashMap<Int, Long>()
    private val acknowledgementWaiters = ConcurrentHashMap<Int, CompletableDeferred<Unit>>()
    private val sessionJobs = mutableListOf<Job>()
    private var currentSettings = AppSettings()
    private var activeTransport: Transport? = null
    private var scanningTransport: Transport? = null
    private var activeDevice: DiscoveredDevice? = null
    private var urgentOutgoing: Channel<Outbound>? = null
    private var normalOutgoing: Channel<Outbound>? = null
    private var scanJob: Job? = null
    private var periodicJob: Job? = null
    private var reconnectJob: Job? = null
    private var handshakeProbe: HandshakeProbe? = null
    private var lastInboundNanos = 0L
    private var userRequestedDisconnect = false
    @Volatile
    private var activeSessionEpoch = 0L

    init {
        scope.launch {
            settingsStore.settings.collectLatest { value ->
                currentSettings = value.validated()
                telemetryTracker.updateStaleThreshold(currentSettings.telemetryStaleMillis)
                mutableState.update {
                    it.copy(
                        settings = currentSettings,
                        reverseCommandAllowed = currentSettings.allowReverseCommand,
                        connection = it.connection.copy(
                            expectedProtocolVersion = currentSettings.protocolVersionExpected,
                        ),
                    )
                }
            }
        }
        scope.launch {
            telemetryTracker.state.collectLatest { telemetry ->
                val confirmed = telemetry.snapshot?.let { it.faultBits and 16L == 0L } ?: false
                mutableState.update { it.copy(telemetry = telemetry, stepperParametersConfirmed = confirmed) }
            }
        }
    }

    override suspend fun scan() {
        val phaseAtRequest = mutableState.value.connection.phase
        if (activeTransport != null || phaseAtRequest in ACTIVE_SESSION_PHASES) {
            failMessage("活动连接期间不能扫描；请先安全断开当前设备")
            return
        }
        scanJob = kotlinx.coroutines.currentCoroutineContext()[Job]
        val previousPhase = mutableState.value.connection.phase
        val transport = transportFactory.create(currentSettings.transportKind)
        scanningTransport = transport
        mutableState.update {
            it.copy(connection = it.connection.copy(phase = RepositoryConnectionPhase.SCANNING, detail = "正在发现设备"))
        }
        try {
            val devices = transport.scan(currentSettings.scanRequest())
                .map { DiscoveredDevice.from(it, currentSettings) }
                .distinctBy { it.id }
            mutableState.update {
                it.copy(
                    devices = devices,
                    connection = it.connection.copy(
                        phase = if (previousPhase == RepositoryConnectionPhase.CONNECTED) {
                            RepositoryConnectionPhase.CONNECTED
                        } else {
                            RepositoryConnectionPhase.DISCONNECTED
                        },
                        detail = if (devices.isEmpty()) "未发现设备；请核对待确认的发现参数" else "发现 ${devices.size} 个设备",
                    ),
                )
            }
            log(LogDirection.SYSTEM, "扫描完成：${devices.size} 个设备")
        } catch (cancelled: CancellationException) {
            mutableState.update {
                it.copy(connection = it.connection.copy(phase = previousPhase, detail = "扫描已取消"))
            }
            throw cancelled
        } catch (error: Exception) {
            failMessage("扫描失败：${error.message ?: "未知错误"}")
            mutableState.update {
                it.copy(connection = it.connection.copy(phase = RepositoryConnectionPhase.FAILED, detail = "扫描失败"))
            }
        } finally {
            scanningTransport = null
            scanJob = null
        }
    }

    override fun cancelScan() {
        scanningTransport?.cancelScan()
        scanJob?.cancel()
    }

    override suspend fun connect(deviceId: String) {
        val device = mutableState.value.devices.firstOrNull { it.id == deviceId }
        if (device == null) {
            failMessage("所选设备已不在列表中，请重新扫描")
            return
        }
        val intentEpoch = connectionIntentEpoch.incrementAndGet()
        reconnectJob?.cancel()
        userRequestedDisconnect = false
        try {
            connectionMutex.withLock {
                if (connectionIntentEpoch.get() != intentEpoch) return@withLock
                var previousDeviceStopFailure: String? = null
                // 手动改连另一条鱼时，旧链路仍可用就必须先把紧急停止送到旧鱼并等 ACK；
                // 即使停止失败也继续销毁旧会话，避免同时控制两台设备。
                if (activeTransport != null) {
                    // 先关闭普通控制入口和周期保活；紧急队列仍保持可用以停止旧鱼。
                    mutableState.update {
                        it.copy(
                            connection = it.connection.copy(
                                phase = RepositoryConnectionPhase.CONNECTING,
                                detail = "正在安全停止旧设备并切换连接",
                            ),
                        )
                    }
                    runCatching { sendSafeStopInternal("切换连接前停止旧设备") }
                        .onFailure {
                            previousDeviceStopFailure =
                                "切换设备前安全停止失败：${it.message ?: "链路不可用"}"
                            failMessage(previousDeviceStopFailure.orEmpty())
                        }
                }
                connectDevice(device, reconnecting = false, intentEpoch = intentEpoch)
                // connectDevice 会发布“已连接”消息；旧鱼未确认停止时必须把风险重新置顶，
                // 不能让新连接成功提示掩盖该故障。
                previousDeviceStopFailure?.let { failure ->
                    failMessage("$failure；新设备已连接，但旧设备仍须等待 STM32 失联保护")
                }
            }
        } catch (cancelled: CancellationException) {
            throw cancelled
        } catch (error: Exception) {
            // 较新的手动连接已经接管时，旧连接失败不得把新会话状态覆盖成 FAILED。
            if (connectionIntentEpoch.get() != intentEpoch) return
            failMessage("连接失败：${error.message ?: "未知错误"}")
            mutableState.update {
                it.copy(
                    connection = it.connection.copy(
                        phase = if (error is UnsupportedOperationException) {
                            RepositoryConnectionPhase.UNSUPPORTED
                        } else {
                            RepositoryConnectionPhase.FAILED
                        },
                        handshakeStatus = HandshakeStatus.FAILED,
                        detail = error.message,
                    ),
                )
            }
        }
    }

    override suspend fun disconnect(reason: String) {
        connectionIntentEpoch.incrementAndGet()
        reconnectJob?.cancel()
        userRequestedDisconnect = true
        connectionMutex.withLock {
            // 先关闭普通控制入口，再等待紧急 STOP/ACK；否则更高 ticket 的新运动可能插入 STOP 后。
            mutableState.update {
                it.copy(
                    connection = it.connection.copy(
                        phase = RepositoryConnectionPhase.DISCONNECTING,
                        detail = "正在安全停止并断开",
                    ),
                )
            }
            if (currentSettings.safeStopOnDisconnect && activeTransport != null) {
                runCatching { sendSafeStopInternal(reason) }
                    .onFailure { failMessage("安全停止发送失败：${it.message ?: "链路不可用"}") }
            }
            teardownSession(reason)
            activeDevice = null
            mutableState.update {
                it.copy(
                    connection = ConnectionInfo(
                        phase = RepositoryConnectionPhase.DISCONNECTED,
                        detail = reason,
                        expectedProtocolVersion = currentSettings.protocolVersionExpected,
                    ),
                )
            }
            log(LogDirection.SYSTEM, "已断开：$reason")
        }
    }

    override suspend fun sendControl(input: ControlInput) {
        // ticket 必须在函数入口取得：这样已开始、但仍在校验/排队的旧请求也能被后来的停止截断。
        val ticket = requestTickets.incrementAndGet()
        input.validated()
        if (input.move == Move.REVERSE && !currentSettings.allowReverseCommand) {
            failMessage("当前固件默认仅允许单方向步进，后退命令已在 APK 侧拒绝")
            return
        }
        if (userRequestedDisconnect ||
            mutableState.value.connection.phase != RepositoryConnectionPhase.CONNECTED
        ) {
            failMessage("尚未完成连接握手，不能发送控制命令")
            return
        }
        val command = input.toCommand(nextSequence())
        queueAndAwait(
            command = command,
            label = "控制",
            ticket = ticket,
            urgent = false,
            survivesSafetyCutoff = false,
        )
        // 只有 STM32 用 ACK/DUP 明确接受后才成为周期保活目标；ERR/超时不能被重复发送。
        publishDesired(DesiredCommand(ticket, command, isSafetyStop = false))
    }

    override suspend fun sendSafeStop(reason: String) {
        if (activeTransport == null) {
            failMessage("$reason：链路不可用，安全停止未送达")
            return
        }
        sendSafeStopInternal(reason)
    }

    override suspend fun exportLogs(): String = buildString {
        appendLine("timestamp_epoch_ms\tdirection\tmessage")
        mutableState.value.logs.forEach { entry ->
            append(entry.timestampEpochMillis)
            append('\t')
            append(entry.direction.name)
            append('\t')
            appendLine(entry.text.replace('\t', ' ').replace('\n', ' '))
        }
    }

    override suspend fun updateSettings(settings: AppSettings) {
        val value = settings.validated()
        settingsStore.update(value)
        currentSettings = value
        telemetryTracker.updateStaleThreshold(value.telemetryStaleMillis)
        mutableState.update {
            it.copy(settings = value, reverseCommandAllowed = value.allowReverseCommand)
        }
    }

    override fun dismissMessage(messageId: Long) {
        mutableState.update {
            if (it.latestMessage?.id == messageId) it.copy(latestMessage = null) else it
        }
    }

    override fun clearLogs() {
        mutableState.update { it.copy(logs = emptyList()) }
    }

    private suspend fun connectDevice(
        device: DiscoveredDevice,
        reconnecting: Boolean,
        intentEpoch: Long,
    ) {
        check(connectionIntentEpoch.get() == intentEpoch) { "连接意图已更新，本次连接已取消" }
        teardownSession(if (reconnecting) "准备重连" else "准备连接")
        activeDevice = device
        mutableState.update {
            it.copy(
                connection = ConnectionInfo(
                    phase = if (reconnecting) RepositoryConnectionPhase.RECONNECTING else RepositoryConnectionPhase.CONNECTING,
                    detail = if (reconnecting) "正在重连" else "正在连接",
                    selectedDeviceId = device.id,
                    connectedDevice = null,
                    expectedProtocolVersion = currentSettings.protocolVersionExpected,
                ),
            )
        }

        val transport = transportFactory.create(device.transportKind)
        activeTransport = transport
        startSession(transport, intentEpoch)
        try {
            transport.connect(device.endpoint, currentSettings.connectTimeoutMillis)
            check(connectionIntentEpoch.get() == intentEpoch) { "连接意图已更新，本次连接已取消" }
            mutableState.update {
                it.copy(
                    connection = it.connection.copy(
                        phase = RepositoryConnectionPhase.HANDSHAKING,
                        handshakeStatus = HandshakeStatus.SAFE_STOP_PROBE,
                        detail = "正在用安全停止帧验证 V1 兼容性",
                    ),
                )
            }
            performV1Handshake()
            mutableState.update {
                it.copy(
                    connection = it.connection.copy(
                        phase = RepositoryConnectionPhase.CONNECTED,
                        connectedDevice = device,
                        handshakeStatus = HandshakeStatus.VERIFIED_V1_COMPATIBLE,
                        detail = "ACK + STA 验证通过；设备类型和版本未由固件显式上报",
                    ),
                )
            }
            persistSelectedDevice(device)
            startPeriodicControl()
            infoMessage("已连接 ${device.name}", MessageSeverity.SUCCESS)
            log(LogDirection.SYSTEM, "V1 兼容握手通过：安全停止 ACK + STA")
        } catch (error: Exception) {
            handshakeProbe?.completion?.cancel()
            handshakeProbe = null
            teardownSession("连接/握手失败")
            throw error
        }
    }

    private fun startSession(transport: Transport, sessionEpoch: Long) {
        decoder.reset()
        activeSessionEpoch = sessionEpoch
        lastInboundNanos = monotonicNanos()
        // 上一条物理链路的未完成屏障不能阻塞新会话；其普通队列已在 teardown 中销毁。
        completedSafetyTicket.set(requiredSafetyTicket.get())
        val urgentChannel = Channel<Outbound>(capacity = 16)
        val normalChannel = Channel<Outbound>(capacity = 64)
        urgentOutgoing = urgentChannel
        normalOutgoing = normalChannel

        sessionJobs += scope.launch(start = CoroutineStart.UNDISPATCHED) {
            transport.incomingBytes.collect { bytes -> handleIncoming(bytes, sessionEpoch) }
        }
        sessionJobs += scope.launch(start = CoroutineStart.UNDISPATCHED) {
            transport.failures.collect { failure ->
                log(LogDirection.SYSTEM, "${failure.operation} 失败：${failure.message}")
                if (failure.recoverable && mutableState.value.connection.phase == RepositoryConnectionPhase.CONNECTED) {
                    onLinkLost(failure.message, sessionEpoch)
                }
            }
        }
        sessionJobs += scope.launch(start = CoroutineStart.UNDISPATCHED) {
            var deferredNormal: Outbound? = null
            while (isActive) {
                val outbound = if (completedSafetyTicket.get() < requiredSafetyTicket.get()) {
                    urgentChannel.receive()
                } else {
                    deferredNormal?.also { deferredNormal = null }
                        ?: urgentChannel.tryReceive().getOrNull()
                        ?: select {
                            urgentChannel.onReceive { it }
                            normalChannel.onReceive { it }
                        }
                }

                // 若停止恰好在 select 选中普通帧后到达，先暂存该帧并等待紧急停止。
                if (outbound.safetyTicket == null &&
                    completedSafetyTicket.get() < requiredSafetyTicket.get()
                ) {
                    deferredNormal = outbound
                    continue
                }

                if (!outbound.survivesSafetyCutoff && outbound.commandTicket <= safetyCutoffTicket.get()) {
                    outbound.completion?.completeExceptionally(SupersededBySafetyStopException())
                    log(LogDirection.SYSTEM, "${outbound.label}帧已被安全停止截断（ticket=${outbound.commandTicket}）")
                    continue
                }
                // CONNECTED 检查与真正写入之间可能失联；最终边界必须再次拦截普通帧。
                // 握手帧和显式安全停止均不依赖 CONNECTED，因此不受此条件影响。
                if (outbound.requiresConnectedPhase &&
                    mutableState.value.connection.phase != RepositoryConnectionPhase.CONNECTED
                ) {
                    outbound.completion?.completeExceptionally(ConnectionLostBeforeSendException())
                    log(LogDirection.SYSTEM, "${outbound.label}帧因连接失联未发送（ticket=${outbound.commandTicket}）")
                    continue
                }
                try {
                    pendingSendNanos[outbound.sequence] = monotonicNanos()
                    // MockTransport 和部分串口封装会在 send() 内同步回送 ACK，故 waiter 必须先注册。
                    outbound.acknowledgement?.let { waiter ->
                        check(acknowledgementWaiters.putIfAbsent(outbound.sequence, waiter) == null) {
                            "同序号 ACK 等待器已存在：${outbound.sequence}"
                        }
                    }
                    transport.send(outbound.bytes)
                    mutableState.update { it.copy(sentFrameCount = it.sentFrameCount + 1L) }
                    log(LogDirection.TX, String(outbound.bytes, StandardCharsets.US_ASCII).trimEnd())
                    outbound.safetyTicket?.let { ticket ->
                        completedSafetyTicket.accumulateAndGet(ticket) { current, candidate ->
                            maxOf(current, candidate)
                        }
                    }
                    outbound.completion?.complete(Unit)
                } catch (error: Exception) {
                    outbound.acknowledgement?.let { waiter ->
                        acknowledgementWaiters.remove(outbound.sequence, waiter)
                        waiter.completeExceptionally(error)
                    }
                    outbound.completion?.completeExceptionally(error)
                    failMessage("${outbound.label}帧发送失败：${error.message ?: "链路不可用"}")
                }
            }
        }
        sessionJobs += scope.launch {
            while (isActive) {
                delay(50L)
                decoder.onTime(clock.nowMillis(), FRAME_ASSEMBLY_TIMEOUT_MILLIS)?.let(::handleMalformed)
                val phase = mutableState.value.connection.phase
                if (phase == RepositoryConnectionPhase.CONNECTED) {
                    val silentMillis = (monotonicNanos() - lastInboundNanos) / 1_000_000L
                    if (silentMillis > currentSettings.linkTimeoutMillis) {
                        onLinkLost("超过 ${currentSettings.linkTimeoutMillis} ms 未收到有效回包", sessionEpoch)
                    }
                }
            }
        }
    }

    private suspend fun performV1Handshake() {
        val ticket = requestTickets.incrementAndGet()
        val command = ControlInput.SAFE_STOP.toCommand(nextSequence())
        publishDesired(DesiredCommand(ticket, command, isSafetyStop = true))
        val probe = HandshakeProbe(command.sequence)
        handshakeProbe = probe
        queueAndAwait(
            command = command,
            label = "握手",
            ticket = ticket,
            urgent = true,
            survivesSafetyCutoff = true,
        )
        try {
            val verified = kotlinx.coroutines.withTimeoutOrNull(currentSettings.handshakeTimeoutMillis) {
                probe.completion.await()
                true
            } ?: false
            if (!verified) {
                throw IllegalStateException(
                    "握手超时：未在 ${currentSettings.handshakeTimeoutMillis} ms 内同时收到 ACK 与有效 STA",
                )
            }
        } finally {
            handshakeProbe = null
        }
    }

    private fun startPeriodicControl() {
        periodicJob?.cancel()
        periodicJob = scope.launch {
            while (isActive && mutableState.value.connection.phase == RepositoryConnectionPhase.CONNECTED) {
                val desired = desiredCommand.get()
                if (desired == null) {
                    delay(50L)
                    continue
                }
                val command = desired.command
                val period = if (command.move == Move.STOP) {
                    currentSettings.heartbeatPeriodMillis
                } else {
                    currentSettings.controlSendPeriodMillis
                }
                delay(period)
                if (!isActive ||
                    mutableState.value.connection.phase != RepositoryConnectionPhase.CONNECTED
                ) {
                    break
                }
                // delay 期间可能发生安全停止或新控制，不能继续排队旧快照。
                val currentDesired = desiredCommand.get() ?: continue
                val currentCommand = currentDesired.command
                val sendResult = normalOutgoing?.trySend(
                    Outbound(
                        bytes = AsciiProtocol.encodeControl(currentCommand),
                        sequence = currentCommand.sequence,
                        label = "周期保活",
                        commandTicket = currentDesired.ticket,
                        survivesSafetyCutoff = currentDesired.isSafetyStop,
                        requiresConnectedPhase = true,
                    ),
                )
                if (sendResult == null || sendResult.isFailure) {
                    failMessage("发送队列已满，周期控制帧被丢弃")
                }
            }
        }
    }

    private suspend fun sendSafeStopInternal(reason: String) {
        val urgentChannel = urgentOutgoing ?: throw IllegalStateException("紧急发送队列不可用")
        val ticket = requestTickets.incrementAndGet()
        // 先发布截断线/屏障，再构造和入队；普通发送器此后只能等待该停止帧。
        safetyCutoffTicket.accumulateAndGet(ticket) { current, candidate -> maxOf(current, candidate) }
        requiredSafetyTicket.accumulateAndGet(ticket) { current, candidate -> maxOf(current, candidate) }
        val previous = desiredCommand.get()?.command
        val stopInput = if (currentSettings.centerServoOnSafeStop || previous == null) {
            ControlInput.SAFE_STOP.copy(stepSpeed = previous?.stepSpeed ?: ControlInput.SAFE_STOP.stepSpeed)
        } else {
            // previous 本身已经通过 ControlCommand 构造校验，保留其 turn/servo 仍满足语义约束。
            ControlInput(
                move = Move.STOP,
                turn = previous.turn,
                stepSpeed = previous.stepSpeed,
                servoDegrees = previous.servoDegrees,
            )
        }
        val command = stopInput.validated().toCommand(nextSequence())
        publishDesired(DesiredCommand(ticket, command, isSafetyStop = true))
        // 在本地停止意图/截断线建立后立即通知观察者；不能等待链路 ACK，
        // 否则发送失败时 UI 仍可能保留 FORWARD 并在回前台后误启动。
        mutableState.update { state ->
            state.copy(safetyStopGeneration = state.safetyStopGeneration + 1L)
        }
        queueAndAwait(
            command = command,
            label = reason,
            ticket = ticket,
            urgent = true,
            survivesSafetyCutoff = true,
            safetyTicket = ticket,
            channelOverride = urgentChannel,
        )
        infoMessage("已发送安全停止：$reason", MessageSeverity.INFO)
    }

    private suspend fun queueAndAwait(
        command: ControlCommand,
        label: String,
        ticket: Long,
        urgent: Boolean,
        survivesSafetyCutoff: Boolean,
        safetyTicket: Long? = null,
        channelOverride: Channel<Outbound>? = null,
    ) {
        val channel = (channelOverride ?: if (urgent) urgentOutgoing else normalOutgoing)
            ?: throw IllegalStateException("发送队列不可用")
        val completion = CompletableDeferred<Unit>()
        val acknowledgement = CompletableDeferred<Unit>()
        channel.send(
            Outbound(
                bytes = AsciiProtocol.encodeControl(command),
                sequence = command.sequence,
                label = label,
                commandTicket = ticket,
                survivesSafetyCutoff = survivesSafetyCutoff,
                requiresConnectedPhase = !urgent,
                safetyTicket = safetyTicket,
                completion = completion,
                acknowledgement = acknowledgement,
            ),
        )
        withTimeout(currentSettings.connectTimeoutMillis) { completion.await() }
        val acknowledged = kotlinx.coroutines.withTimeoutOrNull(currentSettings.commandAckTimeoutMillis) {
            acknowledgement.await()
            true
        } ?: false
        if (!acknowledged) {
            acknowledgementWaiters.remove(command.sequence, acknowledgement)
            val error = CommandAcknowledgementTimeoutException(
                command.sequence,
                currentSettings.commandAckTimeoutMillis,
            )
            failMessage(error.message.orEmpty())
            if (mutableState.value.connection.phase == RepositoryConnectionPhase.CONNECTED) {
                onLinkLost(error.message.orEmpty(), activeSessionEpoch)
            }
            throw error
        }
    }

    private fun handleIncoming(bytes: ByteArray, sessionEpoch: Long) {
        if (sessionEpoch != activeSessionEpoch) return
        val now = clock.nowMillis()
        decoder.feed(bytes, now).forEach { result ->
            when (result) {
                is DecodeResult.Frame -> handleFrame(result.value)
                is DecodeResult.Malformed -> handleMalformed(result)
            }
        }
    }

    private fun handleFrame(frame: ProtocolFrame) {
        mutableState.update { it.copy(receivedFrameCount = it.receivedFrameCount + 1L) }
        log(LogDirection.RX, frame.toString())
        when (frame) {
            is ProtocolFrame.Status -> {
                lastInboundNanos = monotonicNanos()
                telemetryTracker.accept(frame)
                handshakeProbe?.takeIf { it.sequence == frame.sequence && frame.linkAlive }?.let {
                    it.statusSeen = true
                    it.completeIfReady()
                }
            }
            is ProtocolFrame.Acknowledgement -> {
                lastInboundNanos = monotonicNanos()
                acknowledgementWaiters.remove(frame.sequence)?.complete(Unit)
                pendingSendNanos.remove(frame.sequence)?.let { sent ->
                    val latency = ((monotonicNanos() - sent) / 1_000_000L).coerceAtLeast(0L)
                    mutableState.update { it.copy(protocolLatencyMillis = latency) }
                }
                handshakeProbe?.takeIf { it.sequence == frame.sequence }?.let {
                    it.ackSeen = true
                    it.completeIfReady()
                }
            }
            is ProtocolFrame.Error -> {
                lastInboundNanos = monotonicNanos()
                val message = "STM32 拒绝命令：${frame.code}（seq=${frame.sequence ?: "NA"}）"
                failMessage(message)
                frame.sequence?.let { sequence ->
                    acknowledgementWaiters.remove(sequence)
                        ?.completeExceptionally(IllegalStateException(message))
                }
                handshakeProbe?.takeIf { it.sequence == frame.sequence || frame.sequence == null }?.completion
                    ?.completeExceptionally(IllegalStateException(message))
            }
            is ProtocolFrame.Unknown -> log(LogDirection.SYSTEM, "忽略未知扩展帧 ${frame.type}")
        }
    }

    private fun handleMalformed(result: DecodeResult.Malformed) {
        log(LogDirection.SYSTEM, "非法接收帧：${result.reason}${result.rawPayload?.let { " [$it]" } ?: ""}")
    }

    private fun onLinkLost(reason: String, sessionEpoch: Long) {
        val phase = mutableState.value.connection.phase
        if (
            userRequestedDisconnect ||
            sessionEpoch == 0L ||
            sessionEpoch != activeSessionEpoch ||
            sessionEpoch != connectionIntentEpoch.get() ||
            phase == RepositoryConnectionPhase.LOST ||
            phase == RepositoryConnectionPhase.RECONNECTING ||
            phase == RepositoryConnectionPhase.FAILED
        ) {
            return
        }
        mutableState.update {
            it.copy(connection = it.connection.copy(phase = RepositoryConnectionPhase.LOST, detail = reason))
        }
        failMessage("连接失联：$reason")
        scheduleReconnect(sessionEpoch)
    }

    private fun scheduleReconnect(intentEpoch: Long) {
        if (!currentSettings.reconnectEnabled || currentSettings.reconnectMaxAttempts == 0) return
        if (reconnectJob?.isActive == true) return
        val device = activeDevice ?: return
        reconnectJob = scope.launch {
            repeat(currentSettings.reconnectMaxAttempts) { attempt ->
                delay(currentSettings.reconnectDelayMillis)
                if (userRequestedDisconnect || connectionIntentEpoch.get() != intentEpoch) return@launch
                val success = runCatching {
                    connectionMutex.withLock {
                        check(connectionIntentEpoch.get() == intentEpoch) { "重连意图已过期" }
                        connectDevice(device, reconnecting = true, intentEpoch = intentEpoch)
                    }
                }.isSuccess
                if (success) return@launch
                if (connectionIntentEpoch.get() != intentEpoch) return@launch
                log(LogDirection.SYSTEM, "第 ${attempt + 1} 次重连失败")
            }
            if (userRequestedDisconnect || connectionIntentEpoch.get() != intentEpoch) return@launch
            mutableState.update {
                it.copy(connection = it.connection.copy(phase = RepositoryConnectionPhase.FAILED, detail = "自动重连次数已用尽"))
            }
            failMessage("自动重连失败，请检查网络并手动重试")
        }
    }

    private suspend fun teardownSession(reason: String) {
        periodicJob?.cancel()
        periodicJob = null
        val urgentChannel = urgentOutgoing
        val normalChannel = normalOutgoing
        urgentOutgoing = null
        normalOutgoing = null
        val jobs = sessionJobs.toList()
        sessionJobs.clear()
        // 先取消 actor，再取消通道；反序 close 会让挂在 receive() 的 actor 抛出未处理
        // ClosedReceiveChannelException，并把正常的会话销毁误报为应用故障。
        jobs.forEach { it.cancel() }
        urgentChannel?.cancel()
        normalChannel?.cancel()
        runCatching { activeTransport?.disconnect(reason) }
        jobs.forEach { it.join() }
        activeTransport = null
        activeSessionEpoch = 0L
        pendingSendNanos.clear()
        acknowledgementWaiters.values.forEach { waiter ->
            waiter.completeExceptionally(ConnectionLostBeforeSendException())
        }
        acknowledgementWaiters.clear()
        decoder.reset()
    }

    private suspend fun persistSelectedDevice(device: DiscoveredDevice) {
        val endpointPort = device.endpoint.port ?: 0
        val updated = currentSettings.copy(
            wifiHost = if (device.transportKind in setOf(TransportKind.TCP, TransportKind.UDP)) {
                device.endpoint.address
            } else {
                currentSettings.wifiHost
            },
            wifiPort = if (device.transportKind in setOf(TransportKind.TCP, TransportKind.UDP)) {
                endpointPort
            } else {
                currentSettings.wifiPort
            },
            lastDeviceName = device.name,
            lastDeviceAddress = device.endpoint.address,
            lastDevicePort = endpointPort,
        )
        settingsStore.update(updated)
        currentSettings = updated
        mutableState.update { state ->
            state.copy(
                settings = updated,
                devices = state.devices.map { it.copy(isSaved = it.id == device.id) },
            )
        }
    }

    private fun nextSequence(): Int = sequenceCounter.updateAndGet { (it + 1) and 0xFFFF }

    private fun publishDesired(candidate: DesiredCommand) {
        while (true) {
            val current = desiredCommand.get()
            if (current != null && current.ticket > candidate.ticket) return
            if (desiredCommand.compareAndSet(current, candidate)) return
        }
    }

    private fun ControlInput.toCommand(sequence: Int): ControlCommand = ControlCommand(
        sequence = sequence,
        move = move,
        turn = turn,
        stepSpeed = stepSpeed,
        servoDegrees = servoDegrees,
    )

    private fun log(direction: LogDirection, text: String) {
        val entry = CommunicationLogEntry(clock.nowMillis(), direction, text)
        mutableState.update { state ->
            state.copy(logs = (state.logs + entry).takeLast(MAX_LOG_ENTRIES))
        }
    }

    private fun infoMessage(text: String, severity: MessageSeverity) {
        mutableState.update {
            it.copy(latestMessage = RepositoryMessage(messageIds.incrementAndGet(), severity, text))
        }
    }

    private fun failMessage(text: String) = infoMessage(text, MessageSeverity.ERROR)

    private companion object {
        const val MAX_LOG_ENTRIES = 500
        const val FRAME_ASSEMBLY_TIMEOUT_MILLIS = 250L
        val ACTIVE_SESSION_PHASES = setOf(
            RepositoryConnectionPhase.CONNECTING,
            RepositoryConnectionPhase.HANDSHAKING,
            RepositoryConnectionPhase.CONNECTED,
            RepositoryConnectionPhase.DISCONNECTING,
            RepositoryConnectionPhase.LOST,
            RepositoryConnectionPhase.RECONNECTING,
        )
    }
}
