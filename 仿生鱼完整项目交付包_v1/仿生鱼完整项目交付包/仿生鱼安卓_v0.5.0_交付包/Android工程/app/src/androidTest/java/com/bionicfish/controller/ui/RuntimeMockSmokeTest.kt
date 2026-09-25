package com.bionicfish.controller.ui

import android.graphics.Bitmap
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createAndroidComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.bionicfish.controller.BionicFishApplication
import com.bionicfish.controller.MainActivity
import com.bionicfish.controller.device.HandshakeStatus
import com.bionicfish.controller.device.LogDirection
import com.bionicfish.controller.device.RepositoryConnectionPhase
import com.bionicfish.controller.protocol.Move
import com.bionicfish.controller.transport.TransportKind
import java.io.File
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 真实 Activity → ViewModel → 应用仓库 → MockTransport → ASCII 解析 → UI 回传。
 * 只连接 mock://bionic-fish，不点击 AP/TCP 入口，也不接触任何实体电机。
 */
@RunWith(AndroidJUnit4::class)
class RuntimeMockSmokeTest {
    @get:Rule
    val composeRule = createAndroidComposeRule<MainActivity>()

    @Test
    fun realActivity_mockHandshakeDualMotorSteeringIndependentStopsAndNaTelemetry() {
        val application = composeRule.activity.application as BionicFishApplication
        val repository = application.repository
        composeRule.waitForIdle()
        // 启动不应自动连接；测试不接管任何正在运行的真实设备会话。
        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
        val originalSettings = repository.state.value.settings

        try {
            runBlocking {
                withTimeout(5_000L) {
                    repository.updateSettings(
                        originalSettings.copy(
                            transportKind = TransportKind.MOCK,
                            pressAndHoldToMove = false,
                            reconnectEnabled = false,
                            reduceMotion = true,
                            allowReverseCommand = true,
                        ),
                    )
                }
            }
            repository.clearLogs()
            composeRule.waitUntil(5_000L) {
                repository.state.value.settings.transportKind == TransportKind.MOCK
            }

            // 扫描和连接均通过 MainActivity 实际渲染的操作入口触发。
            composeRule.onNodeWithTag("nav_devices").performClick()
            composeRule.onNodeWithText("扫描设备").performScrollTo().assertIsEnabled().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.devices.any { it.endpoint.address == "mock://bionic-fish" } &&
                    repository.state.value.connection.phase == RepositoryConnectionPhase.DISCONNECTED
            }
            composeRule.onNodeWithText("连接").performScrollTo().assertIsEnabled().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.connection.phase == RepositoryConnectionPhase.CONNECTED &&
                    repository.state.value.telemetry.snapshot != null
            }
            val connected = repository.state.value
            assertEquals(HandshakeStatus.VERIFIED_V1_COMPATIBLE, connected.connection.handshakeStatus)
            assertEquals("mock://bionic-fish", connected.connection.connectedDevice?.endpoint?.address)
            assertNull(connected.telemetry.snapshot?.stepTargetRpm)
            assertNull(connected.telemetry.snapshot?.stepEstimatedRpm)
            assertNull(connected.telemetry.snapshot?.stepActualRpm)
            assertEquals(false, connected.telemetry.snapshot?.stepRunning)
            assertEquals(Move.STOP, connected.telemetry.snapshot?.motor1Direction)
            assertEquals(Move.STOP, connected.telemetry.snapshot?.motor2Direction)

            composeRule.onNodeWithTag("nav_control").performClick()
            composeRule.onNodeWithText("双电机独立控制").performScrollTo().assertIsDisplayed()
            composeRule.onAllNodesWithText("60 RPM", substring = true).assertCountEquals(0)
            composeRule.onAllNodesWithText("100 RPM", substring = true).assertCountEquals(0)
            composeRule.onNodeWithTag("m1_forward").performScrollTo().assertIsEnabled().performClick()
            composeRule.onNodeWithTag("m2_reverse").performScrollTo().assertIsEnabled().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.telemetry.snapshot?.let {
                    it.motor1Direction == Move.FORWARD && it.motor2Direction == Move.REVERSE
                } == true
            }
            saveScreenshot("runtime-control-dual-motor.png")

            // 单路停止不能升级成全局安全停止，另一路与舵角必须保留。
            composeRule.onNodeWithTag("m1_stop").performScrollTo().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.telemetry.snapshot?.let {
                    it.motor1Direction == Move.STOP && it.motor2Direction == Move.REVERSE
                } == true
            }
            composeRule.onNodeWithTag("m1_reverse").performScrollTo().performClick()
            composeRule.onNodeWithTag("m2_stop").performScrollTo().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.telemetry.snapshot?.let {
                    it.motor1Direction == Move.REVERSE && it.motor2Direction == Move.STOP
                } == true
            }
            composeRule.onNodeWithTag("m1_forward").performScrollTo().performClick()
            composeRule.onNodeWithTag("m2_reverse").performScrollTo().performClick()

            for ((label, angle) in listOf("左转 -15°" to -15, "右转 +15°" to 15, "直行／舵机 0°" to 0)) {
                composeRule.onNodeWithText(label).performScrollTo().assertIsEnabled().performClick()
                composeRule.waitUntil(5_000L) {
                    repository.state.value.telemetry.snapshot?.let {
                        it.servoDegrees == angle && it.motor1Direction == Move.FORWARD && it.motor2Direction == Move.REVERSE
                    } == true
                }
            }

            composeRule.onNodeWithTag("emergency_stop").assertIsDisplayed().performClick()
            composeRule.waitUntil(5_000L) {
                repository.state.value.telemetry.snapshot?.let {
                    it.motor1Direction == Move.STOP && it.motor2Direction == Move.STOP
                } == true
            }
            val commands = repository.state.value.logs.filter { it.direction == LogDirection.TX }.map { it.text }
            assertTrue(commands.isNotEmpty())
            assertTrue(commands.all { it.contains("step_speed=60") })
            assertTrue(commands.all { it.contains(",m2=") })
            assertTrue(commands.any { it.contains("move=F,turn=L,step_speed=60,servo=-15,m2=R") })
            assertTrue(commands.any { it.contains("move=F,turn=R,step_speed=60,servo=15,m2=R") })
            assertTrue(commands.any { it.contains("move=S") && it.contains("m2=R") })
            assertTrue(commands.any { it.contains("move=R") && it.contains("m2=S") })
            assertTrue(commands.any { it.contains("move=S") && it.contains("m2=S") })
            assertFalse(commands.any { it.contains("n20", ignoreCase = true) })

            composeRule.onNodeWithTag("nav_telemetry").performClick()
            composeRule.onAllNodesWithText("已停止").assertCountEquals(2)
            composeRule.onAllNodesWithText("NA（未提供）").assertCountEquals(3)
            composeRule.onAllNodesWithText("0 RPM").assertCountEquals(0)
            composeRule.onAllNodesWithText("0.0 RPM").assertCountEquals(0)
            assertNotNull(repository.state.value.telemetry.snapshot)
            assertNull(repository.state.value.telemetry.snapshot?.stepTargetRpm)
            // 将新版本的 NA 转速卡片滚入截图，避免只截到链路与运行标志。
            if (composeRule.onAllNodesWithText("知道了").fetchSemanticsNodes().isNotEmpty()) {
                composeRule.onNodeWithText("知道了").performClick()
            }
            composeRule.onNodeWithTag("telemetry_m1_direction").performScrollTo().assertIsDisplayed()
            saveScreenshot("telemetry-screen-dual-direction.png")
            composeRule.onAllNodesWithText("NA（未提供）")[2].performScrollTo().assertIsDisplayed()
            saveScreenshot("telemetry-screen-na.png")
        } finally {
            runBlocking {
                withTimeout(5_000L) {
                    repository.disconnect("模拟运行验收结束")
                    repository.updateSettings(originalSettings)
                }
            }
        }
        assertEquals(RepositoryConnectionPhase.DISCONNECTED, repository.state.value.connection.phase)
    }

    private fun saveScreenshot(fileName: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.getExternalFilesDir(null), "renders")
        assertTrue(directory.isDirectory || directory.mkdirs())
        val output = File(directory, fileName)
        output.outputStream().use { stream ->
            assertTrue(
                composeRule.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, stream),
            )
        }
        assertTrue(output.isFile && output.length() > 0L)
    }
}
