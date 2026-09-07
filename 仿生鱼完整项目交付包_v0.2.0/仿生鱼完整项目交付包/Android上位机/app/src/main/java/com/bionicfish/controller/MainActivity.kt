package com.bionicfish.controller

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.activity.viewModels
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.bionicfish.controller.control.AppEvent
import com.bionicfish.controller.control.BionicFishViewModel
import com.bionicfish.controller.ui.BionicFishApp
import java.nio.charset.StandardCharsets
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

class MainActivity : ComponentActivity() {
    private val viewModel: BionicFishViewModel by viewModels {
        BionicFishViewModel.Factory((application as BionicFishApplication).repository)
    }

    private val createLogDocument = registerForActivityResult(
        ActivityResultContracts.CreateDocument("text/plain"),
    ) { uri ->
        if (uri == null) {
            viewModel.consumePendingExportText()
            viewModel.reportExportResult(success = false, cancelled = true)
            return@registerForActivityResult
        }
        val text = viewModel.consumePendingExportText()
        if (text == null) {
            viewModel.reportExportResult(success = false, detail = "待导出的日志已失效")
            return@registerForActivityResult
        }
        lifecycleScope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    contentResolver.openOutputStream(uri, "wt")?.use { output ->
                        output.write(text.toByteArray(StandardCharsets.UTF_8))
                    } ?: error("无法打开目标文件")
                }
            }
            viewModel.reportExportResult(
                success = result.isSuccess,
                detail = result.exceptionOrNull()?.message,
            )
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                viewModel.events.collect { event ->
                    when (event) {
                        is AppEvent.CreateLogDocument -> createLogDocument.launch(event.suggestedName)
                    }
                }
            }
        }

        setContent {
            val state by viewModel.uiState.collectAsStateWithLifecycle()
            val actions = remember(viewModel) { viewModel.actions }
            BionicFishApp(state = state, actions = actions)
        }
    }
}
