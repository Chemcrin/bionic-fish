package com.bionicfish.controller.ui

import android.graphics.Bitmap
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTextReplacement
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.bionicfish.controller.settings.ApDirectDefaults
import com.bionicfish.controller.ui.model.AppDestination
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.ControlUiState
import com.bionicfish.controller.ui.model.DeviceUiModel
import com.bionicfish.controller.ui.model.HandshakePhase
import com.bionicfish.controller.ui.model.SettingsUiState
import com.bionicfish.controller.ui.model.UiActions
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ApDirectUiTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun initialPage_connectsApWithoutScanning() {
        var apCalls = 0
        var scanCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(),
                actions = UiActions(
                    onConnectApDirect = { apCalls++ },
                    onStartScan = { scanCalls++ },
                ),
            )
        }

        composeRule.onNodeWithText("AP 直连 · 仿生鱼热点").assertIsDisplayed()
        composeRule.onNodeWithText("TCP ${ApDirectDefaults.HOST}:${ApDirectDefaults.PORT} · 无需扫描")
            .assertIsDisplayed()
        composeRule.onNodeWithTag("ap_connect")
            .assertIsDisplayed().assertIsEnabled().assertHeightIsAtLeast(48.dp).performClick()
        composeRule.runOnIdle {
            assertEquals(1, apCalls)
            assertEquals(0, scanCalls)
        }
    }

    @Test
    fun wifiSettingsButton_onlyInvokesSystemSettingsAction() {
        var wifiCalls = 0
        var apCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(),
                actions = UiActions(
                    onOpenWifiSettings = { wifiCalls++ },
                    onConnectApDirect = { apCalls++ },
                ),
            )
        }

        composeRule.onNodeWithTag("ap_wifi_settings").performClick()
        composeRule.runOnIdle {
            assertEquals(1, wifiCalls)
            assertEquals(0, apCalls)
        }
    }

    @Test
    fun activeSession_disablesNewApConnectionAndNetworkSwitch() {
        var phase by mutableStateOf(ConnectionPhase.CONNECTING)
        var handshake by mutableStateOf(HandshakePhase.NOT_STARTED)
        var detail by mutableStateOf<String?>(null)
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(connection = ConnectionUiState(
                    phase = phase,
                    isApDirect = true,
                    handshake = handshake,
                    detail = detail,
                )),
                actions = UiActions(),
            )
        }

        listOf(
            ConnectionPhase.CONNECTING,
            ConnectionPhase.SCANNING,
            ConnectionPhase.RECONNECTING,
            ConnectionPhase.CONNECTED,
            ConnectionPhase.DISCONNECTING,
        ).forEach { current ->
            composeRule.runOnIdle { phase = current }
            composeRule.onNodeWithTag("ap_connect").assertIsNotEnabled()
            composeRule.onNodeWithTag("ap_wifi_settings").assertIsNotEnabled()
            composeRule.onNodeWithTag("ap_connection_summary").assertExists()
            if (current == ConnectionPhase.CONNECTING) {
                composeRule.onNodeWithTag("ap_connect").assertTextContains("正在连接仿生鱼…")
            } else if (current == ConnectionPhase.RECONNECTING) {
                composeRule.onNodeWithTag("ap_connect").assertTextContains("正在重连仿生鱼…")
            }
        }
        composeRule.runOnIdle {
            phase = ConnectionPhase.CONNECTING
            handshake = HandshakePhase.VERIFYING
        }
        composeRule.onNodeWithTag("ap_connect").assertTextContains("正在验证 STM32…")
        composeRule.runOnIdle {
            phase = ConnectionPhase.LOST
            handshake = HandshakePhase.FAILED
            detail = "未收到 ACK，请检查 STM32 供电。"
        }
        composeRule.onNodeWithTag("ap_connect").assertIsEnabled()
        composeRule.onNodeWithTag("ap_connect").assertTextContains("重新连接仿生鱼热点")
        composeRule.onNodeWithText("未收到有效的 STM32 应答，请检查鱼端供电与串口桥接。").assertExists()
    }

    @Test
    fun apPreset_isNotRepeatedInScannedDeviceList() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    devices = listOf(
                        DeviceUiModel(
                            id = "ap-test",
                            name = "AP 预置列表项",
                            address = "192.168.4.1:9000",
                            discoveredAt = "预置",
                            isApDirect = true,
                        ),
                    ),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onAllNodesWithText("AP 预置列表项").assertCountEquals(0)
        composeRule.onNodeWithTag("ap_connect").assertIsDisplayed()
    }

    @Test
    fun settings_showsIndependentApDefaultsAndEditsAllThreeParameters() {
        var settings by mutableStateOf(SettingsUiState(wifiHost = "10.10.0.9", wifiPort = "8123"))
        var saveCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(initialDestination = AppDestination.SETTINGS, settings = settings),
                actions = UiActions(
                    onApSsidChanged = { settings = settings.copy(apSsid = it) },
                    onApHostChanged = { settings = settings.copy(apHost = it) },
                    onApPortChanged = { settings = settings.copy(apPort = it) },
                    onSaveSettings = { saveCalls++ },
                ),
            )
        }

        composeRule.onNodeWithTag("ap_ssid_input").assertTextContains(ApDirectDefaults.SSID)
            .performScrollTo().performTextReplacement("Fish-Lab")
        composeRule.onNodeWithTag("ap_host_input").assertTextContains(ApDirectDefaults.HOST)
            .performScrollTo().performTextReplacement("192.168.4.2")
        composeRule.onNodeWithTag("ap_port_input").assertTextContains(ApDirectDefaults.PORT.toString())
            .performScrollTo().performTextReplacement("9001")
        composeRule.onNodeWithText("保存设置").performScrollTo().performClick()
        composeRule.runOnIdle {
            assertEquals("Fish-Lab", settings.apSsid)
            assertEquals("192.168.4.2", settings.apHost)
            assertEquals("9001", settings.apPort)
            assertEquals("10.10.0.9", settings.wifiHost)
            assertEquals("8123", settings.wifiPort)
            assertEquals(1, saveCalls)
        }
    }

    @Test
    fun twoHundredPercentText_keepsApActionsAndConnectedControlReachable() {
        var connected by mutableStateOf(false)
        var apCalls = 0
        composeRule.setContent {
            val baseDensity = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(baseDensity.density, 2f)) {
                Box(Modifier.width(320.dp).height(560.dp)) {
                    BionicFishApp(
                        state = AppUiState(
                            connection = ConnectionUiState(
                                phase = if (connected) ConnectionPhase.CONNECTED else ConnectionPhase.DISCONNECTED,
                                isApDirect = connected,
                            ),
                            control = ControlUiState(enabled = connected),
                        ),
                        actions = UiActions(onConnectApDirect = { apCalls++ }),
                    )
                }
            }
        }

        composeRule.onNodeWithTag("ap_wifi_settings").performScrollTo().assertIsDisplayed()
        composeRule.onNodeWithTag("ap_connect").performScrollTo()
            .assertIsDisplayed().assertHeightIsAtLeast(48.dp).performClick()
        composeRule.runOnIdle {
            assertEquals(1, apCalls)
            connected = true
        }
        composeRule.onNodeWithTag("ap_open_control").performScrollTo().assertIsDisplayed().performClick()
        composeRule.onNodeWithTag("emergency_stop").assertIsDisplayed().assertIsEnabled()
    }

    /** 真实 Android/Compose 渲染截图；由验收脚本从 external files/renders 收集。 */
    @Test
    fun renderApDirectDevicePageAndLargeTextToPng() {
        var fontScale by mutableStateOf(1f)
        composeRule.setContent {
            val baseDensity = LocalDensity.current
            CompositionLocalProvider(LocalDensity provides Density(baseDensity.density, fontScale)) {
                BionicFishApp(state = AppUiState(), actions = UiActions(), darkTheme = false)
            }
        }

        composeRule.waitForIdle()
        saveRoot("ap-direct-device-page.png")
        composeRule.runOnIdle { fontScale = 2f }
        composeRule.onNodeWithTag("ap_connect").performScrollTo().assertIsDisplayed()
        saveRoot("ap-direct-200-percent-text.png")
    }

    private fun saveRoot(fileName: String) {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val directory = File(context.getExternalFilesDir(null), "renders")
        assertTrue(directory.isDirectory || directory.mkdirs())
        val output = File(directory, fileName)
        output.outputStream().use { stream ->
            assertTrue(composeRule.onRoot().captureToImage().asAndroidBitmap()
                .compress(Bitmap.CompressFormat.PNG, 100, stream))
        }
        assertTrue(output.isFile && output.length() > 0L)
    }
}
