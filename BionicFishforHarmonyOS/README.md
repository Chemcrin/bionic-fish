# 仿生鱼 HarmonyOS 上位机

这是一个面向 HarmonyOS 设备的 ArkTS / Stage 模型上位机应用骨架，用于连接 ESP32-S3 WebSocket 网关，实现仿生鱼的实时数据收发、方向控制、参数下发和基础遥测显示。

## 设计目标

| 模块 | 设计 |
|---|---|
| 通信链路 | HarmonyOS App → WiFi AP → `ws://192.168.4.1/ws` → ESP32-S3 → UART → STM32 |
| 控制原则 | App 发送 `throttle`、`yaw`、`pitch`、`mode` 等高层运动意图，不直接下发舵机瞬时角度 |
| UI 风格 | ArkUI 原生组件、组件级毛玻璃、沉浸深色水下光感、连续脉冲动画、大触控面积 |
| 安全逻辑 | 固定急停入口、参数限幅、心跳、周期性控制帧、断连状态提示 |
| 后续扩展 | 通信层集中在 `FishSocketClient.ets`，可替换为 BLE、UDP 或自定义二进制协议 |

## 工程结构

| 路径 | 作用 |
|---|---|
| `AppScope/app.json5` | 应用级配置、包名、图标、应用名 |
| `entry/src/main/module.json5` | HAP 模块配置、入口 Ability、`ohos.permission.INTERNET` 权限 |
| `entry/src/main/ets/entryability/EntryAbility.ets` | Stage 模型 UIAbility 入口 |
| `entry/src/main/ets/pages/Index.ets` | 主控制台页面，包含连接、方向控制、遥测、参数下发 |
| `entry/src/main/ets/model/FishProtocol.ets` | JSON 协议模型、默认参数、限幅和帧构建 |
| `entry/src/main/ets/service/FishSocketClient.ets` | 官方 `@kit.NetworkKit` WebSocket 封装 |

## 默认连接

| 项目 | 值 |
|---|---|
| ESP32 热点 | `BionicFish_ESP32` |
| 默认 IP | `192.168.4.1` |
| WebSocket | `ws://192.168.4.1/ws` |
| 数据格式 | JSON over WebSocket |

## App 发送的数据帧

### 控制帧

```json
{
  "type": "control",
  "seq": 101,
  "mode": "manual",
  "throttle": 60,
  "yaw": 0,
  "pitch": 0,
  "depth_target": 0.3,
  "tail_amp": 45,
  "tail_freq": 1.6,
  "trim": 0,
  "emergency_stop": false,
  "timestamp": 12345678
}
```

### 参数帧

```json
{
  "type": "config",
  "seq": 201,
  "tail_amp_limit": 80,
  "tail_freq_limit": 3.5,
  "servo_trim_tail": 0,
  "servo_trim_left_fin": -2,
  "servo_trim_right_fin": 2,
  "timestamp": 12345678
}
```

### 心跳帧

```json
{
  "type": "heartbeat",
  "seq": 301,
  "timestamp": 12345678
}
```

## ESP32 需要回传的数据帧

```json
{
  "type": "telemetry",
  "seq": 501,
  "battery": 7.4,
  "rssi": -58,
  "mode": "manual",
  "depth": 0.35,
  "roll": 1.2,
  "pitch": -2.8,
  "yaw": 86.5,
  "stm32_online": true,
  "error": 0
}
```

关键指令建议回 ACK：

```json
{
  "type": "ack",
  "seq": 201,
  "ok": true,
  "message": "config applied"
}
```

## 构建与打包 `.hap`

| 步骤 | 操作 |
|---|---|
| 1 | 使用 DevEco Studio 打开 `BionicFishHarmony` 目录 |
| 2 | 安装或选择 HarmonyOS SDK API 12+，建议 HarmonyOS NEXT / API 12 或更高 |
| 3 | 在 `File > Project Structure > Signing Configs` 配置调试签名 |
| 4 | 等待 DevEco 同步 hvigor 工程 |
| 5 | 运行 `Build > Make Module 'entry'` 或 `Build Hap(s)/APP(s) > Build Hap(s)` |
| 6 | 生成的 `.hap` 通常位于 `entry/build/default/outputs/default/entry-default-unsigned.hap` 或对应签名输出目录 |

> 当前工作区没有 DevEco Studio / HarmonyOS SDK 构建链，因此这里提供的是可导入工程源码；实际 `.hap` 需要在安装 SDK 的环境中生成。

## 官方 API 使用点

| 能力 | API |
|---|---|
| Stage 模型入口 | `@kit.AbilityKit` 的 `UIAbility` |
| WebSocket | `@kit.NetworkKit` 的 `webSocket.createWebSocket()`、`connect()`、`send()`、`close()`、`on('open/message/close/error')` |
| 日志 | `@kit.PerformanceAnalysisKit` 的 `hilog` |
| 毛玻璃 | ArkUI 通用属性 `backgroundBlurStyle(BlurStyle.COMPONENT_THICK / COMPONENT_REGULAR)` |
| 动画 | ArkUI `animateTo`、`animation`、`scale`、`opacity` |

## 视觉与交互说明

| 区域 | 行为 |
|---|---|
| 顶栏 | 显示 App 名和固定急停按钮 |
| 连接卡片 | 编辑 WebSocket 地址、连接/断开、显示电池/深度/信号 |
| 模式切换 | 手动、巡航、调试、停止 |
| 方向控制 | 按住方向按钮发送控制量，松手自动回中 |
| 参数面板 | 调整目标深度、摆尾幅度、摆尾频率、舵机微调 |
| 遥测面板 | 显示横滚、俯仰、航向、错误码和 STM32 在线状态 |

## 兼容提醒

| 问题 | 处理 |
|---|---|
| SDK 对 `BlurStyle.COMPONENT_*` 报错 | 改为 `BlurStyle.Thick` / `BlurStyle.Regular`，官方同页文档也支持这些枚举 |
| ESP32 需要 WebSocket 子协议 | 可在 `FishSocketClient.ets` 的 `connect()` 选项中增加 `protocol: 'bionic-fish-json'` |
| 设备连接 ESP32 热点后无法上网 | 正常现象，WiFi AP 模式下手机网络会切到 ESP32 局域网 |
| 控制频率需要调整 | 修改 `FishSocketClient.ets` 中控制循环 `120ms` 和心跳 `1000ms` |
