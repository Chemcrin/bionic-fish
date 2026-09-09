package com.bionicfish.controller.device

import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.protocol.StepSpeed
import com.bionicfish.controller.protocol.Turn
import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.settings.InMemorySettingsStore
import com.bionicfish.controller.transport.BluetoothCapabilities
import com.bionicfish.controller.transport.BluetoothCapabilityProvider
import com.bionicfish.controller.transport.DiscoverySource
import com.bionicfish.controller.transport.MockTransport
import com.bionicfish.controller.transport.ScanRequest
import com.bionicfish.controller.transport.Transport
import com.bionicfish.controller.transport.TransportCandidate
import com.bionicfish.controller.transport.TransportConnectionState
import com.bionicfish.controller.transport.TransportEndpoint
import com.bionicfish.controller.transport.TransportFailure
import com.bionicfish.controller.transport.TransportFactory
import com.bionicfish.controller.transport.TransportKind
import java.nio.charset.StandardCharsets
import java.util.concurrent.atomic.AtomicLong
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharedFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asSharedFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.advanceTimeBy
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

@OptIn(ExperimentalCoroutinesApi::class)
class DefaultBionicFishRepositoryTest {
    @Test
    fun `legacy UDP settings cannot start an unsupported hardware scan`() = runTest {
        var transportsCreated = 0
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.UDP)),
            transportFactory = TransportFactory {
                transportsCreated += 1
                error("Unsupported UDP must be rejected before creating a transport")
            },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        assertEquals(0, transportsCreated)
        assertTrue(repository.state.value.latestMessage?.text?.contains("UDP 已停用") == true)
    }

    @Test
    fun `UDP candidate cannot create a connection transport or send a handshake`() = runTest {
        // A discovery result can carry its own transport kind; validate the selected
        // candidate even when the current settings themselves are an allowed mode.
        val discoveryTransport = PriorityProbeTransport(kind = TransportKind.UDP)
        val createdKinds = mutableListOf<TransportKind>()
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.MOCK)),
            transportFactory = TransportFactory { kind ->
                createdKinds += kind
                check(kind == TransportKind.MOCK) { "UDP must be rejected before transport creation" }
                discoveryTransport
            },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        val candidate = repository.state.value.devices.single()
        assertEquals(TransportKind.UDP, candidate.transportKind)
        assertEquals(listOf(TransportKind.MOCK), createdKinds)

        repository.connect(candidate.id)
        runCurrent()

        assertEquals(listOf(TransportKind.MOCK), createdKinds)
        assertTrue(discoveryTransport.connectedEndpoints.isEmpty())
        assertTrue(discoveryTransport.sentFrames.isEmpty())
        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.NOT_STARTED, repository.state.value.connection.handshakeStatus)
        assertTrue(repository.state.value.latestMessage?.text?.contains("UDP 已停用") == true)
    }

    @Test
    fun `mock scan handshake control telemetry and safe stop share one protocol path`() = runTest {
        val dispatcher = UnconfinedTestDispatcher(testScheduler)
        val settingsStore = InMemorySettingsStore(
            AppSettings(
                transportKind = TransportKind.MOCK,
                connectTimeoutMillis = 1_000L,
                handshakeTimeoutMillis = 1_000L,
                controlSendPeriodMillis = 200L,
                heartbeatPeriodMillis = 500L,
                linkTimeoutMillis = 1_000L,
            ),
        )
        val repository = DefaultBionicFishRepository(
            settingsStore = settingsStore,
            transportFactory = TransportFactory { MockTransport(0L, dispatcher) },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()

        repository.scan()
        val device = repository.state.value.devices.single()
        repository.connect(device.id)
        runCurrent()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, repository.state.value.connection.handshakeStatus)
        assertNotNull(repository.state.value.telemetry.snapshot)
        assertEquals(false, repository.state.value.telemetry.snapshot?.stepCommandedOn)

        repository.sendControl(
            ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, servoDegrees = -30),
        )
        runCurrent()
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertEquals(true, repository.state.value.telemetry.snapshot?.stepCommandedOn)
        assertEquals(-30, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertFalse(repository.exportLogs().contains("n20", ignoreCase = true))

        repository.updateSettings(repository.state.value.settings.copy(centerServoOnSafeStop = false))
        repository.sendSafeStop("单元测试")
        runCurrent()
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertEquals(false, repository.state.value.telemetry.snapshot?.stepCommandedOn)
        assertEquals(-30, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertTrue(repository.exportLogs().contains("move=S,turn=L,step_speed=100,servo=-30"))

        repository.updateSettings(repository.state.value.settings.copy(centerServoOnSafeStop = true))
        repository.sendSafeStop("回中测试")
        runCurrent()
        assertEquals(0, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertTrue(repository.exportLogs().contains("move=S,turn=C,step_speed=100,servo=0"))
        repository.disconnect("单元测试结束")
        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
    }

    @Test
    fun `reverse stays blocked until explicitly confirmed in settings`() = runTest {
        val dispatcher = UnconfinedTestDispatcher(testScheduler)
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.MOCK)),
            transportFactory = TransportFactory { MockTransport(0L, dispatcher) },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val sentBefore = repository.state.value.sentFrameCount

        repository.sendControl(ControlInput(Move.REVERSE, Turn.CENTER, StepSpeed.SLOW, 0))

        assertEquals(sentBefore, repository.state.value.sentFrameCount)
        assertTrue(repository.state.value.latestMessage?.text.orEmpty().contains("后退命令"))
        repository.disconnect("test")
    }

    @Test
    fun `safety stop bypasses queued controls and cuts off every older request`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.MOCK)),
            transportFactory = TransportFactory { transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)

        transport.blockNextMovement = true
        val inFlight = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))
        }
        transport.blockedWriteStarted.await()
        val staleQueued = async(start = CoroutineStart.UNDISPATCHED) {
            runCatching {
                repository.sendControl(ControlInput(Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 30))
            }
        }
        val safetyStop = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendSafeStop("优先级测试")
        }
        val postStop = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -30))
        }

        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        assertTrue(staleQueued.await().isFailure)
        safetyStop.await()
        postStop.await()

        val commandsAfterHandshake = transport.sentFrames.drop(1)
        assertEquals(3, commandsAfterHandshake.size)
        assertTrue(commandsAfterHandshake[0].contains("move=F,turn=C,step_speed=60,servo=0"))
        // 尚未获 ACK 的 100 RPM 请求不能污染已确认状态；停止沿用最后确认的 60 RPM 配置。
        assertTrue(commandsAfterHandshake[1].contains("move=S,turn=C,step_speed=60,servo=0"))
        assertTrue(commandsAfterHandshake[2].contains("move=F,turn=L,step_speed=100,servo=-30"))
        assertFalse(commandsAfterHandshake.any { it.contains("move=F,turn=R") })
        repository.disconnect("test")
    }

    @Test
    fun `failed safety write cannot deadlock handshake on a new session`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(
                AppSettings(transportKind = TransportKind.MOCK, safeStopOnDisconnect = false),
            ),
            transportFactory = TransportFactory { transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        val deviceId = repository.state.value.devices.single().id
        repository.connect(deviceId)

        transport.failNextSafetyStop = true
        val generationBefore = repository.state.value.safetyStopGeneration
        assertTrue(runCatching { repository.sendSafeStop("故障注入") }.isFailure)
        assertEquals(generationBefore + 1L, repository.state.value.safetyStopGeneration)
        repository.disconnect("重建会话")
        repository.connect(deviceId)

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, repository.state.value.connection.handshakeStatus)
        repository.disconnect("test")
    }

    @Test
    fun `connection loss drops queued and periodic movement before transport write`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.MOCK)),
            transportFactory = TransportFactory { transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)

        transport.blockNextMovement = true
        val inFlight = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))
        }
        transport.blockedWriteStarted.await()
        val queuedBeforeLoss = async(start = CoroutineStart.UNDISPATCHED) {
            runCatching {
                repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -30))
            }
        }

        transport.reportRecoverableFailure("injected link loss")
        // SharedFlow 的 collector 运行在测试调度器上；先排空当前时刻任务再验证状态门禁。
        runCurrent()
        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)
        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        assertTrue(queuedBeforeLoss.await().isFailure)

        val movementWritesAfterLoss = transport.sentFrames.count { it.contains("move=F") }
        assertEquals(1, movementWritesAfterLoss) // 仅允许失联前已经进入 transport.send 的那一帧。
        advanceTimeBy(600L)
        runCurrent()
        assertEquals(
            movementWritesAfterLoss,
            transport.sentFrames.count { it.contains("move=F") },
        )
        repository.disconnect("test")
    }

    @Test
    fun `connecting another device urgently stops old fish before teardown`() = runTest {
        val transport = PriorityProbeTransport(candidateCount = 2)
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val devices = repository.state.value.devices
        repository.connect(devices[0].id)
        repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))

        repository.connect(devices[1].id)

        val oldConnect = transport.events.indexOf("connect:mock://priority-1")
        val oldDisconnect = transport.events.withIndex().first {
            it.index > oldConnect && it.value == "disconnect:准备连接"
        }.index
        val switchStop = transport.events.withIndex().first {
            it.index > oldConnect &&
                it.index < oldDisconnect &&
                it.value.startsWith("send:") &&
                it.value.contains("move=S")
        }.index
        val newConnect = transport.events.indexOf("connect:mock://priority-2")
        assertTrue(oldConnect >= 0)
        assertTrue("旧设备必须先收到停止", switchStop in (oldConnect + 1) until oldDisconnect)
        assertTrue("停止后才可销毁旧链路并连接新设备", oldDisconnect < newConnect)
        assertEquals(devices[1].id, repository.state.value.connection.connectedDevice?.id)
        repository.disconnect("test")
    }

    @Test
    fun `manual connect cancels stale reconnect intent without tearing down new session`() = runTest {
        val transport = PriorityProbeTransport(candidateCount = 2)
        val repository = repositoryFor(
            transport,
            AppSettings(
                transportKind = TransportKind.MOCK,
                reconnectDelayMillis = 200L,
                reconnectMaxAttempts = 2,
            ),
        )
        runCurrent()
        repository.scan()
        val devices = repository.state.value.devices
        repository.connect(devices[0].id)
        transport.reportRecoverableFailure("旧会话失联")
        runCurrent()
        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)

        repository.connect(devices[1].id)
        advanceTimeBy(1_000L)
        runCurrent()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(devices[1].id, repository.state.value.connection.connectedDevice?.id)
        assertEquals(
            listOf("mock://priority-1", "mock://priority-2"),
            transport.events.filter { it.startsWith("connect:") }.map { it.removePrefix("connect:") },
        )
        repository.disconnect("test")
    }

    @Test
    fun `explicit command accepts duplicate acknowledgement`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        transport.responseMode = ResponseMode.DUPLICATE_ACK

        repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        repository.disconnect("test")
    }

    @Test
    fun `handshake still requires status in addition to acknowledgement`() = runTest {
        val transport = PriorityProbeTransport().apply { responseMode = ResponseMode.ACK_ONLY }
        val repository = repositoryFor(
            transport,
            AppSettings(
                transportKind = TransportKind.MOCK,
                handshakeTimeoutMillis = 100L,
            ),
        )
        runCurrent()
        repository.scan()

        repository.connect(repository.state.value.devices.single().id)

        assertEquals(RepositoryConnectionPhase.FAILED, repository.state.value.connection.phase)
        assertTrue(repository.state.value.connection.detail.orEmpty().contains("握手超时"))
    }

    @Test
    fun `same sequence error fails explicit command`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        transport.responseMode = ResponseMode.ERROR

        val result = runCatching {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))
        }

        assertTrue(result.isFailure)
        assertTrue(result.exceptionOrNull()?.message.orEmpty().contains("E_TEST"))
        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        val movementWrites = transport.sentFrames.count { it.contains("move=F") }
        advanceTimeBy(250L)
        runCurrent()
        assertEquals("被 ERR 拒绝的命令不能成为周期保活目标", movementWrites,
            transport.sentFrames.count { it.contains("move=F") })
        repository.disconnect("test")
    }

    @Test
    fun `explicit command ACK timeout reports loss and starts safety policy`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(
            transport,
            AppSettings(
                transportKind = TransportKind.MOCK,
                commandAckTimeoutMillis = 100L,
                reconnectEnabled = false,
            ),
        )
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        transport.responseMode = ResponseMode.NO_RESPONSE
        val result = async { runCatching {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))
        } }
        runCurrent()

        advanceTimeBy(101L)
        runCurrent()

        assertTrue(result.await().isFailure)
        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)
        assertTrue(repository.state.value.latestMessage?.text.orEmpty().contains("ACK 超时"))
        repository.disconnect("test")
    }

    @Test
    fun `scan is rejected while a session is active`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val scanCount = transport.scanCount

        repository.scan()

        assertEquals(scanCount, transport.scanCount)
        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertTrue(repository.state.value.latestMessage?.text.orEmpty().contains("先安全断开"))
        repository.disconnect("test")
    }

    @Test
    fun `control requested during disconnect cannot run after safety stop`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        repository.sendControl(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0))
        val movementWrites = transport.sentFrames.count { it.contains("move=F") }
        transport.blockNextSafetyStop = true
        val disconnect = async(start = CoroutineStart.UNDISPATCHED) {
            repository.disconnect("并发断开测试")
        }
        transport.safetyWriteStarted.await()

        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -30))
        transport.releaseSafetyWrite.complete(Unit)
        disconnect.await()

        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
        assertEquals(movementWrites, transport.sentFrames.count { it.contains("move=F") })
    }

    @Test
    fun `unknown frame does not refresh supported protocol watchdog`() = runTest {
        val transport = PriorityProbeTransport()
        val monotonicNanos = AtomicLong(0L)
        val repository = repositoryFor(transport, monotonicNanos = monotonicNanos::get)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        monotonicNanos.set(900_000_000L)
        transport.emitRaw("<EXT,foo=bar>\n")
        runCurrent()

        monotonicNanos.set(1_100_000_000L)
        advanceTimeBy(51L)
        runCurrent()

        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)
        repository.disconnect("test")
    }

    @Test
    fun `AP direct connects without scanning and saves route without replacing LAN settings`() = runTest {
        val transport = PriorityProbeTransport(kind = TransportKind.TCP)
        val createdKinds = mutableListOf<TransportKind>()
        val settingsStore = InMemorySettingsStore(
            AppSettings(
                transportKind = TransportKind.MOCK,
                wifiHost = "192.168.1.20",
                wifiPort = 8000,
            ),
        )
        val repository = DefaultBionicFishRepository(
            settingsStore = settingsStore,
            transportFactory = TransportFactory { kind -> createdKinds += kind; transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()

        repository.connectApDirect()
        runCurrent()

        assertEquals(0, transport.scanCount)
        assertEquals(listOf(TransportKind.TCP), createdKinds)
        assertEquals(
            listOf(TransportEndpoint("192.168.4.1", 9000, wifiOnly = true)),
            transport.connectedEndpoints,
        )
        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, repository.state.value.connection.handshakeStatus)
        assertTrue(repository.state.value.connection.isApDirect)
        assertTrue(transport.sentFrames.first().contains("move=S,turn=C,step_speed=60,servo=0"))
        val saved = settingsStore.settings.first()
        assertEquals(TransportKind.TCP, saved.transportKind)
        assertTrue(saved.lastDeviceApDirect)
        assertEquals("192.168.4.1", saved.lastDeviceAddress)
        assertEquals(9000, saved.lastDevicePort)
        assertEquals("192.168.1.20", saved.wifiHost)
        assertEquals(8000, saved.wifiPort)

        repository.sendControl(ControlInput(Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 30))
        assertTrue(transport.sentFrames.last().contains("move=F,turn=R,step_speed=100,servo=30"))
        assertFalse(transport.sentFrames.any { it.contains("n20", ignoreCase = true) })
        repository.disconnect("AP 测试结束")
    }

    @Test
    fun `AP direct uses edited preset and never silently connects a remembered device`() = runTest {
        val transport = PriorityProbeTransport(kind = TransportKind.TCP)
        val repository = repositoryFor(
            transport,
            AppSettings(
                transportKind = TransportKind.TCP,
                apSsid = "Fish-Custom",
                apHost = "192.168.5.1",
                apPort = 9001,
                lastDeviceName = "原热点",
                lastDeviceAddress = "192.168.4.1",
                lastDevicePort = 9000,
                lastDeviceApDirect = true,
            ),
        )
        runCurrent()

        assertTrue(transport.connectedEndpoints.isEmpty())
        assertEquals(0, transport.scanCount)
        repository.connectApDirect()

        assertEquals(TransportEndpoint("192.168.5.1", 9001, wifiOnly = true),
            transport.connectedEndpoints.single())
        assertTrue(repository.state.value.connection.connectedDevice?.name.orEmpty().contains("Fish-Custom"))
        repository.disconnect("test")
    }

    @Test
    fun `AP reconnect keeps Wi-Fi-only endpoint and re-handshakes at safe stop`() = runTest {
        val transport = PriorityProbeTransport(kind = TransportKind.TCP)
        val repository = repositoryFor(
            transport,
            AppSettings(reconnectDelayMillis = 200L, reconnectMaxAttempts = 2),
        )
        runCurrent()
        repository.connectApDirect()
        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -30))
        val framesBeforeLoss = transport.sentFrames.size

        transport.reportRecoverableFailure("AP 热点断开")
        runCurrent()
        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)
        advanceTimeBy(201L)
        runCurrent()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertTrue(repository.state.value.connection.isApDirect)
        assertEquals(2, transport.connectedEndpoints.size)
        assertTrue(transport.connectedEndpoints.all { it == TransportEndpoint("192.168.4.1", 9000, wifiOnly = true) })
        assertEquals(0, transport.scanCount)
        val framesAfterLoss = transport.sentFrames.drop(framesBeforeLoss)
        assertTrue(framesAfterLoss.isNotEmpty())
        assertTrue("重新连接只能从安全停止恢复，不得重放旧运动", framesAfterLoss.all { it.contains("move=S") })
        repository.disconnect("test")
    }

    @Test
    fun `AP TCP connection with only ACK is not declared ready or remembered`() = runTest {
        val transport = PriorityProbeTransport(kind = TransportKind.TCP).apply {
            responseMode = ResponseMode.ACK_ONLY
        }
        val settingsStore = InMemorySettingsStore(AppSettings(handshakeTimeoutMillis = 100L))
        val repository = DefaultBionicFishRepository(
            settingsStore = settingsStore,
            transportFactory = TransportFactory { transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()

        repository.connectApDirect()

        assertEquals(RepositoryConnectionPhase.FAILED, repository.state.value.connection.phase)
        assertTrue(repository.state.value.connection.isApDirect)
        assertTrue(repository.state.value.connection.detail.orEmpty().contains("握手"))
        assertFalse(settingsStore.settings.first().lastDeviceApDirect)
        assertEquals(0, transport.scanCount)
        assertTrue(transport.sentFrames.all { it.contains("move=S") })
    }

    @Test
    fun `AP TCP failure before connection does not falsely report a handshake failure`() = runTest {
        val transport = PriorityProbeTransport(
            kind = TransportKind.TCP,
            connectFailure = IllegalStateException("未找到匹配目标地址的 Wi-Fi，请先连接仿生鱼热点"),
        )
        val settingsStore = InMemorySettingsStore(AppSettings())
        val repository = DefaultBionicFishRepository(
            settingsStore = settingsStore,
            transportFactory = TransportFactory { transport },
            bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
            scope = backgroundScope,
        )
        runCurrent()

        repository.connectApDirect()

        assertEquals(RepositoryConnectionPhase.FAILED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.NOT_STARTED, repository.state.value.connection.handshakeStatus)
        assertTrue(repository.state.value.connection.isApDirect)
        assertTrue(repository.state.value.connection.detail.orEmpty().contains("Wi-Fi"))
        assertFalse(settingsStore.settings.first().lastDeviceApDirect)
        assertTrue(transport.sentFrames.isEmpty())
        assertEquals(0, transport.scanCount)
    }

    private fun kotlinx.coroutines.test.TestScope.repositoryFor(
        transport: PriorityProbeTransport,
        settings: AppSettings = AppSettings(transportKind = TransportKind.MOCK),
        monotonicNanos: () -> Long = { System.nanoTime() },
    ) = DefaultBionicFishRepository(
        settingsStore = InMemorySettingsStore(settings),
        transportFactory = TransportFactory { transport },
        bluetoothCapabilityProvider = BluetoothCapabilityProvider { BluetoothCapabilities(false, false) },
        scope = backgroundScope,
        monotonicNanos = monotonicNanos,
    )

    private enum class ResponseMode { OK, DUPLICATE_ACK, ACK_ONLY, ERROR, NO_RESPONSE }

    private class PriorityProbeTransport(
        private val candidateCount: Int = 1,
        override val kind: TransportKind = TransportKind.MOCK,
        private val connectFailure: Exception? = null,
    ) : Transport {
        private val mutableConnection = MutableStateFlow<TransportConnectionState>(
            TransportConnectionState.Disconnected,
        )
        private val mutableIncoming = MutableSharedFlow<ByteArray>(extraBufferCapacity = 16)
        private val mutableFailures = MutableSharedFlow<TransportFailure>(extraBufferCapacity = 1)
        override val connectionState: StateFlow<TransportConnectionState> = mutableConnection.asStateFlow()
        override val incomingBytes: SharedFlow<ByteArray> = mutableIncoming.asSharedFlow()
        override val failures: SharedFlow<TransportFailure> = mutableFailures.asSharedFlow()
        val sentFrames = mutableListOf<String>()
        val events = mutableListOf<String>()
        val connectedEndpoints = mutableListOf<TransportEndpoint>()
        var scanCount = 0
        var responseMode = ResponseMode.OK
        var blockNextMovement = false
        var blockNextSafetyStop = false
        var failNextSafetyStop = false
        val blockedWriteStarted = CompletableDeferred<Unit>()
        val releaseBlockedWrite = CompletableDeferred<Unit>()
        val safetyWriteStarted = CompletableDeferred<Unit>()
        val releaseSafetyWrite = CompletableDeferred<Unit>()

        override suspend fun scan(request: ScanRequest): List<TransportCandidate> {
            scanCount++
            return (1..candidateCount).map { index ->
                TransportCandidate(
                    name = "优先级测试设备 $index",
                    endpoint = TransportEndpoint("mock://priority-$index"),
                    kind = kind,
                    discoverySource = DiscoverySource.MOCK,
                )
            }
        }

        override fun cancelScan() = Unit

        override suspend fun connect(endpoint: TransportEndpoint, timeoutMillis: Long) {
            events += "connect:${endpoint.address}"
            connectedEndpoints += endpoint
            connectFailure?.let { throw it }
            mutableConnection.value = TransportConnectionState.Connected(endpoint)
        }

        override suspend fun disconnect(reason: String) {
            events += "disconnect:$reason"
            mutableConnection.value = TransportConnectionState.Disconnected
        }

        override suspend fun send(bytes: ByteArray) {
            val text = String(bytes, StandardCharsets.US_ASCII)
            sentFrames += text
            events += "send:${text.trim()}"
            val match = COMMAND.matchEntire(text) ?: error("unexpected frame: $text")
            val sequence = match.groupValues[1].toInt()
            val move = match.groupValues[2]
            val speed = match.groupValues[4].toInt()
            val servo = match.groupValues[5].toInt()
            if (failNextSafetyStop && move == "S") {
                failNextSafetyStop = false
                throw IllegalStateException("injected safety write failure")
            }
            if (blockNextSafetyStop && move == "S") {
                blockNextSafetyStop = false
                safetyWriteStarted.complete(Unit)
                releaseSafetyWrite.await()
            }
            if (blockNextMovement && move == "F") {
                blockNextMovement = false
                blockedWriteStarted.complete(Unit)
                releaseBlockedWrite.await()
            }
            val target = if (move == "S") 0 else speed
            val response = when (responseMode) {
                ResponseMode.OK ->
                    "<ACK,seq=$sequence,result=OK>\n" + status(sequence, target, servo)
                ResponseMode.DUPLICATE_ACK ->
                    "<ACK,seq=$sequence,result=DUP>\n" + status(sequence, target, servo)
                ResponseMode.ACK_ONLY -> "<ACK,seq=$sequence,result=OK>\n"
                ResponseMode.ERROR -> "<ERR,seq=$sequence,code=E_TEST>\n"
                ResponseMode.NO_RESPONSE -> null
            }
            response?.let { mutableIncoming.emit(it.toByteArray(StandardCharsets.US_ASCII)) }
        }

        suspend fun reportRecoverableFailure(message: String) {
            mutableFailures.emit(TransportFailure("receive", message, recoverable = true))
        }

        suspend fun emitRaw(frame: String) {
            mutableIncoming.emit(frame.toByteArray(StandardCharsets.US_ASCII))
        }

        private fun status(sequence: Int, target: Int, servo: Int): String =
            "<STA,seq=$sequence,link=1,step_rpm=$target,step_est=$target," +
                "step_actual=NA,servo=$servo,roll=0.0,pitch=0.0,yaw=0.0,err=0>\n"

        private companion object {
            val COMMAND = Regex(
                "<CMD,seq=(\\d+),move=([FRS]),turn=([LRC]),step_speed=(60|100),servo=(-?\\d+)>\\n",
            )
        }
    }
}
