package com.bionicfish.controller.ui.screens

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Devices
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.SignalWifi4Bar
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
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
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import com.bionicfish.controller.ui.components.ConnectionBadge
import com.bionicfish.controller.ui.components.EmptyState
import com.bionicfish.controller.ui.components.LoadingState
import com.bionicfish.controller.ui.components.ResponsivePair
import com.bionicfish.controller.ui.components.ScreenTitle
import com.bionicfish.controller.ui.components.ScrollableScreen
import com.bionicfish.controller.ui.components.SectionCard
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.ConnectionUiState
import com.bionicfish.controller.ui.model.DeviceUiModel
import com.bionicfish.controller.ui.model.HandshakePhase
import com.bionicfish.controller.ui.model.TransportKind
import com.bionicfish.controller.ui.model.UiActions
import com.bionicfish.controller.ui.theme.FishSpacing

@Composable
fun DevicesScreen(
    connection: ConnectionUiState,
    devices: List<DeviceUiModel>,
    reduceMotion: Boolean,
    actions: UiActions,
    contentPadding: PaddingValues,
) {
    var showDisconnectDialog by remember { mutableStateOf(false) }

    ScrollableScreen(contentPadding = contentPadding) {
        ScreenTitle(
            title = "设备",
            subtitle = "发现并验证仿生鱼连接，不会静默连接陌生设备。",
        )

        SectionCard(title = "连接状态") {
            ResponsivePair(
                first = { modifier ->
                    Column(modifier) {
                        ConnectionBadge(connection.phase)
                        if (connection.connectedDeviceName != null) {
                            Text(
                                text = connection.connectedDeviceName,
                                style = MaterialTheme.typography.titleMedium,
                                modifier = Modifier.padding(top = FishSpacing.xs),
                            )
                        }
                        if (connection.detail != null) {
                            Text(
                                text = connection.detail,
                                style = MaterialTheme.typography.bodySmall,
                                color = MaterialTheme.colorScheme.onSurfaceVariant,
                                modifier = Modifier.padding(top = FishSpacing.xxs),
                            )
                        }
                    }
                },
                second = { modifier ->
                    Column(modifier) {
                        Text(
                            text = handshakeLabel(connection.handshake),
                            style = MaterialTheme.typography.labelLarge,
                        )
                        Text(
                            text = "协议版本：${connection.protocolVersion ?: "待验证"}",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                        Text(
                            text = "设备类型：${connection.deviceType ?: "待验证"}",
                            style = MaterialTheme.typography.bodyMedium,
                        )
                    }
                },
            )

            if (connection.phase == ConnectionPhase.LOST || connection.phase == ConnectionPhase.DISCONNECTED) {
                OutlinedButton(
                    onClick = actions.onRetryConnection,
                    enabled = connection.selectedDeviceId != null,
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = FishSpacing.sm)
                        .heightIn(min = FishSpacing.minimumTouchTarget),
                ) {
                    Text("重试上次连接")
                }
            }
            if (connection.phase in setOf(
                    ConnectionPhase.CONNECTED,
                    ConnectionPhase.CONNECTING,
                    ConnectionPhase.LOST,
                    ConnectionPhase.RECONNECTING,
                )
            ) {
                OutlinedButton(
                    onClick = { showDisconnectDialog = true },
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(top = FishSpacing.sm)
                        .heightIn(min = FishSpacing.minimumTouchTarget),
                ) {
                    Text("断开连接")
                }
            }
        }

        SectionCard(title = "传输方式") {
            TransportChoice(
                kind = TransportKind.WIFI_TCP,
                selected = connection.transport == TransportKind.WIFI_TCP,
                label = "Wi-Fi TCP",
                description = "通过局域网连接 ESP-01S；IP 和端口须在设置页确认。",
                icon = Icons.Default.Wifi,
                enabled = connection.phase == ConnectionPhase.DISCONNECTED,
                onClick = { actions.onSelectTransport(TransportKind.WIFI_TCP) },
            )
            TransportChoice(
                kind = TransportKind.WIFI_UDP,
                selected = connection.transport == TransportKind.WIFI_UDP,
                label = "Wi-Fi UDP",
                description = "仅在确认 ESP 固件使用 UDP 时选择。",
                icon = Icons.Default.SignalWifi4Bar,
                enabled = connection.phase == ConnectionPhase.DISCONNECTED,
                onClick = { actions.onSelectTransport(TransportKind.WIFI_UDP) },
            )
            if (connection.bluetoothHardwareAvailable) {
                TransportChoice(
                    kind = TransportKind.BLUETOOTH_CLASSIC,
                    selected = connection.transport == TransportKind.BLUETOOTH_CLASSIC,
                    label = "经典蓝牙",
                    description = "仅供后续实际蓝牙硬件；ESP-01S 本身不支持。",
                    icon = Icons.Default.Bluetooth,
                    enabled = connection.phase == ConnectionPhase.DISCONNECTED,
                    onClick = { actions.onSelectTransport(TransportKind.BLUETOOTH_CLASSIC) },
                )
                TransportChoice(
                    kind = TransportKind.BLUETOOTH_LE,
                    selected = connection.transport == TransportKind.BLUETOOTH_LE,
                    label = "低功耗蓝牙",
                    description = "需要额外 BLE 硬件和已确认 UUID。",
                    icon = Icons.Default.Bluetooth,
                    enabled = connection.phase == ConnectionPhase.DISCONNECTED,
                    onClick = { actions.onSelectTransport(TransportKind.BLUETOOTH_LE) },
                )
            }
            TransportChoice(
                kind = TransportKind.SIMULATOR,
                selected = connection.transport == TransportKind.SIMULATOR,
                label = "模拟设备",
                description = "无硬件时演练控制、状态回传和异常处理。",
                icon = Icons.Default.Memory,
                enabled = connection.phase == ConnectionPhase.DISCONNECTED,
                onClick = { actions.onSelectTransport(TransportKind.SIMULATOR) },
            )
        }

        SectionCard(title = "发现的设备") {
            val scanning = connection.phase == ConnectionPhase.SCANNING
            Button(
                onClick = if (scanning) actions.onCancelScan else actions.onStartScan,
                // 活动会话中扫描会中断周期保活；必须先通过“安全断开”结束旧会话。
                enabled = scanning || connection.phase == ConnectionPhase.DISCONNECTED,
                modifier = Modifier
                    .fillMaxWidth()
                    .heightIn(min = FishSpacing.minimumTouchTarget),
            ) {
                if (scanning) {
                    if (reduceMotion) {
                        Icon(Icons.Default.Devices, contentDescription = null)
                    } else {
                        CircularProgressIndicator(
                            modifier = Modifier.size(20.dp),
                            strokeWidth = 2.dp,
                            color = MaterialTheme.colorScheme.onPrimary,
                        )
                    }
                    Spacer(Modifier.width(FishSpacing.xs))
                    Text("取消扫描")
                } else {
                    Icon(Icons.Default.Devices, contentDescription = null)
                    Spacer(Modifier.width(FishSpacing.xs))
                    Text(if (connection.phase == ConnectionPhase.DISCONNECTED) "扫描设备" else "请先安全断开")
                }
            }

            if (scanning) {
                LoadingState("正在扫描，可随时取消…", reduceMotion = reduceMotion)
            }
            if (devices.isEmpty() && !scanning) {
                EmptyState(
                    title = "暂未发现设备",
                    message = "确认手机与 ESP 处于同一网络，或使用模拟设备。",
                    actionLabel = "开始扫描",
                    onAction = actions.onStartScan,
                )
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                    devices.forEach { device ->
                        DeviceCard(
                            device = device,
                            connected = connection.phase == ConnectionPhase.CONNECTED && connection.selectedDeviceId == device.id,
                            sessionActive = connection.phase != ConnectionPhase.DISCONNECTED,
                            reduceMotion = reduceMotion,
                            onConnect = { actions.onConnect(device.id) },
                            onForget = { actions.onForgetDevice(device.id) },
                        )
                    }
                }
            }
        }
    }

    if (showDisconnectDialog) {
        AlertDialog(
            onDismissRequest = { showDisconnectDialog = false },
            title = { Text("断开仿生鱼？") },
            text = { Text("应用将先尝试发送安全停止帧，再断开当前链路。") },
            confirmButton = {
                Button(
                    onClick = {
                        showDisconnectDialog = false
                        actions.onDisconnect()
                    },
                ) { Text("安全断开") }
            },
            dismissButton = {
                TextButton(onClick = { showDisconnectDialog = false }) { Text("取消") }
            },
        )
    }
}

@Composable
private fun TransportChoice(
    kind: TransportKind,
    selected: Boolean,
    label: String,
    description: String,
    icon: ImageVector,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    FilterChip(
        selected = selected,
        onClick = onClick,
        enabled = enabled,
        label = {
            Column(Modifier.padding(vertical = FishSpacing.xxs)) {
                Text(label, style = MaterialTheme.typography.labelLarge)
                Text(description, style = MaterialTheme.typography.bodySmall)
            }
        },
        leadingIcon = {
            Icon(
                imageVector = if (selected) Icons.Default.Check else icon,
                contentDescription = null,
            )
        },
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = FishSpacing.minimumTouchTarget)
            .semantics { contentDescription = "$label 传输方式" },
    )
}

@Composable
private fun DeviceCard(
    device: DeviceUiModel,
    connected: Boolean,
    sessionActive: Boolean,
    reduceMotion: Boolean,
    onConnect: () -> Unit,
    onForget: () -> Unit,
) {
    Card(Modifier.fillMaxWidth()) {
        Column(Modifier.padding(FishSpacing.md)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Column(Modifier.weight(1f)) {
                    Text(device.name, style = MaterialTheme.typography.titleMedium, fontWeight = FontWeight.SemiBold)
                    Text(device.address, style = MaterialTheme.typography.bodyMedium)
                }
                if (device.isSaved) {
                    Text("已保存", style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.primary)
                }
            }
            Text(
                text = buildString {
                    append("信号：")
                    append(device.signalDbm?.let { "$it dBm" } ?: "未知")
                    append(" · 发现于 ")
                    append(device.discoveredAt)
                },
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = FishSpacing.xxs),
            )
            Text(
                text = "连接状态：${when {
                    connected -> "已连接"
                    device.isConnecting -> "连接中"
                    sessionActive -> "请先断开当前设备"
                    else -> "可连接"
                }}",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            BoxWithConstraints(Modifier.fillMaxWidth().padding(top = FishSpacing.sm)) {
                if (maxWidth >= 360.dp) {
                    Row(horizontalArrangement = Arrangement.spacedBy(FishSpacing.xs)) {
                        DeviceConnectButton(
                            device,
                            connected,
                            sessionActive,
                            reduceMotion,
                            onConnect,
                            Modifier.weight(1f),
                        )
                        if (device.isSaved) {
                            OutlinedButton(
                                onClick = onForget,
                                modifier = Modifier.weight(1f).heightIn(min = FishSpacing.minimumTouchTarget),
                            ) { Text("忘记设备") }
                        }
                    }
                } else {
                    Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.xs)) {
                        DeviceConnectButton(
                            device,
                            connected,
                            sessionActive,
                            reduceMotion,
                            onConnect,
                            Modifier.fillMaxWidth(),
                        )
                        if (device.isSaved) {
                            OutlinedButton(
                                onClick = onForget,
                                modifier = Modifier.fillMaxWidth().heightIn(min = FishSpacing.minimumTouchTarget),
                            ) { Text("忘记设备") }
                        }
                    }
                }
            }
        }
    }
}

@Composable
private fun DeviceConnectButton(
    device: DeviceUiModel,
    connected: Boolean,
    sessionActive: Boolean,
    reduceMotion: Boolean,
    onConnect: () -> Unit,
    modifier: Modifier,
) {
    Button(
        onClick = onConnect,
        enabled = !device.isConnecting && !sessionActive,
        modifier = modifier.heightIn(min = FishSpacing.minimumTouchTarget),
    ) {
        if (device.isConnecting) {
            if (reduceMotion) {
                Icon(Icons.Default.Devices, contentDescription = null)
            } else {
                CircularProgressIndicator(
                    modifier = Modifier.size(18.dp),
                    color = MaterialTheme.colorScheme.onPrimary,
                    strokeWidth = 2.dp,
                )
            }
            Spacer(Modifier.width(FishSpacing.xs))
        }
        Text(
            when {
                connected -> "已连接"
                device.isConnecting -> "连接中"
                sessionActive -> "请先安全断开"
                else -> "连接"
            },
        )
    }
}

private fun handshakeLabel(handshake: HandshakePhase): String = when (handshake) {
    HandshakePhase.NOT_STARTED -> "握手：未开始"
    HandshakePhase.VERIFYING -> "握手：验证中"
    HandshakePhase.VERIFIED -> "握手：已验证"
    HandshakePhase.FAILED -> "握手：失败，请重试"
}
