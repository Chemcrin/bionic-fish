package com.bionicfish.controller.ui.screens

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Bluetooth
import androidx.compose.material.icons.filled.Check
import androidx.compose.material.icons.filled.Info
import androidx.compose.material.icons.filled.Memory
import androidx.compose.material.icons.filled.Save
import androidx.compose.material.icons.filled.Warning
import androidx.compose.material.icons.filled.Wifi
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.input.KeyboardType
import com.bionicfish.controller.ui.components.ResponsivePair
import com.bionicfish.controller.ui.components.ScreenTitle
import com.bionicfish.controller.ui.components.ScrollableScreen
import com.bionicfish.controller.ui.components.SectionCard
import com.bionicfish.controller.ui.model.SettingsUiState
import com.bionicfish.controller.ui.model.ConnectionPhase
import com.bionicfish.controller.ui.model.TransportKind
import com.bionicfish.controller.ui.model.UiActions
import com.bionicfish.controller.ui.theme.FishSpacing

@Composable
fun SettingsScreen(
    settings: SettingsUiState,
    connectionPhase: ConnectionPhase,
    bluetoothHardwareAvailable: Boolean,
    actions: UiActions,
    contentPadding: PaddingValues,
) {
    val connectionSettingsEnabled = connectionPhase == ConnectionPhase.DISCONNECTED
    ScrollableScreen(contentPadding = contentPadding) {
        ScreenTitle(
            title = "设置",
            subtitle = "AP 预置来自联调指引；其他连接参数按实际固件填写。",
        )

        SectionCard(title = "AP 直连参数") {
            Text(
                text = "连接手机已选择的仿生鱼热点，使用 TCP 客户端；这是独立预置，不覆盖下方的局域网配置。",
                style = MaterialTheme.typography.bodyMedium,
            )
            OutlinedTextField(
                value = settings.apSsid,
                onValueChange = actions.onApSsidChanged,
                enabled = connectionSettingsEnabled,
                label = { Text("热点名称 (SSID)") },
                singleLine = true,
                modifier = Modifier.fillMaxWidth().testTag("ap_ssid_input"),
            )
            ResponsivePair(
                first = { modifier ->
                    OutlinedTextField(
                        value = settings.apHost,
                        onValueChange = actions.onApHostChanged,
                        enabled = connectionSettingsEnabled,
                        label = { Text("AP IPv4 地址") },
                        singleLine = true,
                        modifier = modifier.testTag("ap_host_input"),
                    )
                },
                second = { modifier ->
                    OutlinedTextField(
                        value = settings.apPort,
                        onValueChange = { value ->
                            if (value.all { it in '0'..'9' }) actions.onApPortChanged(value)
                        },
                        enabled = connectionSettingsEnabled,
                        label = { Text("AP TCP 端口") },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        singleLine = true,
                        modifier = modifier.testTag("ap_port_input"),
                    )
                },
            )
            Text(
                text = "修改后点击页面底部“保存设置”。地址必须是 IPv4，端口为 1–65535；热点名称用于操作引导，不代表应用已识别当前 SSID。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            if (!connectionSettingsEnabled) {
                Text("请先安全断开，再编辑 AP 直连参数。", style = MaterialTheme.typography.bodySmall)
            }
        }

        SectionCard(title = "运行模式") {
            SettingsTransportChip(
                selected = settings.selectedTransport == TransportKind.WIFI_TCP,
                label = "Wi-Fi TCP",
                description = "局域网 TCP；AP 热点请使用设备页的独立直连入口。",
                icon = Icons.Default.Wifi,
                enabled = connectionSettingsEnabled,
                onClick = { actions.onSelectTransport(TransportKind.WIFI_TCP) },
            )
            SettingsTransportChip(
                selected = settings.selectedTransport == TransportKind.WIFI_UDP,
                label = "Wi-Fi UDP",
                description = "历史扩展，当前 ESP-01S 交付仅支持 TCP。",
                icon = Icons.Default.Wifi,
                enabled = false,
                onClick = { actions.onSelectTransport(TransportKind.WIFI_UDP) },
            )
            if (bluetoothHardwareAvailable) {
                SettingsTransportChip(
                    selected = settings.selectedTransport == TransportKind.BLUETOOTH_CLASSIC,
                    label = "经典蓝牙",
                    description = "仅额外蓝牙硬件可用时显示；ESP-01S 不提供蓝牙。",
                    icon = Icons.Default.Bluetooth,
                    enabled = connectionSettingsEnabled,
                    onClick = { actions.onSelectTransport(TransportKind.BLUETOOTH_CLASSIC) },
                )
                SettingsTransportChip(
                    selected = settings.selectedTransport == TransportKind.BLUETOOTH_LE,
                    label = "低功耗蓝牙",
                    description = "需要实际 BLE 硬件与已确认的 UUID。",
                    icon = Icons.Default.Bluetooth,
                    enabled = connectionSettingsEnabled,
                    onClick = { actions.onSelectTransport(TransportKind.BLUETOOTH_LE) },
                )
            }
            SettingsTransportChip(
                selected = settings.selectedTransport == TransportKind.SIMULATOR,
                label = "模拟设备",
                description = "与真实传输共用协议层，不向硬件发送数据。",
                icon = Icons.Default.Memory,
                enabled = connectionSettingsEnabled,
                onClick = { actions.onSelectTransport(TransportKind.SIMULATOR) },
            )
            SettingSwitch(
                title = "启用模拟数据",
                description = "演示连接、姿态、过期数据和故障状态。",
                checked = settings.simulatorEnabled,
                enabled = connectionSettingsEnabled,
                onCheckedChange = actions.onSimulatorEnabledChanged,
            )
        }

        SectionCard(title = "局域网 Wi-Fi 连接参数") {
            PendingParameterNotice(settings.connectionParametersConfirmed)
            Spacer(Modifier.height(FishSpacing.sm))
            ResponsivePair(
                first = { modifier ->
                    OutlinedTextField(
                        value = settings.wifiHost,
                        onValueChange = actions.onWifiHostChanged,
                        label = { Text("IP / 主机名") },
                        placeholder = { Text("待确认") },
                        singleLine = true,
                        modifier = modifier,
                    )
                },
                second = { modifier ->
                    OutlinedTextField(
                        value = settings.wifiPort,
                        onValueChange = { value ->
                            if (value.all(Char::isDigit)) actions.onWifiPortChanged(value)
                        },
                        label = { Text("端口") },
                        placeholder = { Text("待确认") },
                        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                        singleLine = true,
                        modifier = modifier,
                    )
                },
            )
            Text(
                text = "局域网地址和端口由用户确认，与上方 AP 直连预置分别保存。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = FishSpacing.xs),
            )
        }

        if (settings.selectedTransport == TransportKind.WIFI_UDP) {
            SectionCard(title = "UDP 设备发现") {
                Text(
                    text = "发现地址、端口和请求载荷必须来自 ESP 固件定义，当前均为待确认项。",
                    style = MaterialTheme.typography.bodyMedium,
                )
                Spacer(Modifier.height(FishSpacing.sm))
                ResponsivePair(
                    first = { modifier ->
                        OutlinedTextField(
                            value = settings.discoveryAddress,
                            onValueChange = actions.onDiscoveryAddressChanged,
                            label = { Text("发现地址") },
                            placeholder = { Text("待确认") },
                            singleLine = true,
                            modifier = modifier,
                        )
                    },
                    second = { modifier ->
                        OutlinedTextField(
                            value = settings.discoveryPort,
                            onValueChange = { value ->
                                if (value.all(Char::isDigit)) actions.onDiscoveryPortChanged(value)
                            },
                            label = { Text("发现端口") },
                            placeholder = { Text("待确认") },
                            keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
                            singleLine = true,
                            modifier = modifier,
                        )
                    },
                )
                Spacer(Modifier.height(FishSpacing.sm))
                OutlinedTextField(
                    value = settings.discoveryPayload,
                    onValueChange = actions.onDiscoveryPayloadChanged,
                    label = { Text("发现请求载荷") },
                    placeholder = { Text("待确认") },
                    minLines = 2,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        if (bluetoothHardwareAvailable && settings.selectedTransport == TransportKind.BLUETOOTH_CLASSIC) {
            SectionCard(title = "经典蓝牙参数") {
                OutlinedTextField(
                    value = settings.bluetoothClassicUuid,
                    onValueChange = actions.onBluetoothClassicUuidChanged,
                    label = { Text("经典蓝牙 Service UUID") },
                    placeholder = { Text("待确认") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        if (bluetoothHardwareAvailable && settings.selectedTransport == TransportKind.BLUETOOTH_LE) {
            SectionCard(title = "低功耗蓝牙参数") {
                OutlinedTextField(
                    value = settings.bluetoothLeServiceUuid,
                    onValueChange = actions.onBluetoothLeServiceUuidChanged,
                    label = { Text("BLE Service UUID") },
                    placeholder = { Text("待确认") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
                Spacer(Modifier.height(FishSpacing.sm))
                OutlinedTextField(
                    value = settings.bluetoothLeCharacteristicUuid,
                    onValueChange = actions.onBluetoothLeCharacteristicUuidChanged,
                    label = { Text("BLE Characteristic UUID") },
                    placeholder = { Text("待确认") },
                    singleLine = true,
                    modifier = Modifier.fillMaxWidth(),
                )
            }
        }

        SectionCard(title = "协议节奏与超时") {
            NumericSetting(
                label = "控制帧周期 (ms)",
                value = settings.controlPeriodMs,
                onValueChange = actions.onControlPeriodChanged,
            )
            Spacer(Modifier.height(FishSpacing.sm))
            NumericSetting(
                label = "心跳周期 (ms)",
                value = settings.heartbeatPeriodMs,
                onValueChange = actions.onHeartbeatPeriodChanged,
            )
            Spacer(Modifier.height(FishSpacing.sm))
            NumericSetting(
                label = "失联超时 (ms)",
                value = settings.linkTimeoutMs,
                onValueChange = actions.onLinkTimeoutChanged,
            )
            Text(
                text = "当前 STM32 V1 的命令失联阈值为 1000 ms；控制帧和心跳周期被限制在 750 ms 以内并保留安全余量。最终参数仍需与 ESP 实测。",
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                modifier = Modifier.padding(top = FishSpacing.xs),
            )
        }

        SectionCard(title = "安全策略") {
            SettingSwitch(
                title = "后台/退出控制页时发送安全停止",
                description = "发送失败时显示明确告警，不隐藏风险。",
                checked = settings.sendSafeStopOnBackground,
                onCheckedChange = actions.onSafeStopOnBackgroundChanged,
            )
            SettingSwitch(
                title = "失联安全停止时舵机回中",
                description = "关闭时由固件配置决定是否保持当前角度。",
                checked = settings.centerServoOnSafeStop,
                onCheckedChange = actions.onCenterServoOnSafeStopChanged,
            )
            SettingSwitch(
                title = "减少动态效果",
                description = "停用非必要过渡，同时尊重系统的减少动画设置。",
                checked = settings.reduceMotion,
                onCheckedChange = actions.onReduceMotionChanged,
            )
        }

        SectionCard(title = "权限与隐私") {
            Text("· 当前 Wi-Fi 实现只使用用户填写的地址或 UDP 应用层探测，不读取系统 Wi-Fi 扫描结果，因此不申请运行时位置/附近设备权限。")
            Text("· 鱼端未确认蓝牙硬件，蓝牙入口保持不可用，也不会申请蓝牙运行时权限。")
            Text("· 若后续实现系统 Wi-Fi 扫描或接入真实蓝牙模块，必须按 Android 版本另行加入按需授权流程。")
            Text("· 通信日志仅保存在本机，由用户主动导出，不默认上传。")
        }

        Button(
            onClick = actions.onSaveSettings,
            modifier = Modifier.fillMaxWidth().heightIn(min = FishSpacing.minimumTouchTarget),
        ) {
            Icon(Icons.Default.Save, contentDescription = null)
            Spacer(Modifier.width(FishSpacing.xs))
            Text("保存设置")
        }
    }
}

@Composable
private fun SettingsTransportChip(
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
        leadingIcon = { Icon(if (selected) Icons.Default.Check else icon, contentDescription = null) },
        modifier = Modifier.fillMaxWidth().heightIn(min = FishSpacing.minimumTouchTarget),
    )
}

@Composable
private fun PendingParameterNotice(confirmed: Boolean) {
    Card(
        colors = androidx.compose.material3.CardDefaults.cardColors(
            containerColor = if (confirmed) {
                MaterialTheme.colorScheme.primaryContainer
            } else {
                MaterialTheme.colorScheme.tertiaryContainer
            },
        ),
        modifier = Modifier.fillMaxWidth(),
    ) {
        Row(Modifier.padding(FishSpacing.sm), verticalAlignment = Alignment.CenterVertically) {
            Icon(
                imageVector = if (confirmed) Icons.Default.Info else Icons.Default.Warning,
                contentDescription = null,
            )
            Spacer(Modifier.width(FishSpacing.xs))
            Text(if (confirmed) "连接参数已确认" else "连接参数待确认")
        }
    }
}

@Composable
private fun NumericSetting(
    label: String,
    value: String,
    onValueChange: (String) -> Unit,
) {
    OutlinedTextField(
        value = value,
        onValueChange = { newValue ->
            if (newValue.all(Char::isDigit)) onValueChange(newValue)
        },
        label = { Text(label) },
        placeholder = { Text("待确认") },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Number),
        singleLine = true,
        modifier = Modifier.fillMaxWidth(),
    )
}

@Composable
private fun SettingSwitch(
    title: String,
    description: String,
    checked: Boolean,
    enabled: Boolean = true,
    onCheckedChange: (Boolean) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .heightIn(min = FishSpacing.minimumTouchTarget)
            .padding(vertical = FishSpacing.xs),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Column(Modifier.weight(1f)) {
            Text(title, style = MaterialTheme.typography.titleSmall)
            Text(
                description,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        Spacer(Modifier.width(FishSpacing.sm))
        Switch(
            checked = checked,
            enabled = enabled,
            onCheckedChange = onCheckedChange,
            modifier = Modifier.semantics { contentDescription = title },
        )
    }
}
