package com.bionicfish.controller.ui

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.width
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.assertCountEquals
import androidx.compose.ui.test.assertHeightIsAtLeast
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsEnabled
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.assertTextContains
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.compose.ui.test.performScrollTo
import androidx.compose.ui.test.performTouchInput
import androidx.compose.ui.test.swipe
import androidx.compose.ui.unit.Density
import androidx.compose.ui.unit.dp
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.bionicfish.controller.ui.model.AppDestination
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.BannerLevel
import com.bionicfish.controller.ui.model.BannerUiModel
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.ControlUiState
import com.bionicfish.controller.ui.model.SettingsUiState
import com.bionicfish.controller.ui.model.TelemetryUiState
import com.bionicfish.controller.ui.model.TransportKind
import com.bionicfish.controller.ui.model.UiActions
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class BionicFishAppTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun emergencyStop_isProminentLargeAndImmediate() {
        var stopCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState(),
                actions = UiActions(onEmergencyStop = { stopCalls += 1 }),
            )
        }

        composeRule.onNodeWithTag("emergency_stop")
            .assertIsDisplayed()
            .assertIsEnabled()
            .assertHeightIsAtLeast(44.dp)
            .performClick()

        composeRule.runOnIdle { assertEquals(1, stopCalls) }
    }

    @Test
    fun reverseControl_isDisabledWhenFirmwareDoesNotAllowIt() {
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState(),
                actions = UiActions(),
            )
        }

        composeRule.onAllNodesWithText("后退").assertCountEquals(0)
        composeRule.onNodeWithText("后退未启用：当前 STM32 安全策略只允许单方向步进。")
            .performScrollTo()
            .assertIsDisplayed()
    }

    @Test
    fun bluetoothEntries_areHiddenWithoutBluetoothHardware() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    connection = ConnectionUiState(bluetoothHardwareAvailable = false),
                ),
                actions = UiActions(),
            )
        }

        // AP 直连是首页首选入口，原局域网传输选项仍可通过滚动到达。
        composeRule.onNodeWithText("Wi-Fi TCP").performScrollTo().assertIsDisplayed()
        composeRule.onNodeWithText("模拟设备").assertExists()
        composeRule.onAllNodesWithText("经典蓝牙").assertCountEquals(0)
        composeRule.onAllNodesWithText("低功耗蓝牙").assertCountEquals(0)
    }

    @Test
    fun staleTelemetry_isClearlyMarkedAndNullValuesAreUnavailable() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.TELEMETRY,
                    telemetry = TelemetryUiState(
                        isStale = true,
                        lastUpdatedLabel = "12:00:00",
                        actualStepRpm = null,
                        rollDegrees = null,
                    ),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithText("数据过期").assertIsDisplayed()
        composeRule.onAllNodes(hasText("不可用", substring = true))[0]
            .assertTextContains("不可用")
    }

    @Test
    fun udpSettings_showConfigurableDiscoveryParameters() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.SETTINGS,
                    settings = SettingsUiState(selectedTransport = TransportKind.WIFI_UDP),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithText("UDP 设备发现").assertExists()
        composeRule.onNodeWithText("发现地址").assertExists()
        composeRule.onNodeWithText("发现端口").assertExists()
        composeRule.onNodeWithText("发现请求载荷").assertExists()
    }

    @Test
    fun bluetoothSettings_showFieldsForSelectedBluetoothTypeOnly() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.SETTINGS,
                    connection = ConnectionUiState(bluetoothHardwareAvailable = true),
                    settings = SettingsUiState(selectedTransport = TransportKind.BLUETOOTH_CLASSIC),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithText("经典蓝牙 Service UUID").assertExists()
        composeRule.onAllNodesWithText("BLE Service UUID").assertCountEquals(0)
        composeRule.onAllNodesWithText("BLE Characteristic UUID").assertCountEquals(0)
    }

    @Test
    fun twoHundredPercentFontScale_keepsPrimaryControlsReachable() {
        composeRule.setContent {
            val baseDensity = LocalDensity.current
            CompositionLocalProvider(
                LocalDensity provides Density(baseDensity.density, fontScale = 2f),
            ) {
                BionicFishApp(
                    state = connectedControlState(),
                    actions = UiActions(),
                )
            }
        }

        composeRule.onNodeWithTag("emergency_stop").assertIsDisplayed()
        composeRule.onNodeWithTag("nav_telemetry").assertIsDisplayed()
    }

    @Test
    fun longErrorBannerAndLargeText_keepSafetyStopClickable() {
        var stopCalls = 0
        composeRule.setContent {
            val baseDensity = LocalDensity.current
            CompositionLocalProvider(
                LocalDensity provides Density(baseDensity.density, fontScale = 2f),
            ) {
                Box(Modifier.width(320.dp).height(480.dp)) {
                    BionicFishApp(
                        state = connectedControlState().copy(
                            banner = BannerUiModel(
                                message = "控制帧发送失败，连接状态需要重新确认。安全停止帧可能尚未送达，请等待固件失联保护并检查网络。",
                                level = BannerLevel.ERROR,
                            ),
                        ),
                        actions = UiActions(onEmergencyStop = { stopCalls += 1 }),
                    )
                }
            }
        }

        composeRule.onNodeWithTag("emergency_stop")
            .assertIsDisplayed()
            .assertHeightIsAtLeast(44.dp)
            .performClick()
        composeRule.runOnIdle { assertEquals(1, stopCalls) }
    }

    @Test
    fun leavingControlView_requestsSafeStop() {
        var stopCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState(),
                actions = UiActions(onEmergencyStop = { stopCalls += 1 }),
            )
        }

        composeRule.onNodeWithTag("nav_telemetry").performClick()
        composeRule.runOnIdle { assertEquals(1, stopCalls) }
    }

    @Test
    fun emergencyStop_remainsVisibleAfterScrollingToTargetSummary() {
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState(),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithText("目标指令").performScrollTo()
        composeRule.onNodeWithTag("emergency_stop").assertIsDisplayed()
    }

    @Test
    fun servoSlider_sendsOnlyOneFinalCommandAfterDrag() {
        var steeringCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState(),
                actions = UiActions(onSteeringChanged = { _, _ -> steeringCalls += 1 }),
            )
        }

        composeRule.onNodeWithTag("servo_slider")
            .performScrollTo()
            .performTouchInput {
                // 从当前 0° 拇指位置开始真实拖动，确保触发 Slider 的松手回调。
                swipe(
                    start = center,
                    end = Offset(right - 24f, centerY),
                    durationMillis = 400,
                )
            }

        composeRule.runOnIdle { assertEquals(1, steeringCalls) }
    }

    @Test
    fun navigation_reachesAllFourPrimaryViews() {
        composeRule.setContent {
            BionicFishApp(state = AppUiState(), actions = UiActions())
        }

        composeRule.onNodeWithTag("nav_control").performClick()
        composeRule.onNodeWithText("目标指令").assertExists()
        composeRule.onNodeWithTag("nav_telemetry").performClick()
        composeRule.onNodeWithText("数据新鲜度").assertExists()
        composeRule.onNodeWithTag("nav_settings").performClick()
        composeRule.onNodeWithText("权限与隐私").assertExists()
        composeRule.onNodeWithTag("nav_devices").performClick()
        composeRule.onNodeWithText("发现的设备").assertExists()
    }

    private fun connectedControlState() = AppUiState(
        initialDestination = AppDestination.CONTROL,
        connection = ConnectionUiState(phase = ConnectionPhase.CONNECTED),
        control = ControlUiState(
            enabled = true,
            stepperParametersConfirmed = true,
            reverseSupported = false,
            stepSpeedRpm = 60,
        ),
    )
}
