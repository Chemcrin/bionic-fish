package com.bionicfish.controller.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.selection.SelectionContainer
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.CloudOff
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material.icons.filled.Download
import androidx.compose.material.icons.filled.Error
import androidx.compose.material.icons.filled.HourglassBottom
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.bionicfish.controller.ui.components.MetricCard
import com.bionicfish.controller.ui.components.ResponsivePair
import com.bionicfish.controller.ui.components.ScreenTitle
import com.bionicfish.controller.ui.components.ScrollableScreen
import com.bionicfish.controller.ui.components.SectionCard
import com.bionicfish.controller.ui.model.TelemetryUiState
import com.bionicfish.controller.ui.model.UiActions
import com.bionicfish.controller.ui.theme.FishSpacing
import java.util.Locale

@Composable
fun TelemetryScreen(
    telemetry: TelemetryUiState,
    actions: UiActions,
    contentPadding: PaddingValues,
) {
    var showClearLogsDialog by remember { mutableStateOf(false) }
    val fresh = !telemetry.isStale

    ScrollableScreen(contentPadding = contentPadding) {
        ScreenTitle(
            title = "状态 / 调试",
            subtitle = "所有数值都标注有效性和更新时间，过期数据不冒充实时值。",
        )

        SectionCard(title = "数据新鲜度") {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = if (fresh) Icons.Default.CheckCircle else Icons.Default.HourglassBottom,
                    contentDescription = null,
                    tint = if (fresh) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.error,
                    modifier = Modifier.size(28.dp),
                )
                Spacer(Modifier.width(FishSpacing.sm))
                Column(Modifier.weight(1f)) {
                    Text(
                        text = if (fresh) "实时数据" else "数据过期",
                        style = MaterialTheme.typography.titleMedium,
                        color = if (fresh) MaterialTheme.colorScheme.onSurface else MaterialTheme.colorScheme.error,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = "最后更新：${telemetry.lastUpdatedLabel}",
                        style = MaterialTheme.typography.bodyMedium,
                    )
                }
            }
            if (!fresh) {
                Text(
                    text = "以下数值仅供参考，不应用于判断当前鱼体状态。",
                    color = MaterialTheme.colorScheme.error,
                    style = MaterialTheme.typography.bodyMedium,
                    modifier = Modifier.padding(top = FishSpacing.xs),
                )
            }
        }

        SectionCard(title = "链路") {
            ResponsivePair(
                first = { modifier ->
                    MetricCard(
                        label = "STM32 链路",
                        value = if (telemetry.linkAlive) "在线" else "离线",
                        supporting = if (fresh) "来自最新状态帧" else "状态帧已过期",
                        valid = fresh && telemetry.linkAlive,
                        modifier = modifier,
                    )
                },
                second = { modifier ->
                    MetricCard(
                        label = "协议延迟",
                        value = telemetry.latencyMs?.let { "$it ms" } ?: "不可用",
                        supporting = "往返/应答统计",
                        valid = fresh && telemetry.latencyMs != null,
                        modifier = modifier,
                    )
                },
            )
            Spacer(Modifier.height(FishSpacing.sm))
            Text("最后序号：${telemetry.lastSequence?.toString() ?: "不可用"}")
            Text("发送帧：${telemetry.sentFrameCount} · 接收帧：${telemetry.receivedFrameCount}")
        }

        SectionCard(title = "步进电机") {
            ResponsivePair(
                first = { modifier ->
                    MetricCard(
                        label = "目标转速",
                        value = "${telemetry.targetStepRpm} RPM",
                        valid = fresh,
                        modifier = modifier,
                    )
                },
                second = { modifier ->
                    MetricCard(
                        label = "换相估算转速",
                        value = telemetry.estimatedStepRpm?.let { "$it RPM" } ?: "不可用",
                        supporting = "无编码器，非实测转速",
                        valid = fresh && telemetry.estimatedStepRpm != null,
                        modifier = modifier,
                    )
                },
            )
            Spacer(Modifier.height(FishSpacing.sm))
            MetricCard(
                label = "实际转速",
                value = telemetry.actualStepRpm?.let { "$it RPM" } ?: "不可用",
                supporting = if (telemetry.actualStepRpm == null) "当前硬件无速度传感器" else "来自实测",
                valid = fresh && telemetry.actualStepRpm != null,
                modifier = Modifier.fillMaxWidth(),
            )
        }

        SectionCard(title = "舵机与姿态") {
            MetricCard(
                label = "舵机角度",
                value = "${formatSigned(telemetry.servoAngleDegrees)}°",
                valid = fresh,
                modifier = Modifier.fillMaxWidth(),
            )
            Spacer(Modifier.height(FishSpacing.sm))
            ResponsivePair(
                first = { modifier ->
                    MetricCard(
                        label = "滚转 Roll",
                        value = telemetry.rollDegrees.asAngle(),
                        valid = fresh && telemetry.rollDegrees != null,
                        modifier = modifier,
                    )
                },
                second = { modifier ->
                    MetricCard(
                        label = "俯仰 Pitch",
                        value = telemetry.pitchDegrees.asAngle(),
                        valid = fresh && telemetry.pitchDegrees != null,
                        modifier = modifier,
                    )
                },
            )
            Spacer(Modifier.height(FishSpacing.sm))
            MetricCard(
                label = "偏航 Yaw",
                value = telemetry.yawDegrees.asAngle(),
                valid = fresh && telemetry.yawDegrees != null,
                modifier = Modifier.fillMaxWidth(),
            )
        }

        SectionCard(title = "故障") {
            val faultPresent = telemetry.faultCode != null && telemetry.faultCode != 0
            Row(verticalAlignment = Alignment.CenterVertically) {
                Icon(
                    imageVector = if (faultPresent) Icons.Default.Error else Icons.Default.CheckCircle,
                    contentDescription = null,
                    tint = if (faultPresent) MaterialTheme.colorScheme.error else MaterialTheme.colorScheme.primary,
                )
                Spacer(Modifier.width(FishSpacing.xs))
                Text(
                    text = when (val code = telemetry.faultCode) {
                        null -> "故障码：不可用"
                        else -> "故障码：$code (0x${code.toString(16).uppercase(Locale.ROOT)})"
                    },
                    fontWeight = FontWeight.SemiBold,
                )
            }
            Text(
                text = telemetry.faultSummary,
                style = MaterialTheme.typography.bodyMedium,
                modifier = Modifier.padding(top = FishSpacing.xs),
            )
        }

        SectionCard(title = "本地通信日志") {
            ResponsivePair(
                first = { modifier ->
                    Button(
                        onClick = actions.onExportLogs,
                        enabled = !telemetry.isStale || telemetry.logLines.isNotEmpty(),
                        modifier = modifier.heightIn(min = FishSpacing.minimumTouchTarget),
                    ) {
                        Icon(Icons.Default.Download, contentDescription = null)
                        Spacer(Modifier.width(FishSpacing.xs))
                        Text("导出日志")
                    }
                },
                second = { modifier ->
                    OutlinedButton(
                        onClick = { showClearLogsDialog = true },
                        enabled = telemetry.logLines.isNotEmpty(),
                        modifier = modifier.heightIn(min = FishSpacing.minimumTouchTarget),
                    ) {
                        Icon(Icons.Default.Delete, contentDescription = null)
                        Spacer(Modifier.width(FishSpacing.xs))
                        Text("清空日志")
                    }
                },
            )

            Spacer(Modifier.height(FishSpacing.sm))
            if (telemetry.logLines.isEmpty()) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Icon(Icons.Default.CloudOff, contentDescription = null)
                    Spacer(Modifier.width(FishSpacing.xs))
                    Text("暂无通信日志", color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
            } else {
                SelectionContainer {
                    Column(
                        modifier = Modifier
                            .fillMaxWidth()
                            .semantics { contentDescription = "最近通信日志" },
                    ) {
                        telemetry.logLines.takeLast(100).forEachIndexed { index, line ->
                            if (index > 0) HorizontalDivider()
                            Text(
                                text = line,
                                fontFamily = FontFamily.Monospace,
                                style = MaterialTheme.typography.bodySmall,
                                modifier = Modifier.padding(vertical = FishSpacing.xs),
                            )
                        }
                    }
                }
            }
        }
    }

    if (showClearLogsDialog) {
        AlertDialog(
            onDismissRequest = { showClearLogsDialog = false },
            title = { Text("清空本地日志？") },
            text = { Text("此操作不影响设备，但无法恢复尚未导出的记录。") },
            confirmButton = {
                Button(
                    onClick = {
                        showClearLogsDialog = false
                        actions.onClearLogs()
                    },
                ) { Text("清空") }
            },
            dismissButton = {
                TextButton(onClick = { showClearLogsDialog = false }) { Text("取消") }
            },
        )
    }
}

private fun Float?.asAngle(): String = this?.let {
    String.format(Locale.ROOT, "%.1f°", it)
} ?: "不可用"

private fun formatSigned(value: Int): String = if (value > 0) "+$value" else value.toString()
