# 仿生鱼安卓控制器 v0.5.0

版本：`0.5.0 / versionCode 7`；包名：`com.bionicfish.controller`。本版在 v0.4.0 的独立副本中实现双直流电机与 ±15° 舵机控制，未覆盖旧工程。

用户已在本轮明确授权实施及默认允许两路反转；[原始改造方案](docs/原始资料/双电机控制与舵机15度_安卓端改造方案_v0.5.0.md) 中“只出方案”的旧范围已由该授权覆盖。原文关于固件和实测的描述是用户资料，不是本次硬件测试结论。

## 1. 本版功能与边界

- M1 为 370 直流减速电机，M2 为 N20 直流减速电机；两路各自前进、停止、后退。
- 每条 CMD 是两路电机和舵机的完整目标。单路停止只停该路；底部“立即安全停止”显式同时停止两路。
- 舵机预设及滑杆为 -15° / 0° / +15°，1° 步进；发送协议模型拒绝越界值。
- 用户方案称配套固件两路均为固定 100% 占空比。APK 不提供 PWM 或 RPM 调速；`step_speed=60` 仅维持旧协议合法性，不改变输出。
- 回传 `m1/m2` 为固件下达方向，不是实测机械反馈。旧固件缺字段显示“未知”；全部转速 `NA` 保持未知。
- 新安装默认允许两路反向；升级时保留旧版明确关闭的设置。在“设置”中开启“允许电机反向”并保存即可。保存关闭反向前先执行双路停止。
- 已移除旧步进参数确认对控制按钮的阻挡，保留旧状态字段和旧故障位解释；旧固件拒绝命令时仍显示真实 ERR。

只修改 Android；没有改写或烧录 STM32、ESP 固件。是否已安装双电机、±15° 及网页限角配套固件，仍需用户确认；本 APK 不能替其它客户端收窄固件/网页的行程。

## 2. 连接与安装

实际硬件入口是 **Wi-Fi TCP AP 直连**。手机手工加入 `BionicFish-AP`（指引初始密码 `12345678`）后，返回应用连接 `192.168.4.1:9000`。参数来自用户《AP直连_安卓端修改指引.md》，不是本机实测；已修改时以设备实际值为准。

AP Socket 绑定符合目标本地子网的 Wi-Fi，不回退移动数据/VPN；不要求热点有互联网，不自动加入热点或读取 SSID。普通 LAN TCP、UDP 和 Mock 保留，LAN/UDP 地址与发现参数须自行确认。ESP-01S 没有蓝牙，本版不提供可用蓝牙连接。

安装桌面的 `BionicFish-v0.5.0.apk`；交付包根目录也放有同字节的同名副本。不要安装工程 ZIP、AndroidTest APK 或 MCU BIN。完整文件名应以 `.apk` 结尾。S24 安装、来源权限及实际热点行为待实测。

交付采用 debug 签名；覆盖安装要求与手机旧包签名相同。签名冲突时先保存需要的日志/设置，由用户决定是否卸载旧版，不能保证任意历史 APK 可原位升级。

## 3. 工程结构与构建

```text
app/src/main/java/com/bionicfish/controller/
├─ transport/   TCP、UDP、Wi-Fi 专用 Socket、Mock
├─ protocol/    CMD 编码、STA/ACK/ERR、流式分帧
├─ device/      会话、握手、发送队列、安全停止、重连
├─ control/     ViewModel 与完整控制目标
├─ telemetry/   回传状态、新鲜度、故障位、本地日志
├─ settings/    DataStore、AP 参数、用户偏好
└─ ui/          Compose 页面、主题、导航、可复用组件
app/src/test/、app/src/androidTest/   自动测试
scripts/Build-Apk.ps1                分阶段测试、构建、签名校验
```

| 构建项 | 固定配置 |
| --- | --- |
| Java / Kotlin / Gradle | Java 17 / Kotlin 2.3.21 / Gradle 8.13 |
| Android Gradle Plugin | 8.13.2 |
| min / target / compile SDK | 26 / 36 / 36 |
| Build Tools | 36.0.0 |

准备 JDK 17、SDK 36 和 Build Tools 36.0.0，在项目目录执行（实际路径自行填写）：

```powershell
./scripts/Build-Apk.ps1 -JdkHome '你的JDK17目录' -AndroidSdk '你的AndroidSDK目录'
./gradlew.bat connectedDebugAndroidTest --no-daemon --no-parallel
```

第二条需要模拟器或明确选择的测试手机。构建 APK 位于 `app/build/outputs/apk/debug/app-debug.apk`。依赖版本沿用 v0.4.0；本版不新增 release 密钥或发布配置。

## 4. 验证与联调

本次编译、lint、JVM/UI 测试数量、截图、APK 大小、SHA-256 与签名以 [v0.5.0 构建验证结果](artifacts/v0.5.0构建验证结果.md) 为唯一结果来源，不沿用旧版统计。

先用 Mock 验证两路独立方向、舵机 ±15°、双路急停和状态；再在受控台架按 [验收检查清单](验收检查清单.md) 联调。三星 S24、真实 ESP/STM32、机械转向与失联停机时间均未在本轮实测。关闭网络后停止帧可能无法送达，必须依赖固件失联停机及可立即断电措施。

日志仅在本地有界保存，用户主动导出；无默认上传。TCP 协议无加密或设备身份认证，不应暴露到不可信互联网。

详见 [通信协议](通信协议.md)、[UI设计与交互说明](UI设计与交互说明.md)、[AP直连使用说明](docs/AP直连使用说明.md)。
