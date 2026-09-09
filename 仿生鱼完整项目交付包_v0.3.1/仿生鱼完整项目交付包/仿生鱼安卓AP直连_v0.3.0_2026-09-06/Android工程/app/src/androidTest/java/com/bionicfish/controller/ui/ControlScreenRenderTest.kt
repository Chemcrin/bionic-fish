package com.bionicfish.controller.ui

import android.graphics.Bitmap
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.v2.createComposeRule
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.unit.Density
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.bionicfish.controller.ui.model.AppDestination
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.ControlUiState
import com.bionicfish.controller.ui.model.MoveDirection
import com.bionicfish.controller.ui.model.TurnDirection
import com.bionicfish.controller.ui.model.UiActions
import java.io.File
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.runner.RunWith

/**
 * 在真实 Android 渲染管线中输出控制页浅色/深色截图。
 * 文件写入目标应用 external files/renders，验收脚本再通过 adb pull 收集。
 */
@RunWith(AndroidJUnit4::class)
class ControlScreenRenderTest {
    @get:Rule
    val composeRule = createComposeRule()

    @Test
    fun renderOptimizedControlScreenToPng() {
        var darkTheme by mutableStateOf(false)
        var requestedFontScale by mutableStateOf(1f)
        composeRule.setContent {
            val baseDensity = LocalDensity.current
            CompositionLocalProvider(
                LocalDensity provides Density(baseDensity.density, requestedFontScale),
            ) {
                BionicFishApp(
                    state = previewState(),
                    actions = UiActions(),
                    darkTheme = darkTheme,
                )
            }
        }

        composeRule.waitForIdle()
        saveRoot("control-screen-light.png")

        composeRule.runOnIdle { darkTheme = true }
        composeRule.waitForIdle()
        saveRoot("control-screen-dark.png")

        composeRule.runOnIdle {
            darkTheme = false
            requestedFontScale = 2f
        }
        composeRule.waitForIdle()
        saveRoot("control-screen-200-percent-text.png")
    }

    private fun saveRoot(fileName: String) {
        val targetContext = InstrumentationRegistry.getInstrumentation().targetContext
        val outputDirectory = File(targetContext.getExternalFilesDir(null), "renders")
        assertTrue(outputDirectory.isDirectory || outputDirectory.mkdirs())
        val output = File(outputDirectory, fileName)
        output.outputStream().use { stream ->
            assertTrue(composeRule.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, stream))
        }
        assertTrue(output.isFile && output.length() > 0L)
    }

    private fun previewState() = AppUiState(
        initialDestination = AppDestination.CONTROL,
        connection = ConnectionUiState(
            phase = ConnectionPhase.CONNECTED,
            connectedDeviceName = "模拟仿生鱼",
        ),
        control = ControlUiState(
            enabled = true,
            move = MoveDirection.FORWARD,
            turn = TurnDirection.LEFT,
            servoAngleDegrees = -30,
            reverseSupported = false,
        ),
    )
}
