package com.bionicfish.controller.ui

import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
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
import androidx.compose.ui.test.assert
import androidx.compose.ui.test.SemanticsMatcher
import androidx.compose.ui.semantics.SemanticsProperties
import androidx.compose.ui.semantics.ProgressBarRangeInfo
import androidx.compose.ui.test.hasText
import androidx.compose.ui.test.hasAnyDescendant
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onAllNodesWithText
import androidx.compose.ui.test.onNodeWithTag
import androidx.compose.ui.test.onNodeWithContentDescription
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
import com.bionicfish.controller.ui.model.MoveDirection
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
    fun reverseControl_isDisabledForBothMotorsWhenSettingIsOff() {
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState().copy(control = ControlUiState(enabled = true, reverseSupported = false)),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithTag("m1_reverse").performScrollTo().assertIsNotEnabled()
        composeRule.onNodeWithTag("m2_reverse").performScrollTo().assertIsNotEnabled()
        composeRule.onNodeWithText("M2 后退未启用：请在设置中开启“允许电机反向”并保存。")
            .performScrollTo()
            .assertIsDisplayed()
    }

    @Test
    fun fixedDutyDualMotorControl_doesNotAdvertiseUnmeasuredRpmOrSpeedSelection() {
        composeRule.setContent {
            BionicFishApp(state = connectedControlState(), actions = UiActions())
        }

        composeRule.onNodeWithText("双电机独立控制").performScrollTo().assertIsDisplayed()
        composeRule.onAllNodesWithText("60 RPM", substring = true).assertCountEquals(0)
        composeRule.onAllNodesWithText("100 RPM", substring = true).assertCountEquals(0)
        listOf("m1_forward", "m1_stop", "m1_reverse", "m2_forward", "m2_stop", "m2_reverse").forEach {
            composeRule.onNodeWithTag(it).performScrollTo().assertIsEnabled().assertHeightIsAtLeast(44.dp)
        }
    }

    @Test
    fun naTelemetry_usesIndependentDirectionsAndNeverFabricatesZeroRpm() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.TELEMETRY,
                    telemetry = TelemetryUiState(
                        isStale = false,
                        stepRunning = true,
                        motor1Direction = MoveDirection.FORWARD,
                        motor2Direction = MoveDirection.REVERSE,
                    ),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithTag("telemetry_m1_direction").performScrollTo().assert(hasAnyDescendant(hasText("前进")))
        composeRule.onNodeWithTag("telemetry_m2_direction").performScrollTo().assert(hasAnyDescendant(hasText("后退")))
        composeRule.onAllNodesWithText("NA（未提供）").assertCountEquals(3)
        composeRule.onAllNodesWithText("0 RPM").assertCountEquals(0)
        composeRule.onAllNodesWithText("0.0 RPM").assertCountEquals(0)
        composeRule.onAllNodesWithText("已停止").assertCountEquals(0)
    }

    @Test
    fun naTelemetry_m1StoppedDoesNotHideRunningM2() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.TELEMETRY,
                    telemetry = TelemetryUiState(
                        isStale = false,
                        stepRunning = false,
                        motor1Direction = MoveDirection.STOP,
                        motor2Direction = MoveDirection.REVERSE,
                    ),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onNodeWithText("已停止").performScrollTo().assertIsDisplayed()
        composeRule.onNodeWithText("后退").performScrollTo().assertIsDisplayed()
        composeRule.onAllNodesWithText("0 RPM").assertCountEquals(0)
        composeRule.onAllNodesWithText("0.0 RPM").assertCountEquals(0)
    }

    @Test
    fun oldFirmwareWithoutDirections_showsUnknownEvenWhenStepOnIsKnown() {
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(
                    initialDestination = AppDestination.TELEMETRY,
                    telemetry = TelemetryUiState(isStale = false, stepRunning = true),
                ),
                actions = UiActions(),
            )
        }

        composeRule.onAllNodesWithText("未知").assertCountEquals(2)
        composeRule.onAllNodesWithText("已停止").assertCountEquals(0)
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
        listOf("m1_forward", "m1_stop", "m1_reverse", "m2_forward", "m2_stop", "m2_reverse").forEach {
            composeRule.onNodeWithTag(it).performScrollTo().assertIsDisplayed().assertHeightIsAtLeast(44.dp)
        }
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
    fun independentMotorStops_doNotCallGlobalEmergencyStopOrChangeOtherTarget() {
        var control by mutableStateOf(ControlUiState(enabled = true))
        var emergencyCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState().copy(control = control),
                actions = UiActions(
                    onMoveChanged = { control = control.copy(move = it) },
                    onMotor2Changed = { control = control.copy(motor2 = it) },
                    onEmergencyStop = {
                        emergencyCalls++
                        control = control.copy(move = MoveDirection.STOP, motor2 = MoveDirection.STOP)
                    },
                ),
            )
        }
        composeRule.onNodeWithTag("m1_forward").performScrollTo().performClick()
        composeRule.onNodeWithTag("m2_reverse").performScrollTo().performClick()
        composeRule.runOnIdle {
            assertEquals(MoveDirection.FORWARD, control.move)
            assertEquals(MoveDirection.REVERSE, control.motor2)
        }
        composeRule.onNodeWithTag("m1_stop").performScrollTo().performClick()
        composeRule.runOnIdle {
            assertEquals(MoveDirection.STOP, control.move)
            assertEquals(MoveDirection.REVERSE, control.motor2)
            assertEquals(0, emergencyCalls)
        }
        composeRule.onNodeWithTag("m1_reverse").performScrollTo().performClick()
        composeRule.onNodeWithTag("m2_stop").performScrollTo().performClick()
        composeRule.runOnIdle {
            assertEquals(MoveDirection.REVERSE, control.move)
            assertEquals(MoveDirection.STOP, control.motor2)
            assertEquals(0, emergencyCalls)
        }
        composeRule.onNodeWithTag("m2_forward").performScrollTo().performClick()
        composeRule.onNodeWithTag("emergency_stop").performClick()
        composeRule.runOnIdle {
            assertEquals(MoveDirection.STOP, control.move)
            assertEquals(MoveDirection.STOP, control.motor2)
            assertEquals(1, emergencyCalls)
        }
    }

    @Test
    fun servoPresetsAndSlider_useFifteenDegreesOneDegreeStepsAndCorrectSelection() {
        var control by mutableStateOf(ControlUiState(enabled = true))
        composeRule.setContent {
            BionicFishApp(
                state = connectedControlState().copy(control = control),
                actions = UiActions(onSteeringChanged = { turn, angle ->
                    control = control.copy(turn = turn, servoAngleDegrees = angle)
                }),
            )
        }
        for ((label, angle) in listOf("左转 -15°" to -15, "右转 +15°" to 15, "直行／舵机 0°" to 0)) {
            composeRule.onNodeWithText(label).performScrollTo().performClick()
                .assert(SemanticsMatcher.expectValue(SemanticsProperties.StateDescription, "当前目标"))
            composeRule.runOnIdle { assertEquals(angle, control.servoAngleDegrees) }
        }
        composeRule.onNodeWithTag("servo_slider").performScrollTo()
            .assert(SemanticsMatcher.expectValue(SemanticsProperties.ProgressBarRangeInfo, ProgressBarRangeInfo(0f, -15f..15f, 29)))
        composeRule.onNodeWithContentDescription("舵机角度，范围负 15 度到正 15 度；松手后发送").assertExists()
    }

    @Test
    fun reverseSetting_isEditableAndRequiresSaveAction() {
        var settings by mutableStateOf(SettingsUiState(allowReverseCommand = false))
        var saveCalls = 0
        composeRule.setContent {
            BionicFishApp(
                state = AppUiState(initialDestination = AppDestination.SETTINGS, settings = settings),
                actions = UiActions(
                    onAllowReverseCommandChanged = { settings = settings.copy(allowReverseCommand = it) },
                    onSaveSettings = { saveCalls++ },
                ),
            )
        }
        composeRule.onNodeWithContentDescription("允许电机反向（M1/M2 均生效）").performScrollTo().performClick()
        composeRule.runOnIdle {
            assertEquals(true, settings.allowReverseCommand)
            assertEquals(0, saveCalls)
        }
        composeRule.onNodeWithText("保存设置").performScrollTo().performClick()
        composeRule.runOnIdle { assertEquals(1, saveCalls) }
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
            stepperParametersConfirmed = false,
            reverseSupported = true,
            stepSpeedRpm = 60,
        ),
    )
}
