# 仿生鱼 Android TCP 控制器

当前配套系统为 STM32F103C8T6 + ESP-01S AT/TCP。源码版本 **0.3.2 / versionCode 5**，Android API 26 及以上。使用设备页的 **AP 直连** 连接实际仿生鱼；HarmonyOS 与自定义 ESP 透明桥已暂停。

## 实际连接

1. 手机加入 `BionicFish-AP`，默认密码 `12345678`，选择保持此无互联网 Wi-Fi。
2. 打开设备页“AP 直连”，默认目标 `192.168.4.1:9000`（TCP）。Socket 绑定到该 Wi-Fi，不要求 ESP 有互联网，也不依赖系统默认网络是 Wi-Fi。
3. 应用先发安全停止 CMD，等同序号 ACK 和 `link=1` 的 STA 后才显示已连接。已加入热点、TCP 建连成功、V1 握手通过是三个不同阶段。
4. 完成握手后先检查舵机、姿态、错误码。`E_STEPPER_HW_UNCONFIRMED` 表示电机真实参数未配置，不是连接失败；合法 STOP 握手仍可通过。

AP 设置与 STM32 `app_config.h` 同步修改。IP/端口不提供自动发现保证；热点名称是操作提示，并非应用读取或验证了当前 SSID。

## 当前支持范围

- 实际硬件仅支持 TCP。UDP 设置入口已禁用，旧 UDP 偏好迁移为 TCP 并清除旧发现参数；仓库仍保留旧 UDP 库与测试作为历史实现，应用仓库层拒绝 UDP 扫描/连接。
- 局域网 TCP 入口仅供已知地址调试；AP 直连入口独立使用 Wi-Fi 专用连接。
- 模拟设备可用于界面和协议演示，必须与实际硬件验收区分。蓝牙是不可用占位，不代表 ESP-01S 支持蓝牙。
- 推进只提供前进低速连续运行和停止，已取消转速选择；舵角 ±30°。没有实际转速或舵角反馈。无效姿态显示为不可用，不能使用假零值充当实时数据。
- 普通新命令等 ACK 后才成为周期发送目标。周期保活复用序号；断连、退出控制页或退后台进入停止处理，重连先重新做 STOP 握手。

## 超时与安全

默认控制周期 200 ms，停止保活 500 ms；命令 ACK 等待 800 ms，应用失联阈值 1000 ms，与 STM32 1000 ms 看门狗配套。不要为掩盖 ESP 发送错误而无限增大这些超时。

V1 ASCII 协议不验证设备身份，不提供加密或应用层 CRC；仅在受控的仿生鱼 AP/局域网中使用。完整帧必须保留 LF（或 CRLF），不能用 UDP 数据报边界代替帧尾。

## 构建与测试

需要独立目录中的 JDK 17 或兼容 JDK、Android SDK API 36 和固定 Build Tools 36.0.0。Java/Kotlin 编译目标均显式为17，本机使用JDK21.0.7验证。Gradle Wrapper 固定 8.13 并校验官方 SHA-256；AGP/Kotlin/Compose 依赖版本在 Gradle 文件中固定。

```powershell
# 将 ANDROID_HOME 指向本机 SDK；不把机器路径提交到工程。
$env:ANDROID_HOME = 'C:/path/to/Android/sdk'
.\gradlew.bat testDebugUnitTest lintDebug assembleDebug --no-daemon
```

本轮验证结果见 [验证结果_2026-09-09.md](验证结果_2026-09-09.md)：56/56单元测试、23/23 MuMu完整设备测试通过，lint 0错误（10条依赖版本更新提示），新版APK已安装并正常启动。

新调试 APK：`app/build/outputs/apk/debug/app-debug.apk`。单元测试结果在 `app/build/reports/tests/testDebugUnitTest/`；lint 报告在 `app/build/reports/`。连接 Android 真机/模拟器后再执行 `connectedDebugAndroidTest` 验证 Compose 交互。Windows 的历史中文深目录若触发 AGP 路径检查，应将该 Android 工程复制到短 ASCII 路径构建，不应把跳过路径检查等同于兼容性已验证。

父目录的 `仿生鱼控制器_v0.3.0_AP直连.apk`、旧 screenshots/reports 属历史产物；不能作为本轮 0.3.2 源码的构建或验收证据。当前软件测试与实板验收以本轮重新生成的结果为准。

## 排障

| 现象 | 检查 |
| --- | --- |
| 找不到可用 AP Wi-Fi | 保留无互联网 Wi-Fi，确认路由为目标网段；不要只看手机 Wi-Fi 图标 |
| TCP 连接失败 | STM32 USART3 的 `ESP server`/`reset`/`error`，ESP AT 固件与端口是否匹配 |
| TCP 已连接但 ACK/STA 失败 | USART2 交叉收发、AT CIPSEND/IPD、完整 LF 帧尾、正确连接 ID |
| 收到 `E_STEPPER_DISABLED` | 检查 STM32 是否被显式配置为禁用推进；默认配置允许固定低速前进 |
| 姿态 NA / OLED故障 | STM32 传感器/显示电平、地址、寄存器及重试诊断；不是伪造数据的理由 |

当前整机验收入口是 [STM32验收检查清单](../../STM32控制工程/验收检查清单.md)。未连接实板进行实际测试前，不声明系统已可正常使用。
