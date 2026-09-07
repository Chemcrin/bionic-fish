# AP 直连控制 —— 安卓上位机修改指引（给 GPT 的改动任务书）

> 目标：让「手机直连仿生鱼自身热点（ESP-01S AP）」成为一等公民的连接方式，
> 而不是只能"手机与设备在同一局域网（路由器）下手动填 IP"。
> 本文基于 2026-09-06 实测联调事实编写，供 GPT 定位并修改本安卓工程。

---

## 0. 结论先行（GPT 请先读这一节）

当前端到端链路是**「手机 = TCP 客户端 → 192.168.4.1:9000」**，协议是 **TCP 字节流承载 V1 ASCII 帧**。
在 AP 直连下：

- **不要**依赖"扫描发现设备"——当前 STM32/ESP 侧**没有 UDP 发现应答服务**，`UdpDiscoveryScanner`
  扫描不到任何真实设备，只能返回"已保存端点"。
- **不要**改传输/编解码层——现有 `TcpTransport` + 帧重组已完全够用，AP 直连与"同局域网"
  在传输层**没有任何区别**，区别只在：①手机当前连的是哪个 Wi-Fi（ESP 热点）；②连接目标固定为
  ESP 热点网关 `192.168.4.1:9000`；③UI/流程把"直连热点"做成一步操作。
- **必须**处理安卓"连上无外网热点会被自动切走/提示无互联网"的问题，否则链路连上又断。

所以这次改动的实质 = **新增一个"AP 直连"连接模式/流程 + Wi-Fi 前置处理 + 固定端点预置**，
不是重写网络栈。

---

## 1. 联调确定的硬件/固件事实（改动的前提）

| 项 | 值 | 说明 |
|---|---|---|
| 模块 | ESP-01S（原厂 AT 固件 1.7.4） | 或后续刷"透明 TCP 桥"自定义固件 |
| ESP 工作模式 | **SoftAP + TCP 服务器** | 热点 SSID `BionicFish-AP` / 密码 `12345678` |
| TCP 服务 | **192.168.4.1:9000** | ESP AP 网关地址固定为 192.168.4.1 |
| ESP↔STM32 | USART2，115200 | STM32 侧已实现 AT/`+IPD` 帧化（route B）；两桥接方式最终对手机暴露的都是 TCP 9000 |
| 协议 | V1 ASCII，`\n` 分帧 | `CMD`/`ACK`/`ERR`/`STA`，见 `通信协议.md` |
| STA 周期 | 每 200 ms | `CFG_STATUS_PERIOD_MS` |
| 握手/保活边界 | ACK 超时 800 ms，链路失联 1000 ms | 改动时不得越界（见 §5 的"不要做"） |

> 手机在 AP 直连时处于 **192.168.4.x** 网段，ESP 是网关 `192.168.4.1`，没有互联网、没有路由器。
> 这就是它和"同局域网（路由器）"的本质差异：**不是 IP/端口不同，而是"此 Wi-Fi 无外网"**。

---

## 2. 现在安卓工程里与"连接/同局域网"相关的实现（GPT 定位用）

按你已有代码结构：

- `settings/AppSettings.kt`
  - `transportKind`（默认 `MOCK`）、`wifiHost`、`wifiPort`
  - `selectedEndpoint()`：TCP/UDP 时用 `wifiHost:wifiPort` 组 `TransportEndpoint`；
    **端口 0 = 未配置**，`validate`/连接处会拒。
- `transport/TcpTransport.kt`
  - `connect(endpoint, timeoutMillis)`：出站 `Socket` 连 `endpoint.address:endpoint.port!!`
  - `readLoop` 1024B 读 + 字节流上抛；帧重组在上层。
- `transport/UdpDiscoveryScanner.kt`
  - 只做两件事：把已保存端点当候选；若配置了 discovery 地址/端口/报文才发 UDP 探测。
  - **ESP 端没有应答端** → AP 直连下 scan 无真实结果。
- `transport/Transport.kt`：`TransportKind.{TCP,UDP,MOCK,BT...}`、`TransportEndpoint(address,port)`、
  `TransportCandidate`、`ScanRequest`（含 `configuredEndpoint`）。
- `device/DeviceModels.kt`：`DiscoveredDevice(id,name,endpoint,...)`；`id = "<kind>:<addr>:<port>"`。
- `device/DefaultBionicFishRepository.kt`：`connect(deviceId)` 用 `device.endpoint` 走 `transport.connect`；
  有握手相位（`SAFE_STOP_PROBE`→`VERIFIED_V1_COMPATIBLE`）、自动重连、`safeStopOnDisconnect`。
- `control/BionicFishViewModel.kt`：`onWifiHostChanged/onWifiPortChanged`、`onConnect(id)`、
  `onRetryConnection`；Settings 里手动填 host/port 即"同局域网手填"入口。
- UI：`ui/screens/SettingsScreen.kt`、连接页/设备列表（把扫描/保存端点渲染成可点设备）。

**为什么现在只能"同局域网手填"**：设备列表 = 扫描（无 UDP 应答→只有"已保存端点"）+
手动在设置里填 `wifiHost:wifiPort`。用户若不懂要先切到 ESP 热点、并把目标填成
`192.168.4.1:9000`，就会失败。

---

## 3. 要新增的能力（按优先级）

### 3.1 "AP 直连"入口（核心）
在连接页加一个常驻的固定选项（类似一个 `DiscoveredDevice`）：

```
id        = "tcp:192.168.4.1:9000"          // 可写死，也可叫 "AP"
name      = "AP 直连（仿生鱼热点 192.168.4.1）"
endpoint  = TransportEndpoint("192.168.4.1", 9000)
transport = TCP
```

用户点它时：`settings` 置 `transportKind=TCP, wifiHost=192.168.4.1, wifiPort=9000`
并走既有的 `repository.connect(...)`。**不要为它去触发 UDP 扫描**。

> 把端点来源做成配置常量（可在设置里改，但默认 192.168.4.1:9000），避免硬编码散落。

### 3.2 Wi-Fi 前置：确保手机已连到 ESP 热点
连接前/连接失败时，检测"当前 Wi-Fi 是不是仿生鱼热点"：

- 读当前 Wi-Fi 的 SSID（`WifiManager.connectionInfo` / `ConnectivityManager` + 网络能力，
  Android 13+ 需要定位权限或 `NetworkCallback` 才能取 SSID）。
- 若不在热点上：给出明确提示"请先连接 Wi-Fi：`BionicFish-AP`"，或提供**引导连热点**按钮：
  - 目标 SDK ≤ 28：可用 `WifiManager.addNetwork/connectNetwork` 直接加网；
  - 目标 SDK ≥ 29（Android 10+）：系统限制随意切网，优先用 **`WifiNetworkSpecifier`** 发起连网请求，
    或只做引导（打开系统 Wi-Fi 设置），不要绕过系统。

### 3.3 处理"此 Wi-Fi 无互联网"
安卓检测到热点无外网会：提示"无互联网连接"，甚至**自动切回蜂窝/别的 Wi-Fi**，导致 Socket 断开。
处理建议：

- **提示与开关**：连上后提示"该热点无外网属正常；若自动断网请关闭『自动切换到更好的网络』/
  在系统里对该网络选择『不自动切换』"。
- **代码兜底（推荐，稳妥）**：连接前先 `registerNetworkCallback` 请求/绑定到该 Wi-Fi 网络，
  TCP Socket 通过 `ConnectivityManager.bindProcessToNetwork(network)` 或 `SocketFactory` 绑定到该网络，
  避免被默认路由带走。若实现成本高，则**至少**在连不上时提示用户"关闭移动数据重试"。

### 3.4 失败/断连的分类提示（让用户可操作）
AP 直连场景常见三态，UI 要区分：

| 现象 | 原因 | 引导 |
|---|---|---|
| 完全连不上（超时） | 手机不在 ESP 热点 / 热点没起来 | 提示去连 `BionicFish-AP`；检查 ESP 供电 |
| 连上立刻掉 | 无 ACK/STA 回来（STM32 侧没回） | 保留现"握手/数据过期"文案；这是对端未就绪 |
| 连上但被系统切走 | 无外网自动切网 | 见 §3.3 提示 |

### 3.5 数据过期/重连沿用现有逻辑
不要新造心跳。复用现有 heartbeat/STA 过期判定与重连。AP 直连下断线重连间隔/次数建议与现有一致。

---

## 4. 对 GPT 的实现级提示（结合你现有代码）

1. **加一个 AP 直连候选**：在设备列表数据源处（`RepositoryState.devices` 的组装处，见
   `DeviceModels.from` / repository 的 scan 结果合并处）**注入**一个固定
   `DiscoveredDevice("tcp:192.168.4.1:9000", ...)`，`discoverySource` 可用一个
   新增的 `DiscoverySource.AP_DIRECT`（枚举在 transport），`isSaved=false`。
   UI 不必改太多就能出现可点项，`onConnect(id)` 原样生效。
2. **一键端点**：ViewModel 里给"AP 直连"做一个专门 action（类似现有 `onConnect`），内部
   `editSettings { copy(transportKind=TCP, wifiHost=AP_IP, wifiPort=AP_PORT) }` 再
   `repository.connect(id)`；同时更新 `lastDeviceAddress/Port` 便于重启恢复。
3. **SSID/网络检测**：用一个 `@Suppress("DEPRECATION")` 的兼容封装读当前 SSID；不一定要真连网，
   先保证"提示"与"绑定网络（可选）"两条都落到代码里。
4. **别动**这些常量边界：`commandAckTimeoutMillis=800`、`linkTimeoutMillis=1000`、
   `controlSendPeriodMillis=200`、`heartbeatPeriodMillis=500`、`reconnectDelayMillis`。
   它们与 STM32 固件 1000 ms 失联保护是配套的（见 `通信协议.md`）。
5. **协议不动**：`CMD/ACK/ERR/STA` 编解码、粘包重组、序号窗口都复用；AP 直连只是换"管道"的
   端点和网络环境。

---

## 5. 明确的"不要做"

- ❌ 不要给 ESP/STM32 加"UDP 发现应答"——那是固件侧工作且现方案没有；App 侧不要假定有。
- ❌ 不要改动 TCP/UDP 传输层与 V1 协议字节、不要新增猜测报文。
- ❌ 不要为了 AP 直连放宽/改动 800ms ACK 超时、1000ms 失联、200/500ms 控制与心跳周期。
- ❌ 不要把"网络安全停"当急停：AP 断开时 Socket 没了不一定能发指令，安全停仍依赖 STM32 自身失联逻辑
  （约 1000ms 停步进、舵机回中）。
- ❌ 不要假设手机在 AP 直连下仍能访问互联网/其它设备；一切以"热点内联"为准。

---

## 6. 验收清单（AP 直连）

- [ ] 手机切到 `BionicFish-AP` 后，App 首页出现"AP 直连（192.168.4.1:9000）"选项并可一键连接
- [ ] 连上后进入 CONNECTED；每 ~200ms 收到 `STA`，遥测不过期
- [ ] 发送控制命令，收到对应 `ACK`，电机/舵机按命令动作
- [ ] 手机保持在该无外网热点不自动掉线；即便移动数据开着，命令仍直达（或在引导下关移动数据可用）
- [ ] 热点断开/ESP 断电时进入现有失联/重连路径，UI 给出"是否已连上 BionicFish-AP"的可操作提示

---

## 7. 附：两张可行的固件桥接（对安卓都暴露 TCP 9000，本指引均适用）

- **route A（推荐长期）**：ESP 刷"透明 TCP 服务器"自定义固件，串口是干净管道，STM32 零改动。
- **route B（当前）**：ESP 保持原厂 AT + TCP 服务器，STM32 增加 AT/`+IPD` 帧化（`bsp/esp_link.c`）。

两者最终都是：手机以 **TCP 客户端**连 **192.168.4.1:9000**。故本安卓改动与桥接方式无关。
