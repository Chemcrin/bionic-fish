package com.bionicfish.controller.ui.screens

import android.content.res.Configuration
import android.view.accessibility.AccessibilityManager
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsPressedAsState
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
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
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.automirrored.filled.ArrowForward
import androidx.compose.material.icons.filled.ArrowDownward
import androidx.compose.material.icons.filled.ArrowUpward
import androidx.compose.material.icons.filled.CheckCircle
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Lock
import androidx.compose.material.icons.filled.StopCircle
import androidx.compose.material.icons.filled.Straight
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import com.bionicfish.controller.ui.BionicFishApp
import com.bionicfish.controller.ui.components.ConnectionBadge
import com.bionicfish.controller.ui.components.ScreenTitle
import com.bionicfish.controller.ui.components.ScrollableScreen
import com.bionicfish.controller.ui.components.SectionCard
import com.bionicfish.controller.ui.model.AppDestination
import com.bionicfish.controller.ui.model.AppUiState
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.ControlMode
import com.bionicfish.controller.ui.model.ControlUiState
import com.bionicfish.controller.ui.model.MoveDirection
import com.bionicfish.controller.ui.model.TurnDirection
import com.bionicfish.controller.ui.model.UiActions
import com.bionicfish.controller.ui.theme.FishSpacing
import kotlin.math.roundToInt

@Composable
fun ControlScreen(
    connection: ConnectionUiState,
    control: ControlUiState,
    actions: UiActions,
    contentPadding: PaddingValues,
) {
    val connectionPhase = connection.phase
    val connected = connectionPhase == ConnectionPhase.CONNECTED
    // 双直流电机不再使用已移除的步进参数确认门禁。
    val driveEnabled = connected && control.enabled
    val steeringEnabled = connected && control.enabled
    val context = LocalContext.current
    val accessibilityManager = remember(context) {
        context.getSystemService(AccessibilityManager::class.java)
    }
    var touchExplorationEnabled by remember(accessibilityManager) {
        mutableStateOf(accessibilityManager?.isTouchExplorationEnabled == true)
    }
    DisposableEffect(accessibilityManager) {
        val listener = AccessibilityManager.TouchExplorationStateChangeListener { enabled ->
            touchExplorationEnabled = enabled
        }
        accessibilityManager?.addTouchExplorationStateChangeListener(listener)
        onDispose {
            accessibilityManager?.removeTouchExplorationStateChangeListener(listener)
        }
    }
    val effectiveControlMode = if (touchExplorationEnabled) {
        ControlMode.EXPLICIT_STOP
    } else {
        control.mode
    }
    LaunchedEffect(touchExplorationEnabled, control.mode) {
        if (touchExplorationEnabled && control.mode == ControlMode.HOLD_TO_RUN) {
            // TalkBack 的 ACTION_CLICK 没有可靠的 pointer press/release 生命周期；
            // 自动切回显式停止，保证方向按钮可以访问且不会留下无法释放的按压状态。
            actions.onControlModeChanged(ControlMode.EXPLICIT_STOP)
        }
    }

    var servoDraftDegrees by remember {
        mutableIntStateOf(control.servoAngleDegrees.coerceIn(-15, 15))
    }
    var servoDragging by remember { mutableStateOf(false) }
    LaunchedEffect(control.servoAngleDegrees) {
        // 拖动期间保留本地预览，避免状态回传把滑块抢回旧位置。
        if (!servoDragging) {
            servoDraftDegrees = control.servoAngleDegrees.coerceIn(-15, 15)
        }
    }
    LaunchedEffect(steeringEnabled) {
        if (!steeringEnabled) {
            servoDragging = false
            servoDraftDegrees = control.servoAngleDegrees.coerceIn(-15, 15)
        }
    }

    ScrollableScreen(contentPadding = contentPadding) {
            ScreenTitle(
                title = "操控台",
                subtitle = "清晰下达目标指令；执行与安全保护由 STM32 完成。",
            )

            CommandOverviewCard(
                connection = connection,
                control = control,
            )

            SectionCard(title = "操纵") {
                ControlModeRow(
                    mode = effectiveControlMode,
                    accessibilityForcesExplicitMode = touchExplorationEnabled,
                    onModeChanged = actions.onControlModeChanged,
                )

                HorizontalDivider(Modifier.padding(vertical = FishSpacing.md))

                ControlSectionLabel(
                    title = "双电机独立控制",
                    supporting = "配套固件两路均为固定 100% 占空比，无手机调速功能。step_speed 仅为兼容字段，不改变输出。",
                )

                Spacer(Modifier.height(FishSpacing.md))
                MotorControls(
                    id = "m1",
                    title = "主推进 M1 · 370 直流减速电机",
                    direction = control.move,
                    driveEnabled = driveEnabled,
                    connected = connected,
                    reverseSupported = control.reverseSupported,
                    mode = effectiveControlMode,
                    onMove = actions.onMoveChanged,
                )
                HorizontalDivider(Modifier.padding(vertical = FishSpacing.md))
                MotorControls(
                    id = "m2",
                    title = "辅助推进 M2 · N20 直流减速电机",
                    direction = control.motor2,
                    driveEnabled = driveEnabled,
                    connected = connected,
                    reverseSupported = control.reverseSupported,
                    mode = effectiveControlMode,
                    onMove = actions.onMotor2Changed,
                )
                Text(
                    text = "单路操作保留另一路及舵机目标；底部“立即安全停止”同时停止 M1 和 M2。",
                    style = MaterialTheme.typography.bodySmall,
                    modifier = Modifier.padding(top = FishSpacing.sm),
                )

                Spacer(Modifier.height(FishSpacing.md))
                ControlSectionLabel(
                    title = "转向预设",
                    supporting = "转向与舵机角度会作为同一目标原子发送。",
                )
                Spacer(Modifier.height(FishSpacing.sm))
                AdaptiveChoicePair(
                    first = { modifier ->
                        TurnChoice(
                            label = "左转 -15°",
                            icon = Icons.AutoMirrored.Filled.ArrowBack,
                            selected = control.servoAngleDegrees == -15,
                            enabled = steeringEnabled,
                            onClick = {
                                actions.onSteeringChanged(TurnDirection.LEFT, -15)
                            },
                            modifier = modifier,
                        )
                    },
                    second = { modifier ->
                        TurnChoice(
                            label = "右转 +15°",
                            icon = Icons.AutoMirrored.Filled.ArrowForward,
                            selected = control.servoAngleDegrees == 15,
                            enabled = steeringEnabled,
                            onClick = {
                                actions.onSteeringChanged(TurnDirection.RIGHT, 15)
                            },
                            modifier = modifier,
                        )
                    },
                )
                Spacer(Modifier.height(FishSpacing.sm))
                TurnChoice(
                    label = "直行／舵机 0°",
                    icon = Icons.Default.Straight,
                    selected = control.servoAngleDegrees == 0,
                    enabled = steeringEnabled,
                    onClick = {
                        actions.onSteeringChanged(TurnDirection.CENTER, 0)
                    },
                    modifier = Modifier.fillMaxWidth(),
                )
            }

            SectionCard(title = "舵机精细调节") {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Column(Modifier.weight(1f)) {
                        Text(
                            text = "目标角度",
                            style = MaterialTheme.typography.labelLarge,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                        Text(
                            text = "${formatSigned(servoDraftDegrees)}°",
                            style = MaterialTheme.typography.headlineMedium,
                            fontWeight = FontWeight.Bold,
                        )
                    }
                    Surface(
                        shape = MaterialTheme.shapes.extraLarge,
                        color = MaterialTheme.colorScheme.primaryContainer,
                        contentColor = MaterialTheme.colorScheme.onPrimaryContainer,
                    ) {
                        Text(
                            text = when {
                                servoDraftDegrees < 0 -> "向左"
                                servoDraftDegrees > 0 -> "向右"
                                else -> "居中"
                            },
                            modifier = Modifier.padding(horizontal = FishSpacing.md, vertical = FishSpacing.xs),
                            style = MaterialTheme.typography.labelLarge,
                        )
                    }
                }
                Slider(
                    value = servoDraftDegrees.toFloat(),
                    onValueChange = { raw ->
                        servoDragging = true
                        servoDraftDegrees = raw.roundToInt().coerceIn(-15, 15)
                    },
                    onValueChangeFinished = {
                        val angle = servoDraftDegrees.coerceIn(-15, 15)
                        // 滑动过程只更新本地预览；松手后只发送一次最终目标，避免 ACK 队列堆积。
                        if (steeringEnabled && angle != control.servoAngleDegrees) {
                            actions.onSteeringChanged(turnForAngle(angle), angle)
                        }
                        servoDragging = false
                    },
                    enabled = steeringEnabled,
                    valueRange = -15f..15f,
                    steps = 29,
                    modifier = Modifier
                        .fillMaxWidth()
                        .testTag("servo_slider")
                        .semantics {
                            contentDescription = "舵机角度，范围负 15 度到正 15 度；松手后发送"
                            stateDescription = "预览角度 $servoDraftDegrees 度"
                        },
                )
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                ) {
                    Text("-15° 左", style = MaterialTheme.typography.labelMedium)
                    Text("0° 中", style = MaterialTheme.typography.labelMedium)
                    Text("+15° 右", style = MaterialTheme.typography.labelMedium)
                }
                Text(
                    text = "拖动时仅在本机预览，松手后发送一次最终角度。",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(top = FishSpacing.xs),
                )
            }

            SectionCard(title = "目标指令") {
                TargetCommandDetails(control = control)
            }
    }
}

@Composable
private fun CommandOverviewCard(
    connection: ConnectionUiState,
    control: ControlUiState,
) {
    val connected = connection.phase == ConnectionPhase.CONNECTED
    val containerColor = if (connected) {
        MaterialTheme.colorScheme.secondaryContainer
    } else {
        MaterialTheme.colorScheme.surfaceContainerHigh
    }
    val contentColor = if (connected) {
        MaterialTheme.colorScheme.onSecondaryContainer
    } else {
        MaterialTheme.colorScheme.onSurface
    }
    val deviceLabel = connection.connectedDeviceName
        ?: if (connected) "已选择的仿生鱼" else "尚未连接设备"

    Card(
        modifier = Modifier
            .fillMaxWidth()
            .semantics {
                liveRegion = LiveRegionMode.Polite
                stateDescription = "${connectionLabel(connection.phase)}，M1 ${moveLabel(control.move)}，M2 ${moveLabel(control.motor2)}"
            },
        colors = CardDefaults.cardColors(
            containerColor = containerColor,
            contentColor = contentColor,
        ),
    ) {
        Column(Modifier.padding(FishSpacing.md)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                ConnectionBadge(connection.phase)
                Spacer(Modifier.width(FishSpacing.sm))
                Column(Modifier.weight(1f)) {
                    Text(
                        text = deviceLabel,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                    )
                    Text(
                        text = if (connected) "指令通道就绪" else "控制已锁定；请先在设备页建立连接",
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            HorizontalDivider(
                modifier = Modifier.padding(vertical = FishSpacing.sm),
                color = contentColor.copy(alpha = 0.18f),
            )
            CommandMetricStrip(control)
        }
    }
}

@Composable
private fun CommandMetricStrip(control: ControlUiState) {
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val useColumn = LocalDensity.current.fontScale >= 1.45f || maxWidth < 240.dp
        if (useColumn) {
            Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.xs)) {
                CommandMetric("M1 · 370", moveLabel(control.move), Modifier.fillMaxWidth())
                CommandMetric("M2 · N20", moveLabel(control.motor2), Modifier.fillMaxWidth())
                CommandMetric("舵机", "${formatSigned(control.servoAngleDegrees)}°", Modifier.fillMaxWidth())
            }
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(FishSpacing.xs)) {
                CommandMetric("M1 · 370", moveLabel(control.move), Modifier.weight(1f))
                CommandMetric("M2 · N20", moveLabel(control.motor2), Modifier.weight(1f))
                CommandMetric("舵机", "${formatSigned(control.servoAngleDegrees)}°", Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun CommandMetric(
    label: String,
    value: String,
    modifier: Modifier,
) {
    Surface(
        modifier = modifier,
        color = MaterialTheme.colorScheme.surface.copy(alpha = 0.58f),
        shape = MaterialTheme.shapes.medium,
    ) {
        Column(
            modifier = Modifier.padding(horizontal = FishSpacing.sm, vertical = FishSpacing.xs),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Text(
                text = label,
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = TextAlign.Center,
            )
            Text(
                text = value,
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.Bold,
                textAlign = TextAlign.Center,
            )
        }
    }
}

@Composable
private fun MotorControls(
    id: String,
    title: String,
    direction: MoveDirection,
    driveEnabled: Boolean,
    connected: Boolean,
    reverseSupported: Boolean,
    mode: ControlMode,
    onMove: (MoveDirection) -> Unit,
) {
    val motorLabel = id.uppercase()
    ControlSectionLabel(
        title = title,
        supporting = if (mode == ControlMode.HOLD_TO_RUN) {
            "按住运行，松手仅停止 $motorLabel。"
        } else {
            "点击持续运行；停止键仅停止 $motorLabel。"
        },
    )
    Spacer(Modifier.height(FishSpacing.sm))
    AdaptiveChoicePair(
        first = { modifier ->
            DirectionButton(
                label = "$motorLabel 前进",
                icon = Icons.Default.ArrowUpward,
                selected = direction == MoveDirection.FORWARD,
                enabled = driveEnabled,
                mode = mode,
                direction = MoveDirection.FORWARD,
                onMove = onMove,
                modifier = modifier.heightIn(min = 60.dp).testTag("${id}_forward"),
            )
        },
        second = { modifier ->
            SelectableChoiceButton(
                label = "$motorLabel 停止",
                icon = Icons.Default.StopCircle,
                selected = direction == MoveDirection.STOP,
                enabled = connected,
                onClick = { onMove(MoveDirection.STOP) },
                modifier = modifier.heightIn(min = 60.dp).testTag("${id}_stop"),
            )
        },
    )
    Spacer(Modifier.height(FishSpacing.sm))
    DirectionButton(
        label = "$motorLabel 后退",
        icon = Icons.Default.ArrowDownward,
        selected = direction == MoveDirection.REVERSE,
        enabled = driveEnabled && reverseSupported,
        mode = mode,
        direction = MoveDirection.REVERSE,
        onMove = onMove,
        modifier = Modifier.fillMaxWidth().heightIn(min = 56.dp).testTag("${id}_reverse"),
    )
    if (!reverseSupported) {
        LockedFeatureNotice(text = "$motorLabel 后退未启用：请在设置中开启“允许电机反向”并保存。")
    }
}

@Composable
private fun ControlSectionLabel(
    title: String,
    supporting: String,
) {
    Text(
        text = title,
        style = MaterialTheme.typography.titleSmall,
        fontWeight = FontWeight.SemiBold,
    )
    Text(
        text = supporting,
        style = MaterialTheme.typography.bodySmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        modifier = Modifier.padding(top = FishSpacing.xxs),
    )
}

@Composable
private fun ControlModeRow(
    mode: ControlMode,
    accessibilityForcesExplicitMode: Boolean,
    onModeChanged: (ControlMode) -> Unit,
) {
    Surface(
        color = MaterialTheme.colorScheme.surfaceContainer,
        shape = MaterialTheme.shapes.medium,
    ) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(FishSpacing.sm),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(Modifier.weight(1f)) {
                Text(
                    text = if (mode == ControlMode.HOLD_TO_RUN) "松手即停" else "显式停止",
                    style = MaterialTheme.typography.titleSmall,
                )
                Text(
                    text = if (accessibilityForcesExplicitMode) {
                        "TalkBack 触摸探索已开启，自动使用显式停止模式。"
                    } else if (mode == ControlMode.HOLD_TO_RUN) {
                        "按住任一路运行，松开仅停止该路。"
                    } else {
                        "启动后持续运行，直到明确停止。"
                    },
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
            }
            Spacer(Modifier.width(FishSpacing.sm))
            Switch(
                checked = mode == ControlMode.HOLD_TO_RUN,
                enabled = !accessibilityForcesExplicitMode,
                onCheckedChange = {
                    onModeChanged(if (it) ControlMode.HOLD_TO_RUN else ControlMode.EXPLICIT_STOP)
                },
                modifier = Modifier.semantics { contentDescription = "松手即停模式" },
            )
        }
    }
}

@Composable
private fun DirectionButton(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    enabled: Boolean,
    mode: ControlMode,
    direction: MoveDirection,
    onMove: (MoveDirection) -> Unit,
    modifier: Modifier,
) {
    val interactionSource = remember { MutableInteractionSource() }
    val isPressed by interactionSource.collectIsPressedAsState()
    var wasPressed by remember { mutableStateOf(false) }

    LaunchedEffect(isPressed, mode, enabled) {
        // 一旦已开始按住运行，松手、控件被禁用或模式被切换都必须产生 STOP。
        // 此 STOP 只停止当前电机，ViewModel 保留另一路与舵机目标。
        if (wasPressed && (!isPressed || mode != ControlMode.HOLD_TO_RUN || !enabled)) {
            wasPressed = false
            onMove(MoveDirection.STOP)
        } else if (mode == ControlMode.HOLD_TO_RUN && enabled && isPressed && !wasPressed) {
            wasPressed = true
            onMove(direction)
        }
    }

    val borderColor = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outline
    OutlinedButton(
        onClick = {
            if (mode == ControlMode.EXPLICIT_STOP) {
                onMove(direction)
            }
        },
        enabled = enabled,
        interactionSource = interactionSource,
        colors = ButtonDefaults.outlinedButtonColors(
            containerColor = if (selected) MaterialTheme.colorScheme.primaryContainer else Color.Transparent,
            contentColor = if (selected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.primary,
        ),
        border = BorderStroke(if (selected) 2.dp else 1.dp, borderColor),
        modifier = modifier.semantics {
            stateDescription = if (selected) "当前目标" else "未选择"
            contentDescription = if (mode == ControlMode.HOLD_TO_RUN) "$label，按住运行，松开停止" else label
        },
    ) {
        Icon(icon, contentDescription = null)
        Spacer(Modifier.width(FishSpacing.xs))
        Text(label, modifier = Modifier.weight(1f), textAlign = TextAlign.Center)
        if (selected) {
            Spacer(Modifier.width(FishSpacing.xs))
            Icon(Icons.Default.CheckCircle, contentDescription = null, modifier = Modifier.size(18.dp))
        }
    }
}

@Composable
private fun TurnChoice(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier,
) {
    SelectableChoiceButton(
        label = label,
        icon = icon,
        selected = selected,
        enabled = enabled,
        onClick = onClick,
        modifier = modifier,
    )
}

@Composable
private fun SelectableChoiceButton(
    label: String,
    icon: ImageVector,
    selected: Boolean,
    enabled: Boolean,
    onClick: () -> Unit,
    modifier: Modifier,
) {
    val borderColor = if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.outlineVariant
    OutlinedButton(
        onClick = onClick,
        enabled = enabled,
        colors = ButtonDefaults.outlinedButtonColors(
            containerColor = if (selected) MaterialTheme.colorScheme.primaryContainer else Color.Transparent,
            contentColor = if (selected) MaterialTheme.colorScheme.onPrimaryContainer else MaterialTheme.colorScheme.onSurface,
        ),
        border = BorderStroke(if (selected) 2.dp else 1.dp, borderColor),
        modifier = modifier
            .heightIn(min = 56.dp)
            .semantics { stateDescription = if (selected) "当前目标" else "未选择" },
    ) {
        Icon(if (selected) Icons.Default.CheckCircle else icon, contentDescription = null)
        Spacer(Modifier.width(FishSpacing.xs))
        Text(label, textAlign = TextAlign.Center)
    }
}

@Composable
private fun AdaptiveChoicePair(
    first: @Composable (Modifier) -> Unit,
    second: @Composable (Modifier) -> Unit,
) {
    BoxWithConstraints(Modifier.fillMaxWidth()) {
        val stack = LocalDensity.current.fontScale >= 1.45f || maxWidth < 272.dp
        if (stack) {
            Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                first(Modifier.fillMaxWidth())
                second(Modifier.fillMaxWidth())
            }
        } else {
            Row(horizontalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                first(Modifier.weight(1f))
                second(Modifier.weight(1f))
            }
        }
    }
}

@Composable
private fun LockedFeatureNotice(text: String) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .padding(top = FishSpacing.sm),
        shape = MaterialTheme.shapes.small,
        color = MaterialTheme.colorScheme.surfaceContainer,
        contentColor = MaterialTheme.colorScheme.onSurfaceVariant,
    ) {
        Row(
            modifier = Modifier.padding(horizontal = FishSpacing.sm, vertical = FishSpacing.xs),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Icon(Icons.Default.Lock, contentDescription = null, modifier = Modifier.size(18.dp))
            Spacer(Modifier.width(FishSpacing.xs))
            Text(text, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun TargetCommandDetails(control: ControlUiState) {
    Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.xs)) {
        TargetCommandRow("M1 · 370", moveLabel(control.move))
        TargetCommandRow("M2 · N20", moveLabel(control.motor2))
        TargetCommandRow("转向", turnLabel(control.turn))
        TargetCommandRow("固件占空比", "两路固定 100%")
        TargetCommandRow("舵机角度", "${formatSigned(control.servoAngleDegrees)}°")
        Text(
            text = "这里显示待发送/已下达的目标，不代表传感器实测值；实际状态请查看“状态”页。",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.padding(top = FishSpacing.xs),
        )
    }
}

@Composable
private fun TargetCommandRow(label: String, value: String) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(
            text = label,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f),
        )
        Spacer(Modifier.width(FishSpacing.sm))
        Text(value, fontWeight = FontWeight.SemiBold, textAlign = TextAlign.End)
    }
}

@Composable
fun ControlSafetyStopBar(
    connected: Boolean,
    control: ControlUiState,
    reduceMotion: Boolean,
    onStop: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Surface(
        modifier = modifier,
        shape = MaterialTheme.shapes.large,
        color = MaterialTheme.colorScheme.surface,
        shadowElevation = 10.dp,
        tonalElevation = 4.dp,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant),
    ) {
        Button(
            onClick = onStop,
            colors = ButtonDefaults.buttonColors(
                containerColor = MaterialTheme.colorScheme.error,
                contentColor = MaterialTheme.colorScheme.onError,
            ),
            modifier = Modifier
                .fillMaxWidth()
                .heightIn(min = 72.dp)
                .padding(FishSpacing.xs)
                .testTag("emergency_stop")
                .semantics {
                    liveRegion = LiveRegionMode.Polite
                    role = Role.Button
                    contentDescription = when {
                        control.stopPending -> "正在发送安全停止"
                        control.move != MoveDirection.STOP || control.motor2 != MoveDirection.STOP -> "M1 ${moveLabel(control.move)}，M2 ${moveLabel(control.motor2)}，立即同时停止两路电机"
                        connected -> "立即安全停止 M1 和 M2，并按安全配置处理舵机"
                        else -> "当前未连接，保持本地停止并尝试通知设备"
                    }
                },
        ) {
            if (control.stopPending && !reduceMotion) {
                CircularProgressIndicator(
                    modifier = Modifier.size(24.dp),
                    color = MaterialTheme.colorScheme.onError,
                    strokeWidth = 2.dp,
                )
            } else if (control.stopPending) {
                Icon(Icons.Default.Info, contentDescription = null)
            } else {
                Icon(Icons.Default.StopCircle, contentDescription = null, modifier = Modifier.size(28.dp))
            }
            Spacer(Modifier.width(FishSpacing.sm))
            Column(Modifier.weight(1f)) {
                Text(
                    text = if (control.stopPending) "正在安全停止…" else "立即安全停止",
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                )
                Text(
                    text = if (connected) "同时停止 M1 / M2 · 等待 ACK" else "当前离线 · 无线停止可能无法送达",
                    style = MaterialTheme.typography.labelSmall,
                )
            }
        }
    }
}

private fun formatSigned(value: Int): String = if (value > 0) "+$value" else value.toString()

private fun turnForAngle(angle: Int): TurnDirection = when {
    angle < 0 -> TurnDirection.LEFT
    angle > 0 -> TurnDirection.RIGHT
    else -> TurnDirection.CENTER
}

private fun moveLabel(move: MoveDirection): String = when (move) {
    MoveDirection.FORWARD -> "前进"
    MoveDirection.REVERSE -> "后退"
    MoveDirection.STOP -> "停止"
}

private fun turnLabel(turn: TurnDirection): String = when (turn) {
    TurnDirection.LEFT -> "左转"
    TurnDirection.CENTER -> "直行"
    TurnDirection.RIGHT -> "右转"
}

private fun connectionLabel(phase: ConnectionPhase): String = when (phase) {
    ConnectionPhase.DISCONNECTED -> "未连接"
    ConnectionPhase.SCANNING -> "扫描中"
    ConnectionPhase.CONNECTING -> "连接中"
    ConnectionPhase.DISCONNECTING -> "安全断开中"
    ConnectionPhase.CONNECTED -> "已连接"
    ConnectionPhase.LOST -> "已失联"
    ConnectionPhase.RECONNECTING -> "重连中"
}

@Preview(name = "手机浅色", widthDp = 360, heightDp = 800, showBackground = true)
@Preview(name = "320dp / 200% 字体", widthDp = 320, heightDp = 720, fontScale = 2f, showBackground = true)
@Preview(
    name = "平板横屏深色",
    widthDp = 800,
    heightDp = 600,
    uiMode = Configuration.UI_MODE_NIGHT_YES,
    showBackground = true,
)
@Composable
private fun ControlScreenPreview() {
    BionicFishApp(
        state = AppUiState(
            initialDestination = AppDestination.CONTROL,
            connection = ConnectionUiState(
                phase = ConnectionPhase.CONNECTED,
                connectedDeviceName = "模拟仿生鱼",
            ),
            control = ControlUiState(
                enabled = true,
                move = MoveDirection.FORWARD,
                motor2 = MoveDirection.REVERSE,
                turn = TurnDirection.LEFT,
                stepSpeedRpm = 60,
                servoAngleDegrees = -15,
                reverseSupported = true,
                stepperParametersConfirmed = false,
            ),
        ),
        actions = UiActions(),
    )
}
