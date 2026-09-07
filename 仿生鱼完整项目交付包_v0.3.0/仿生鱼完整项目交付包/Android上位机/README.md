# 仿生鱼安卓控制器

这是用于 Android 手机/平板的仿生鱼控制端工程，采用 Kotlin、Jetpack Compose、Material 3、单 Activity 和分层状态管理。应用只通过通信协议向 ESP-01S/STM32 发送控制命令，不直接操作任何 STM32 GPIO。

> **与当前固件一致性说明**：最新版 STM32 固件已移除 N20 减速电机，TB6612 的 A、B 两个 H 桥均用于同一颗 42 型两相步进电机。因此本应用不提供 N20 方向、PWM、状态或模拟数据；旧提示词中涉及 N20 的部分已被当前硬件决定覆盖。

> **资料边界**：当前交付目录及用户提供的文件路径中未找到 `UI设计RP.md`。UI 依据来自本轮需求中列出的设计规则，不能视为对该缺失文件的逐条移植。

## 1. 当前传输结论

| 传输 | 当前定位 | 边界 |
| --- | --- | --- |
| Mock | 默认可验证模式 | 不需要硬件，使用与真实链路相同的协议编解码器。 |
| Wi-Fi TCP | 已预留真实传输实现 | ESP 工作模式、IP、端口和服务发现方式均为“待确认”，用户必须主动配置。 |
| Wi-Fi UDP | 已预留真实传输实现 | 广播/组播范围、目标端口、应答地址和丢包策略均为“待确认”。 |
| Bluetooth Classic/BLE | 能力门控的扩展入口 | ESP-01S 本身不提供蓝牙；仅当手机和未来的实际鱼端硬件都支持对应蓝牙链路且 UUID 已确认时才可启用。 |

应用不得把 ESP-01S 显示成蓝牙设备，也不会静默连接未由用户选中的设备。真实硬件首次联调应优先使用 Wi-Fi；最终采用 TCP 还是 UDP，必须依据 ESP 固件/AT 配置和台架抓包结果决定。

## 2. 工程结构

```text
bionic-fish-android-controller/
├─ app/src/main/java/com/bionicfish/controller/
│  ├─ transport/     # Transport 抽象、TCP/UDP/Mock、连接状态与重连
│  ├─ protocol/      # ASCII 组帧、流式拆帧、CMD/STA/ACK/ERR 模型
│  ├─ device/        # 发现、保存用户选择、握手与设备会话
│  ├─ control/       # 控制意图、限幅、安全停止与发送调度
│  ├─ telemetry/     # 状态、新鲜度、故障位和本地通信日志
│  ├─ settings/      # 待确认的传输参数与用户偏好
│  └─ ui/            # Compose 页面、组件、主题和导航
├─ app/src/test/     # 协议、序号、粘包/拆包、超时等 JVM 测试
├─ app/src/androidTest/ # 基础 Compose UI 测试
├─ 通信协议.md
├─ UI设计与交互说明.md
└─ 验收检查清单.md
```

网络、扫描、解析和重连均应在协程中运行，以 `Flow`/`StateFlow` 向 UI 提供不可变状态；Composable 不直接持有 Socket、阻塞等待或解析串口帧。本地日志采用有界存储，不默认上传。

## 3. 构建环境

当前 Gradle 配置基线：

| 项目 | 值 |
| --- | --- |
| applicationId | `com.bionicfish.controller` |
| versionName / versionCode | `0.2.0` / `2` |
| minSdk / targetSdk / compileSdk | 26 / 36 / 36 |
| Java | 17 |
| Android Gradle Plugin | 8.13.2 |
| Kotlin | 2.3.21 |
| Gradle | 8.13 |

准备 JDK 17、Android SDK 36 和可用的 Android SDK 许可证后，在项目根目录执行：

```powershell
./gradlew.bat testDebugUnitTest lintDebug assembleDebug assembleDebugAndroidTest --no-daemon --no-parallel
./gradlew.bat connectedDebugAndroidTest --no-daemon --no-parallel
```

交付目录已经包含 `gradlew.bat` 和 Gradle wrapper。第二条命令需要已启动的模拟器或已连接真机。本次可追溯结果、产物哈希和仍待真实硬件执行的项目以 `验收检查清单.md` 为准。

调试 APK 通常位于：

```text
app/build/outputs/apk/debug/app-debug.apk
```

### 本次实际构建结果

- 最终组合构建已成功：`testDebugUnitTest`、`lintDebug`、`assembleDebug`、`assembleDebugAndroidTest` 全部完成。
- 31 个 JVM 单元测试全部通过；lint 为 0 错误、10 个仅提示 Gradle/依赖存在更新版本的非功能警告。
- 13 个 Compose instrumentation 测试已在 Android 15 / API 35 AOSP ATD 模拟器 `BionicFishApi35` 上全部执行通过，0 失败、0 错误、0 跳过。
- 可安装的开发调试包位于 `artifacts/BionicFishController-v0.2.0-debug.apk`，大小 20,257,298 字节，SHA-256 为 `D85F952C1FC4FE8B7F331A0D94CEBB68099DE2C782E93D1BFA4BBA8F8805B972`。
- APK 已通过 zipalign 校验及 APK Signature Scheme v2 调试签名验证。详细记录和 HTML 报告见 `artifacts/构建验证结果.md`、`artifacts/reports/`。

### v0.2.0 控制页优化

- 连接状态、推进目标、速度档位和舵机目标集中为清晰摘要；操纵、转向和舵机微调按任务分组。
- 60/100 RPM、左/中/右预设提供边框、勾选和状态语义，避免只靠颜色表达选中状态；不支持反向时隐藏反向动作并显示 STM32 安全策略说明。
- 舵机滑杆拖动期间只做本地预览，松手后发送一次最终命令，避免 ACK 队列堆积。
- “立即安全停止”位于根 `Scaffold` 的固定底栏，在页面滚动、长错误提示和 200% 字体下仍可见、可点击；并发停止请求的等待状态不会提前消失。
- 浅色、深色和 200% 字体真实 Compose 渲染图分别见 [浅色](artifacts/screenshots/control-screen-light.png)、[深色](artifacts/screenshots/control-screen-dark.png) 和 [200% 字体](artifacts/screenshots/control-screen-200-percent-text.png)。

### 发布签名

发布版本必须使用项目所有者自己的 keystore。密钥口令应通过本机环境变量、`~/.gradle/gradle.properties` 或 CI 密钥库注入，禁止提交 keystore、明文口令和带签名的私密配置。生成 release APK/AAB 前，还需决定代码压缩、备份策略和版本号；当前配置不代表生产发布已完成。

## 4. 权限与隐私

当前版本使用普通 TCP/UDP Socket、用户填写的端点以及可选的 UDP 应用层探测，不读取 Android 系统的 Wi-Fi 扫描列表；鱼端也没有已确认的蓝牙硬件。因此当前构建不会弹出危险权限/运行时权限对话框：

| 场景 | 权限说明 |
| --- | --- |
| TCP/UDP 连接 | `INTERNET` 为普通权限；网络状态用于显示可操作错误。 |
| 系统 Wi-Fi 扫描列表 | 当前未实现，也不申请 `NEARBY_WIFI_DEVICES` 或位置权限。若后续确需读取系统扫描结果，必须按目标 Android 版本另行实现按需授权。 |
| Bluetooth | 当前只有不可用的接口占位，不扫描、不连接、不申请 `BLUETOOTH_SCAN`/`BLUETOOTH_CONNECT`。接入经过确认的鱼端蓝牙硬件后，才可同步实现 UUID 配置和按需授权。 |

通信日志仅保存在本地，用户主动导出时才写入用户选择的位置；应用不默认上传控制、网络或姿态数据。应用已关闭 Android 系统云备份，并在备份规则中排除全部本地控制数据。

## 5. 无硬件使用

1. 启动应用并选择 **Mock 设备**。
2. 连接后先完成安全停止握手。
3. 在“控制”页验证停止、前进、60/100 RPM、左/中/右和舵机角度限幅。
4. 在“状态/调试”页观察 `STA`、延迟、最后更新时间、故障位及模拟的姿态数据。
5. 断开、切后台或退出控制页，确认 UI 记录了安全停止尝试；可在日志中查看发送与应答。

Mock 只验证应用状态机和协议，不证明 ESP、串口、电机或实际水中运动安全。

## 6. 真实 Wi-Fi 联调

以下参数均不得从本工程名称或 ESP-01S 型号推断：

| 配置项 | 当前值 |
| --- | --- |
| ESP 工作模式（AP/STA） | 待确认 |
| TCP 或 UDP | 待确认 |
| IP/主机名 | 待确认 |
| 控制端口/发现端口 | 待确认 |
| 局域网发现方式与服务标识 | 待确认 |
| ESP AT 固件或自定义固件版本 | 待确认 |
| Wi-Fi 配网流程 | 待确认 |
| 控制/心跳周期 | 默认 200/500 ms；当前固件下仅允许 50~750 ms，最终值待联调 |
| 显式命令 ACK 超时 | 默认 800 ms；允许 100~900 ms 且必须小于应用失联超时 |
| 重连退避 | 可配置，最终值待联调 |

建议顺序：

1. 先用电脑串口确认 STM32 USART2 与 ESP RXD/TXD 交叉连接及 115200 参数。
2. 固定一套可追溯的 ESP 固件与 AP/STA 配置，记录 TCP/UDP、地址和端口来源。
3. 手机主动选择扫描到的服务或手工输入已知地址；首次连接不启用静默自动连接。
4. 连接后发送五字段安全停止 CMD，收到同序号 `ACK` 并继续收到可解析 `STA` 才认定 V1 协议可用。
5. 测试拆包、粘包、丢包、Wi-Fi 切换、锁屏、后台、远离热点和 ESP 复位。
6. 最后才在受控台架上解除 STM32 步进参数门禁并测试机械动作。

## 7. 握手、保活与安全停止

当前 STM32 V1 固件只认识 `CMD`，不会接受凭空增加的 `HELLO`/`PING` 帧。因此：

- V1 兼容握手使用一帧合法的安全停止 CMD；收到相同序号的 `ACK,OK`/`ACK,DUP`，并能继续解析 `STA`，才将会话标为协议可用。
- 周期发送当前五字段 CMD 同时承担固件保活；专用版本/设备类型握手与心跳帧仅作为未来扩展，必须在 ESP 与 STM32 同步实现并协商后启用。
- 断开、离开控制页或进入后台时，应用会按配置尽力发送高优先级 `move=S` 安全帧并显示发送失败。默认同时回中（`turn=C,servo=0`）；若用户关闭“安全停止时舵机回中”，则保留上一安全角度。移动网络、进程终止或射频断开会使该帧无法送达，因此仍必须依赖 STM32 自身约 1000 ms 的失联停机策略和独立硬件保护。
- “松手即停”与“显式停止”必须共用同一控制状态模型。停止按钮始终可见且优先清空待发送运动命令。
- 显式控制及安全停止按 `seq` 等待 `ACK,OK`/`ACK,DUP`；同序号 `ERR` 直接失败，ACK 超时进入失联/重连策略。周期保活不阻塞发送线程等待 ACK。
- 改连设备前会先停止旧鱼；进入 `DISCONNECTING` 后立即封锁普通控制。旧会话事件与自动重连由会话代次隔离，不能拆掉较新的连接。

## 8. 当前已知限制

- ESP-01S 网络服务实现、发现机制、IP、端口及配网流程均待确认；扫描不到服务不等于手机 Wi-Fi 故障。
- Bluetooth 只有接口/能力门控，不代表鱼端具有蓝牙；Classic 地址、BLE service/characteristic UUID 全部待确认。
- STM32 当前默认拒绝反向 `move=R`，前进也可能因步进参数门禁返回 `E_STEPPER_HW_UNCONFIRMED`。APK 必须展示错误，不能伪装为执行成功。
- `step_actual=NA` 是真实硬件无测速反馈的明确状态；UI 不得用 `step_est` 冒充实际 RPM。
- 协议未提供加密、设备身份认证或防重放安全机制，不应直接暴露到不可信互联网。
- UI 设计参考文件 `UI设计RP.md` 未找到；若后续补充该文件，应另行进行差异审查。
- 13 个 UI 测试已在 Android 15 模拟器执行通过，并产出浅色、深色和 200% 字体真实渲染图；目标真机 TalkBack、系统“减少动态效果”、更多横竖屏尺寸和真实后台/网络切换仍为“待实测”。

更多细节见 [通信协议.md](通信协议.md)、[UI设计与交互说明.md](UI设计与交互说明.md) 和 [验收检查清单.md](验收检查清单.md)。
