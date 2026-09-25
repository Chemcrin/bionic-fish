package com.bionicfish.controller.control

import androidx.lifecycle.ViewModelStore
import com.bionicfish.controller.device.BionicFishRepository
import com.bionicfish.controller.device.ConnectionInfo
import com.bionicfish.controller.device.RepositoryConnectionPhase
import com.bionicfish.controller.device.RepositoryState
import com.bionicfish.controller.protocol.ControlInput
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.settings.AppSettings
import com.bionicfish.controller.ui.model.ControlMode
import com.bionicfish.controller.ui.model.MoveDirection
import com.bionicfish.controller.ui.model.TurnDirection
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestScope
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runCurrent
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** 从 UI 意图验证完整目标快照；仓库的实际优先队列另有集成测试。 */
@OptIn(ExperimentalCoroutinesApi::class)
class BionicFishViewModelTest {
    @Test
    fun `steering and M2 changes preserve the other motor and clamp commands to 15 degrees`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            vm.actions.onSteeringChanged(TurnDirection.LEFT, -99)
            runCurrent()
            val target = repo.controls.last()
            assertEquals(Move.FORWARD, target.move)
            assertEquals(Move.REVERSE, target.motor2)
            assertEquals(-15, target.servoDegrees)
            assertEquals(60, target.stepSpeed.rpm)
        }
    }

    @Test
    fun `individual motor stops preserve the other direction and steering without global stop`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            vm.actions.onSteeringChanged(TurnDirection.LEFT, -15)
            runCurrent()
            vm.actions.onMoveChanged(MoveDirection.STOP)
            runCurrent()
            assertEquals(Move.STOP, repo.motorStops.last().move)
            assertEquals(Move.REVERSE, repo.motorStops.last().motor2)
            assertEquals(-15, repo.motorStops.last().servoDegrees)
            assertEquals(MoveDirection.REVERSE, vm.uiState.value.control.motor2)
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.STOP)
            runCurrent()
            assertEquals(Move.FORWARD, repo.motorStops.last().move)
            assertEquals(Move.STOP, repo.motorStops.last().motor2)
            assertEquals(0, repo.safeStops)
        }
    }

    @Test
    fun `global stop clears both motor drafts with center and hold steering policies`() = runTest {
        for (center in listOf(true, false)) {
            withViewModel(AppSettings(centerServoOnSafeStop = center)) { vm, repo ->
                vm.actions.onMoveChanged(MoveDirection.REVERSE)
                vm.actions.onMotor2Changed(MoveDirection.FORWARD)
                vm.actions.onSteeringChanged(TurnDirection.RIGHT, 15)
                runCurrent()
                vm.actions.onEmergencyStop()
                runCurrent()
                assertEquals(1, repo.safeStops)
                assertEquals(MoveDirection.STOP, vm.uiState.value.control.move)
                assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
                assertEquals(if (center) 0 else 15, vm.uiState.value.control.servoAngleDegrees)
            }
        }
    }

    @Test
    fun `background stop generation and lost link clear M2 without another user gesture`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMotor2Changed(MoveDirection.FORWARD)
            runCurrent()
            repo.sendSafeStop("后台停止")
            runCurrent()
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
            vm.actions.onMotor2Changed(MoveDirection.FORWARD)
            runCurrent()
            repo.state.value = repo.state.value.copy(connection = ConnectionInfo(phase = RepositoryConnectionPhase.LOST))
            runCurrent()
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.move)
        }
    }

    @Test
    fun `steering before background stop collector runs cannot revive either motor`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            runCurrent()

            repo.sendSafeStop("后台停止")
            // 故意不运行 collector：按键入口必须直接读取最新全停代次。
            vm.actions.onSteeringChanged(TurnDirection.RIGHT, 15)
            runCurrent()

            assertEquals(Move.STOP, repo.controls.last().move)
            assertEquals(Move.STOP, repo.controls.last().motor2)
            assertEquals(15, repo.controls.last().servoDegrees)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.move)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
        }
    }

    @Test
    fun `individual stop before background stop collector runs cannot revive other motor`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            runCurrent()

            repo.sendSafeStop("后台停止")
            vm.actions.onMoveChanged(MoveDirection.STOP)
            runCurrent()

            assertEquals(Move.STOP, repo.motorStops.last().move)
            assertEquals(Move.STOP, repo.motorStops.last().motor2)
        }
    }

    @Test
    fun `queued UI target is discarded when global stop changes its generation`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMotor2Changed(MoveDirection.FORWARD)
            // launch 尚未取得仓库 ticket；新的全停已经生效时不能晚到重启。
            repo.sendSafeStop("后台停止")
            runCurrent()

            assertTrue(repo.controls.isEmpty())
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
        }
    }

    @Test
    fun `changing control mode stops even when only M2 is running`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMotor2Changed(MoveDirection.FORWARD)
            runCurrent()
            vm.actions.onControlModeChanged(ControlMode.HOLD_TO_RUN)
            runCurrent()
            assertEquals(1, repo.safeStops)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
            assertTrue(repo.state.value.settings.pressAndHoldToMove)
        }
    }

    @Test
    fun `saved reverse off blocks both UI intents without retaining an unsent reverse draft`() = runTest {
        withViewModel(AppSettings(allowReverseCommand = false)) { vm, repo ->
            vm.actions.onMoveChanged(MoveDirection.REVERSE)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            runCurrent()
            assertTrue(repo.controls.isEmpty())
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.move)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
            assertFalse(vm.uiState.value.control.reverseSupported)
        }
    }

    @Test
    fun `reverse setting can be saved and disabling it first requests global stop`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            runCurrent()
            vm.actions.onAllowReverseCommandChanged(false)
            vm.actions.onSaveSettings()
            runCurrent()
            assertFalse(repo.state.value.settings.allowReverseCommand)
            assertEquals(1, repo.safeStops)
            assertEquals(MoveDirection.STOP, vm.uiState.value.control.motor2)
            vm.actions.onAllowReverseCommandChanged(true)
            vm.actions.onSaveSettings()
            runCurrent()
            assertTrue(repo.state.value.settings.allowReverseCommand)
        }
    }

    @Test
    fun `reverse disable blocks controls until stop ACK and settings write both finish`() = runTest {
        withViewModel { vm, repo ->
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            runCurrent()
            val controlsBeforeSave = repo.controls.size
            val stopAck = CompletableDeferred<Unit>()
            val settingsWrite = CompletableDeferred<Unit>()
            repo.stopAckGate = stopAck
            repo.settingsWriteGate = settingsWrite
            vm.actions.onAllowReverseCommandChanged(false)
            vm.actions.onSaveSettings()
            vm.actions.onMoveChanged(MoveDirection.REVERSE)
            runCurrent()
            assertEquals(1, repo.safeStops)
            assertTrue(repo.state.value.settings.allowReverseCommand)

            vm.actions.onSaveSettings()
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            vm.actions.onMoveChanged(MoveDirection.STOP)
            vm.actions.onEmergencyStop()
            runCurrent()
            assertEquals("保存等待期间全局停止仍然可用", 2, repo.safeStops)
            assertEquals(controlsBeforeSave, repo.controls.size)
            assertTrue(repo.motorStops.isEmpty())
            assertEquals(0, repo.settingsUpdates)

            stopAck.complete(Unit)
            runCurrent()
            assertEquals(1, repo.settingsUpdates)
            assertTrue(repo.state.value.settings.allowReverseCommand)
            vm.actions.onMotor2Changed(MoveDirection.REVERSE)
            vm.actions.onSaveSettings()
            runCurrent()
            assertEquals(controlsBeforeSave, repo.controls.size)
            assertEquals(1, repo.settingsUpdates)

            settingsWrite.complete(Unit)
            runCurrent()
            assertFalse(repo.state.value.settings.allowReverseCommand)
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            runCurrent()
            assertEquals(controlsBeforeSave + 1, repo.controls.size)
            assertEquals(Move.STOP, repo.controls.last().motor2)
        }
    }

    @Test
    fun `failed reverse disable releases save gate without committing false setting`() = runTest {
        withViewModel { vm, repo ->
            val stopAck = CompletableDeferred<Unit>()
            repo.stopAckGate = stopAck
            vm.actions.onAllowReverseCommandChanged(false)
            vm.actions.onSaveSettings()
            runCurrent()
            stopAck.completeExceptionally(IllegalStateException("测试 ACK 超时"))
            runCurrent()
            assertTrue(repo.state.value.settings.allowReverseCommand)
            assertEquals(0, repo.settingsUpdates)

            repo.stopAckGate = null
            vm.actions.onSaveSettings()
            runCurrent()
            assertFalse(repo.state.value.settings.allowReverseCommand)
            assertEquals(1, repo.settingsUpdates)
            vm.actions.onMoveChanged(MoveDirection.FORWARD)
            runCurrent()
            assertEquals(Move.FORWARD, repo.controls.last().move)
        }
    }

    private suspend fun TestScope.withViewModel(
        settings: AppSettings = AppSettings(),
        block: suspend TestScope.(BionicFishViewModel, RecordingRepository) -> Unit,
    ) {
        Dispatchers.setMain(StandardTestDispatcher(testScheduler))
        val repo = RecordingRepository(settings)
        val vm = BionicFishViewModel(repo)
        val store = ViewModelStore().apply { put("test", vm) }
        val collector = backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { vm.uiState.collect {} }
        try {
            runCurrent()
            block(vm, repo)
        } finally {
            collector.cancel()
            store.clear()
            // 先完成 viewModelScope/StateFlow 的取消，再撤销测试 Main dispatcher。
            runCurrent()
            Dispatchers.resetMain()
        }
    }

    private class RecordingRepository(settings: AppSettings) : BionicFishRepository {
        override val state = MutableStateFlow(RepositoryState(
            connection = ConnectionInfo(phase = RepositoryConnectionPhase.CONNECTED),
            settings = settings,
            reverseCommandAllowed = settings.allowReverseCommand,
        ))
        val controls = mutableListOf<ControlInput>()
        val motorStops = mutableListOf<ControlInput>()
        var safeStops = 0
        var settingsUpdates = 0
        var stopAckGate: CompletableDeferred<Unit>? = null
        var settingsWriteGate: CompletableDeferred<Unit>? = null
        override suspend fun sendControl(input: ControlInput) { controls += input.validated() }
        override suspend fun sendMotorStop(input: ControlInput) { motorStops += input.validated() }
        override suspend fun sendSafeStop(reason: String) {
            safeStops += 1
            state.value = state.value.copy(safetyStopGeneration = state.value.safetyStopGeneration + 1L)
            stopAckGate?.await()
        }
        override suspend fun updateSettings(settings: AppSettings) {
            settingsUpdates += 1
            settingsWriteGate?.await()
            state.value = state.value.copy(settings = settings.validated(), reverseCommandAllowed = settings.allowReverseCommand)
        }
        override suspend fun disconnect(reason: String) {
            sendSafeStop(reason)
            state.value = state.value.copy(connection = ConnectionInfo())
        }
        override suspend fun scan() = Unit
        override fun cancelScan() = Unit
        override suspend fun connect(deviceId: String) = Unit
        override suspend fun connectApDirect() = Unit
        override suspend fun exportLogs() = ""
        override fun dismissMessage(messageId: Long) = Unit
        override fun clearLogs() = Unit
    }
}
