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
        assertTrue(repository.state.value.stepperParametersConfirmed)

        repository.sendControl(
            ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, servoDegrees = -15, motor2 = Move.REVERSE),
        )
        runCurrent()
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertEquals(true, repository.state.value.telemetry.snapshot?.stepRunning)
        assertEquals(-15, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertEquals(Move.FORWARD, repository.state.value.telemetry.snapshot?.motor1Direction)
        assertEquals(Move.REVERSE, repository.state.value.telemetry.snapshot?.motor2Direction)

        repository.updateSettings(repository.state.value.settings.copy(centerServoOnSafeStop = false))
        repository.sendSafeStop("单元测试")
        runCurrent()
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertEquals(false, repository.state.value.telemetry.snapshot?.stepRunning)
        assertEquals(-15, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertEquals(Move.STOP, repository.state.value.telemetry.snapshot?.motor1Direction)
        assertEquals(Move.STOP, repository.state.value.telemetry.snapshot?.motor2Direction)
        assertTrue(repository.exportLogs().contains("move=S,turn=L,step_speed=100,servo=-15,m2=S"))

        repository.updateSettings(repository.state.value.settings.copy(centerServoOnSafeStop = true))
        repository.sendSafeStop("回中测试")
        runCurrent()
        assertEquals(0, repository.state.value.telemetry.snapshot?.servoDegrees)
        assertTrue(repository.exportLogs().contains("move=S,turn=C,step_speed=100,servo=0,m2=S"))
        repository.disconnect("单元测试结束")
        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
    }

    @Test
    fun `reverse stays blocked until explicitly confirmed in settings`() = runTest {
        val dispatcher = UnconfinedTestDispatcher(testScheduler)
        val repository = DefaultBionicFishRepository(
            settingsStore = InMemorySettingsStore(AppSettings(transportKind = TransportKind.MOCK, allowReverseCommand = false)),
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
    fun `both motor directions are enabled by default and full targets remain independent`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        assertTrue(repository.state.value.reverseCommandAllowed)

        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE))
        assertTrue(transport.sentFrames.last().contains("move=F,turn=L,step_speed=60,servo=-15,m2=R"))
        repository.sendControl(ControlInput(Move.REVERSE, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE))
        assertTrue(transport.sentFrames.last().contains("move=R,turn=L,step_speed=60,servo=-15,m2=R"))
        repository.sendControl(ControlInput(Move.REVERSE, Turn.RIGHT, StepSpeed.SLOW, 15, Move.FORWARD))
        assertTrue(transport.sentFrames.last().contains("move=R,turn=R,step_speed=60,servo=15,m2=F"))
        repository.disconnect("test")
        assertTrue(transport.sentFrames.last().contains("move=S,turn=C,step_speed=60,servo=0,m2=S"))
    }

    @Test
    fun `disabled reverse setting applies to both motors and priority stop path`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport, AppSettings(allowReverseCommand = false))
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val before = transport.sentFrames.size

        repository.sendControl(ControlInput(Move.REVERSE, Turn.CENTER, StepSpeed.SLOW, 0, Move.STOP))
        repository.sendControl(ControlInput(Move.STOP, Turn.CENTER, StepSpeed.SLOW, 0, Move.REVERSE))
        repository.sendMotorStop(ControlInput(Move.REVERSE, Turn.CENTER, StepSpeed.SLOW, 0, Move.STOP))
        repository.sendMotorStop(ControlInput(Move.STOP, Turn.CENTER, StepSpeed.SLOW, 0, Move.REVERSE))

        assertEquals(before, transport.sentFrames.size)
        assertTrue(repository.state.value.latestMessage?.text.orEmpty().contains("两路电机反向"))
        repository.disconnect("test")
    }

    @Test
    fun `individual stops preserve other motor and servo without global stop generation`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val bothMoving = ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE)
        repository.sendControl(bothMoving)
        val generation = repository.state.value.safetyStopGeneration

        repository.sendMotorStop(bothMoving.copy(move = Move.STOP))
        val m1Stopped = transport.sentFrames.last()
        assertTrue(m1Stopped.contains("move=S,turn=L,step_speed=60,servo=-15,m2=R"))
        assertEquals(generation, repository.state.value.safetyStopGeneration)
        advanceTimeBy(701L)
        runCurrent()
        assertEquals("单路停止 ACK 后保活必须保留完整新目标", m1Stopped, transport.sentFrames.last())

        repository.sendControl(bothMoving)
        repository.sendMotorStop(bothMoving.copy(motor2 = Move.STOP))
        assertTrue(transport.sentFrames.last().contains("move=F,turn=L,step_speed=60,servo=-15,m2=S"))
        assertEquals(generation, repository.state.value.safetyStopGeneration)
        repository.disconnect("test")
    }

    @Test
    fun `M2 alone uses active control cadence rather than stopped heartbeat cadence`() = runTest {
        val transport = PriorityProbeTransport(nowMillis = { testScheduler.currentTime })
        val repository = repositoryFor(transport, AppSettings(controlSendPeriodMillis = 100L, heartbeatPeriodMillis = 500L))
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        repository.sendControl(ControlInput.SAFE_STOP.copy(motor2 = Move.FORWARD))
        // 首轮 delay 可能从握手 STOP 开始；之后必须按运动周期刷新 M2。
        advanceTimeBy(501L)
        runCurrent()
        val writesAt500 = transport.sentFrames.size
        advanceTimeBy(101L)
        runCurrent()
        assertEquals(writesAt500 + 1, transport.sentFrames.size)
        assertTrue(transport.sentFrames.last().contains("move=S,turn=C,step_speed=60,servo=0,m2=F"))
        repository.disconnect("test")
    }

    @Test
    fun `individual stop overtakes queued targets and discards stale movement keepalive`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val moving = ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE)
        repository.sendControl(moving)
        transport.blockNextMovement = true
        val inFlight = async(start = CoroutineStart.UNDISPATCHED) { repository.sendControl(moving) }
        transport.blockedWriteStarted.await()
        val staleQueued = async(start = CoroutineStart.UNDISPATCHED) {
            runCatching { repository.sendControl(moving.copy(turn = Turn.RIGHT, servoDegrees = 15)) }
        }
        advanceTimeBy(501L)
        runCurrent()
        val stopping = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendMotorStop(moving.copy(move = Move.STOP))
        }

        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        stopping.await()
        assertTrue(staleQueued.await().isFailure)
        val stopIndex = transport.sentFrames.indexOfFirst { it.contains("move=S,turn=L") }
        assertTrue(stopIndex >= 0)
        advanceTimeBy(701L)
        runCurrent()
        assertTrue(transport.sentFrames.drop(stopIndex).all { it.contains("move=S") && it.contains("m2=R") })
        assertFalse(transport.sentFrames.any { it.contains("move=F,turn=R") })
        repository.disconnect("test")
    }

    @Test
    fun `later global stop cuts off queued individual stop that retains a moving motor`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val moving = ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE)
        transport.blockNextMovement = true
        val inFlight = async(start = CoroutineStart.UNDISPATCHED) { repository.sendControl(moving) }
        transport.blockedWriteStarted.await()
        val staleStop = async(start = CoroutineStart.UNDISPATCHED) {
            runCatching { repository.sendMotorStop(moving.copy(move = Move.STOP)) }
        }
        val globalStop = async(start = CoroutineStart.UNDISPATCHED) { repository.sendSafeStop("退出控制页") }

        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        assertTrue(staleStop.await().isFailure)
        globalStop.await()
        advanceTimeBy(701L)
        runCurrent()
        assertTrue(transport.sentFrames.drop(2).all { it.contains("move=S") && it.contains("m2=S") })
        assertFalse(transport.sentFrames.any { it.contains("move=S,turn=L") && it.contains("m2=R") })
        repository.disconnect("test")
    }

    @Test
    fun `late individual stop ACK cannot publish over newer global stop`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE))
        transport.responseMode = ResponseMode.NO_RESPONSE
        val singleStop = async {
            repository.sendMotorStop(ControlInput(Move.STOP, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE))
        }
        runCurrent()
        val sequence = Regex("seq=(\\d+)").find(transport.sentFrames.last())!!.groupValues[1]
        transport.responseMode = ResponseMode.OK
        repository.sendSafeStop("应用进入后台")
        val writesAfterGlobalStop = transport.sentFrames.size

        transport.emitRaw("<ACK,seq=$sequence,result=OK>\n")
        singleStop.await()
        advanceTimeBy(701L)
        runCurrent()
        assertTrue(transport.sentFrames.drop(writesAfterGlobalStop).all {
            it.contains("move=S,turn=C,step_speed=60,servo=0,m2=S")
        })
        repository.disconnect("test")
    }

    @Test
    fun `rejected individual stop neither republishes target nor repeats old moving target`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val moving = ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE)
        repository.sendControl(moving)
        transport.responseMode = ResponseMode.ERROR

        assertTrue(runCatching { repository.sendMotorStop(moving.copy(move = Move.STOP)) }.isFailure)
        val writesAfterRejection = transport.sentFrames.size
        advanceTimeBy(701L)
        runCurrent()
        assertEquals(writesAfterRejection, transport.sentFrames.size)
        transport.responseMode = ResponseMode.OK
        repository.disconnect("test")
    }

    @Test
    fun `individual stop keeps range and handshake requirements`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport)
        runCurrent()
        repository.sendMotorStop(ControlInput.SAFE_STOP.copy(motor2 = Move.FORWARD))
        assertTrue(transport.sentFrames.isEmpty())
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val before = transport.sentFrames.size
        assertTrue(runCatching {
            repository.sendMotorStop(ControlInput(Move.STOP, Turn.RIGHT, StepSpeed.SLOW, 16, Move.FORWARD))
        }.isFailure)
        assertTrue(runCatching {
            repository.sendMotorStop(ControlInput(Move.FORWARD, Turn.CENTER, StepSpeed.SLOW, 0, Move.FORWARD))
        }.isFailure)
        assertEquals(before, transport.sentFrames.size)
        repository.disconnect("test")
    }

    @Test
    fun `queued individual stop cannot retain a running motor after connection loss`() = runTest {
        val transport = PriorityProbeTransport()
        val repository = repositoryFor(transport, AppSettings(reconnectEnabled = false))
        runCurrent()
        repository.scan()
        repository.connect(repository.state.value.devices.single().id)
        val moving = ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.SLOW, -15, Move.REVERSE)
        transport.blockNextMovement = true
        val inFlight = async(start = CoroutineStart.UNDISPATCHED) { repository.sendControl(moving) }
        transport.blockedWriteStarted.await()
        val singleStop = async(start = CoroutineStart.UNDISPATCHED) {
            runCatching { repository.sendMotorStop(moving.copy(move = Move.STOP)) }
        }
        transport.reportRecoverableFailure("单路停止排队期间断开热点")
        runCurrent()
        assertEquals(RepositoryConnectionPhase.LOST, repository.state.value.connection.phase)

        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        assertTrue(singleStop.await().isFailure)
        assertFalse(transport.sentFrames.any { it.contains("move=S") && it.contains("m2=R") })
        repository.disconnect("test")
        assertTrue(transport.sentFrames.last().contains("move=S,turn=C,step_speed=60,servo=0,m2=S"))
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
                repository.sendControl(ControlInput(Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 15))
            }
        }
        val safetyStop = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendSafeStop("优先级测试")
        }
        val postStop = async(start = CoroutineStart.UNDISPATCHED) {
            repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -15))
        }

        transport.releaseBlockedWrite.complete(Unit)
        inFlight.await()
        assertTrue(staleQueued.await().isFailure)
        safetyStop.await()
        postStop.await()

        val commandsAfterHandshake = transport.sentFrames.drop(1)
        assertEquals(3, commandsAfterHandshake.size)
        assertTrue(commandsAfterHandshake[0].contains("move=F,turn=C,step_speed=60,servo=0"))
        // 尚未获 ACK 的兼容字段请求不能污染已确认状态；停止沿用最后确认的 step_speed。
        assertTrue(commandsAfterHandshake[1].contains("move=S,turn=C,step_speed=60,servo=0"))
        assertTrue(commandsAfterHandshake[2].contains("move=F,turn=L,step_speed=100,servo=-15"))
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
                repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -15))
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
    fun `slow STA stays linked by identical sequence handshake keepalive`() = runTest {
        val transport = PriorityProbeTransport(nowMillis = { testScheduler.currentTime }).apply {
            responseMode = ResponseMode.ACK_ONLY
        }
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val connecting = async { repository.connect(repository.state.value.devices.single().id) }
        runCurrent()

        advanceTimeBy(1_250L)
        runCurrent()

        assertEquals(RepositoryConnectionPhase.HANDSHAKING, repository.state.value.connection.phase)
        assertEquals(listOf(0L, 400L, 800L, 1_200L), transport.sentAtMillis)
        assertEquals("保活必须逐字节复用原握手 CMD，不推进 seq", 1, transport.sentFrames.distinct().size)
        // 模拟固件 1000 ms 控制超时。没有保活时，此处只会回 link=0，不能通过握手。
        val link = if (testScheduler.currentTime - transport.sentAtMillis.last() < 1_000L) 1 else 0
        transport.emitRaw(naStatus(sequence = 1, link = link))
        connecting.await()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(160L, repository.state.value.telemetry.snapshot?.faultBits)
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertEquals(false, repository.state.value.telemetry.snapshot?.stepRunning)
        val writesAtSuccess = transport.sentFrames.size
        advanceTimeBy(450L)
        runCurrent()
        assertEquals("成功后停止 400 ms 握手保活，500 ms 正常心跳尚未到期", writesAtSuccess,
            transport.sentFrames.size)
        repository.disconnect("test")
    }

    @Test
    fun `handshake keepalive also runs while the original ACK is pending`() = runTest {
        val transport = PriorityProbeTransport().apply { responseMode = ResponseMode.NO_RESPONSE }
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val connecting = async { repository.connect(repository.state.value.devices.single().id) }
        runCurrent()

        advanceTimeBy(450L)
        runCurrent()
        assertEquals(2, transport.sentFrames.size)
        assertEquals(1, transport.sentFrames.distinct().size)
        transport.emitRaw("<ACK,seq=1,result=DUP>\n" + naStatus(sequence = 1))
        connecting.await()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertFalse(repository.exportLogs().contains("同序号 ACK 等待器已存在"))
        transport.responseMode = ResponseMode.OK
        repository.disconnect("test")
    }

    @Test
    fun `quick NA status handshake accepts faults without redundant keepalive`() = runTest {
        val transport = PriorityProbeTransport().apply { responseMode = ResponseMode.NO_RESPONSE }
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val connecting = async { repository.connect(repository.state.value.devices.single().id) }
        runCurrent()
        transport.emitRaw("<ACK,seq=1,result=OK>\n" + naStatus(sequence = 1))
        connecting.await()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, repository.state.value.connection.handshakeStatus)
        assertEquals(160L, repository.state.value.telemetry.snapshot?.faultBits)
        assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
        assertNull(repository.state.value.telemetry.snapshot?.stepEstimatedRpm)
        assertNull(repository.state.value.telemetry.snapshot?.stepActualRpm)
        advanceTimeBy(450L)
        runCurrent()
        assertEquals(1, transport.sentFrames.size)
        transport.responseMode = ResponseMode.OK
        repository.disconnect("test")
    }

    @Test
    fun `queued keepalives are discarded after the probe completes`() = runTest {
        val transport = PriorityProbeTransport().apply {
            blockNextSafetyStop = true
            responseMode = ResponseMode.NO_RESPONSE
        }
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val connecting = async { repository.connect(repository.state.value.devices.single().id) }
        transport.safetyWriteStarted.await()

        // 原始写入尚未返回，400/800 ms 的保活只能在同一 actor 中排队。
        advanceTimeBy(850L)
        runCurrent()
        assertEquals(1, transport.sentFrames.size)
        // 接收协程独立于 writer：先让 probe 实际处理 ACK+STA，再释放原始写入。
        // 有缓冲 SharedFlow 的 emit 返回不等于接收方已处理，不能依赖 send 内的 emit 顺序。
        transport.emitRaw("<ACK,seq=1,result=OK>\n" + naStatus(sequence = 1))
        runCurrent()
        assertNotNull(repository.state.value.telemetry.snapshot)
        assertFalse("原始写入仍未返回，连接过程尚未结束", connecting.isCompleted)
        transport.releaseSafetyWrite.complete(Unit)
        connecting.await()
        runCurrent()

        assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
        assertEquals("probe 完成后不能把排队保活补发到已连接阶段", 1, transport.sentFrames.size)
        transport.responseMode = ResponseMode.OK
        repository.disconnect("test")
    }

    @Test
    fun `keepalive cannot replace same sequence linked status verification`() = runTest {
        val transport = PriorityProbeTransport().apply { responseMode = ResponseMode.ACK_ONLY }
        val repository = repositoryFor(transport)
        runCurrent()
        repository.scan()
        val connecting = async { repository.connect(repository.state.value.devices.single().id) }
        runCurrent()

        transport.emitRaw(naStatus(sequence = 2) + naStatus(sequence = 1, link = 0))
        advanceTimeBy(1_250L)
        runCurrent()
        assertEquals(RepositoryConnectionPhase.HANDSHAKING, repository.state.value.connection.phase)
        assertFalse(connecting.isCompleted)
        connecting.await()
        assertEquals(RepositoryConnectionPhase.FAILED, repository.state.value.connection.phase)
        assertTrue(repository.state.value.connection.detail.orEmpty().contains("握手超时"))
        val failedWrites = transport.sentFrames.size
        advanceTimeBy(1_000L)
        runCurrent()
        assertEquals(failedWrites, transport.sentFrames.size)
    }

    @Test
    fun `cancellation during ACK or STA wait stops keepalive before the next session`() = runTest {
        for (mode in listOf(ResponseMode.NO_RESPONSE, ResponseMode.ACK_ONLY)) {
            val transport = PriorityProbeTransport().apply { responseMode = mode }
            val repository = repositoryFor(transport)
            runCurrent()
            repository.scan()
            val id = repository.state.value.devices.single().id
            val connecting = async { repository.connect(id) }
            runCurrent()
            advanceTimeBy(450L)
            runCurrent()
            assertEquals(2, transport.sentFrames.size)
            val oldFrame = transport.sentFrames.first()

            connecting.cancel()
            connecting.join()
            val writesAtCancellation = transport.sentFrames.size
            advanceTimeBy(1_000L)
            runCurrent()
            assertEquals("取消 $mode 后不能再发送握手保活", writesAtCancellation, transport.sentFrames.size)

            transport.responseMode = ResponseMode.OK
            repository.connect(id)
            advanceTimeBy(600L)
            runCurrent()
            assertEquals(RepositoryConnectionPhase.CONNECTED, repository.state.value.connection.phase)
            assertFalse("旧 probe 的字节不能进入新会话", transport.sentFrames.drop(writesAtCancellation).contains(oldFrame))
            repository.disconnect("test")
        }
    }

    @Test
    fun `write failure ACK timeout and ERR all terminate handshake keepalive`() = runTest {
        for (mode in listOf(ResponseMode.OK, ResponseMode.NO_RESPONSE, ResponseMode.ERROR)) {
            val transport = PriorityProbeTransport().apply {
                responseMode = mode
                failNextSafetyStop = mode == ResponseMode.OK
            }
            val repository = repositoryFor(transport)
            runCurrent()
            repository.scan()

            repository.connect(repository.state.value.devices.single().id)

            assertEquals(RepositoryConnectionPhase.FAILED, repository.state.value.connection.phase)
            val failedWrites = transport.sentFrames.size
            advanceTimeBy(1_000L)
            runCurrent()
            assertEquals("$mode 失败后不得继续握手保活", failedWrites, transport.sentFrames.size)
        }
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

        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -15))
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

        repository.sendControl(ControlInput(Move.FORWARD, Turn.RIGHT, StepSpeed.FAST, 15, motor2 = Move.REVERSE))
        assertTrue(transport.sentFrames.last().contains("move=F,turn=R,step_speed=100,servo=15,m2=R"))
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
        repository.sendControl(ControlInput(Move.FORWARD, Turn.LEFT, StepSpeed.FAST, -15, motor2 = Move.REVERSE))
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
        assertTrue("重新连接只能从双路安全停止恢复，不得重放旧运动",
            framesAfterLoss.all { it.contains("move=S") && it.contains("m2=S") })
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

    private fun naStatus(sequence: Int, link: Int = 1): String =
        "<STA,seq=$sequence,link=$link,step_rpm=NA,step_est=NA,step_actual=NA," +
            "step_on=0,servo=0,roll=1.2,pitch=-5.1,yaw=74.7,err=160>\n"

    private class PriorityProbeTransport(
        private val candidateCount: Int = 1,
        override val kind: TransportKind = TransportKind.MOCK,
        private val connectFailure: Exception? = null,
        private val nowMillis: () -> Long = { 0L },
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
        val sentAtMillis = mutableListOf<Long>()
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
            sentAtMillis += nowMillis()
            events += "send:${text.trim()}"
            val match = COMMAND.matchEntire(text) ?: error("unexpected frame: $text")
            val sequence = match.groupValues[1].toInt()
            val move = match.groupValues[2]
            val speed = match.groupValues[4].toInt()
            val servo = match.groupValues[5].toInt()
            val motor2 = match.groupValues[6]
            if (failNextSafetyStop && move == "S" && motor2 == "S") {
                failNextSafetyStop = false
                throw IllegalStateException("injected safety write failure")
            }
            if (blockNextSafetyStop && move == "S" && motor2 == "S") {
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
                    "<ACK,seq=$sequence,result=OK>\n" + status(sequence, target, servo, move, motor2)
                ResponseMode.DUPLICATE_ACK ->
                    "<ACK,seq=$sequence,result=DUP>\n" + status(sequence, target, servo, move, motor2)
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

        private fun status(sequence: Int, target: Int, servo: Int, move: String, motor2: String): String =
            "<STA,seq=$sequence,link=1,step_rpm=$target,step_est=$target," +
                "step_actual=NA,servo=$servo,roll=0.0,pitch=0.0,yaw=0.0,m1=$move,m2=$motor2,err=0>\n"

        private companion object {
            val COMMAND = Regex(
                "<CMD,seq=(\\d+),move=([FRS]),turn=([LRC]),step_speed=(60|100),servo=(-?\\d+),m2=([FRS])>\\n",
            )
        }
    }
}
