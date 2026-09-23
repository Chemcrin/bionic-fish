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
import androidx.compose.material.icons.filled.Gamepad
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
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.LiveRegionMode
import androidx.compose.ui.semantics.liveRegion
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.bionicfish.controller.settings.ApDirectDefaults
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
import com.bionicfish.controller.ui.model.SettingsUiState
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
    settings: SettingsUiState = SettingsUiState(),
    onOpenControl: () -> Unit = {},
) {
    var showDisconnectDialog by remember { mutableStateOf(false) }
    val discoveredDevices = devices.filterNot(DeviceUiModel::isApDirect)

    ScrollableScreen(contentPadding = contentPadding) {
        ScreenTitle(
            title = "设备",
            subtitle = "直连仿生鱼热点，或使用局域网与模拟设备。",
        )

        ApDirectCard(
            connection = connection,
            settings = settings,
            actions = actions,
            onOpenControl = onOpenControl,
        )

        SectionCard(title = "连接状态") {
            ResponsivePair(
                first = { modifier ->
                    Column(modifier) {
                        ConnectionBadge(connection.phase)
                        if (connection.isApDirect) {
                            Text(
                                text = "AP 直连 · TCP",
                                style = MaterialTheme.typography.labelLarge,
                                modifier = Modifier.padding(top = FishSpacing.xs),
                            )
                        }
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
                selected = connection.transport == TransportKind.WIFI_TCP && !connection.isApDirect,
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
            if (discoveredDevices.isEmpty() && !scanning) {
                if (connection.phase == ConnectionPhase.DISCONNECTED) {
                    EmptyState(
                        title = "暂未发现设备",
                        message = "AP 直连无需扫描，请使用页面上方入口；也可扫描已配置的局域网端点或模拟设备。",
                        actionLabel = "开始扫描",
                        onAction = actions.onStartScan,
                    )
                } else {
                    Text("当前没有其他发现结果。请先安全断开，再扫描新设备。")
                }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(FishSpacing.sm)) {
                    discoveredDevices.forEach { device ->
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
private fun ApDirectCard(
    connection: ConnectionUiState,
    settings: SettingsUiState,
    actions: UiActions,
    onOpenControl: () -> Unit,
) {
    val canStartSession = connection.phase == ConnectionPhase.DISCONNECTED ||
        connection.phase == ConnectionPhase.LOST
    val apConnected = connection.isApDirect && connection.phase == ConnectionPhase.CONNECTED
    SectionCard(title = "AP 直连 · 仿生鱼热点") {
        Text(
            text = "Wi-Fi：${settings.apSsid}",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.SemiBold,
        )
        Text(
            text = "TCP ${settings.apHost}:${settings.apPort} · 无需扫描",
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (connection.isApDirect) {
            Column(
                verticalArrangement = Arrangement.spacedBy(FishSpacing.xxs),
                modifier = Modifier.padding(top = FishSpacing.xs)
                    .testTag("ap_connection_summary")
                    .semantics { liveRegion = LiveRegionMode.Polite },
            ) {
                ConnectionBadge(connection.phase)
                Text(
                    text = apConnectionSummary(connection),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (connection.detail != null && (
                        connection.phase == ConnectionPhase.LOST ||
                            connection.phase == ConnectionPhase.DISCONNECTED ||
                            connection.handshake == HandshakePhase.FAILED
                        )
                ) {
                    Text(
                        text = connection.detail,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.error,
                        // 完整原因仍在下方“连接状态”；首卡保留可操作摘要及连接入口。
                        maxLines = 4,
                        overflow = TextOverflow.Ellipsis,
                    )
                }
            }
        }
        Column(
            verticalArrangement = Arrangement.spacedBy(FishSpacing.xs),
            modifier = Modifier.padding(top = FishSpacing.sm),
        ) {
            OutlinedButton(
                onClick = actions.onOpenWifiSettings,
                enabled = canStartSession,
                modifier = Modifier.fillMaxWidth()
                    .heightIn(min = FishSpacing.minimumTouchTarget)
                    .testTag("ap_wifi_settings"),
            ) {
                Icon(Icons.Default.Wifi, contentDescription = null)
                Spacer(Modifier.width(FishSpacing.xs))
                Text("打开系统 Wi-Fi 设置")
            }
            Button(
                onClick = actions.onConnectApDirect,
                enabled = canStartSession,
                modifier = Modifier.fillMaxWidth()
                    .heightIn(min = FishSpacing.minimumTouchTarget)
                    .testTag("ap_connect"),
            ) {
                Icon(Icons.Default.SignalWifi4Bar, contentDescription = null)
                Spacer(Modifier.width(FishSpacing.xs))
                Text(
                    when {
                        !connection.isApDirect -> "连接仿生鱼热点"
                        connection.phase == ConnectionPhase.CONNECTING &&
                            connection.handshake == HandshakePhase.VERIFYING -> "正在验证 STM32…"
                        connection.phase == ConnectionPhase.CONNECTING -> "正在连接仿生鱼…"
                        connection.phase == ConnectionPhase.RECONNECTING -> "正在重连仿生鱼…"
                        connection.phase == ConnectionPhase.DISCONNECTING -> "正在安全断开…"
                        apConnected -> "仿生鱼热点已连接"
                        connection.phase == ConnectionPhase.LOST -> "重新连接仿生鱼热点"
                        else -> "连接仿生鱼热点"
                    },
                )
            }
            if (apConnected) {
                Button(
                    onClick = onOpenControl,
                    modifier = Modifier.fillMaxWidth()
                        .heightIn(min = FishSpacing.minimumTouchTarget)
                        .testTag("ap_open_control"),
                ) {
                    Icon(Icons.Default.Gamepad, contentDescription = null)
                    Spacer(Modifier.width(FishSpacing.xs))
                    Text("进入控制页")
                }
            }
        }
        Text(
            text = "先在系统中选择 ${settings.apSsid}，再返回连接。初始密码 ${ApDirectDefaults.PASSWORD}，修改后以实际配置为准。",
            style = MaterialTheme.typography.bodySmall,
            modifier = Modifier.padding(top = FishSpacing.xs),
        )
        Text(
            text = "热点无互联网属正常，请选择“保留连接”；如手机自动切网，请关闭自动切换网络。应用不会自动确认热点名称。",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        if (!canStartSession && !apConnected) {
            Text(
                text = "正在扫描或已有连接，请先取消扫描或安全断开，再切换热点。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

private fun apConnectionSummary(connection: ConnectionUiState): String = when {
    connection.handshake == HandshakePhase.FAILED -> "未收到有效的 STM32 应答，请检查鱼端供电与串口桥接。"
    connection.handshake == HandshakePhase.VERIFYING -> "正在等待 STM32 安全停止应答，完成验证后即可控制。"
    connection.phase == ConnectionPhase.CONNECTING -> "正在通过手机已选择的 Wi-Fi 建立 TCP 连接。"
    connection.phase == ConnectionPhase.RECONNECTING -> "正在重建 TCP 连接；请让手机保留在仿生鱼热点。"
    connection.phase == ConnectionPhase.CONNECTED -> "TCP 已连接，可进入控制页查看状态与控制仿生鱼。"
    connection.phase == ConnectionPhase.DISCONNECTING -> "正在尝试发送安全停止帧并结束当前连接。"
    connection.phase == ConnectionPhase.LOST -> "链路已失联，请检查热点连接与鱼端供电后重试。"
    else -> "请确认手机已选择上方热点，再发起 TCP 连接。"
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
