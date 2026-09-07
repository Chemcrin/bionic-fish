# v0.3.0 AP 直连构建验证结果

验证日期：2026-09-06。仅更新安卓应用；未更改或重新烧录 STM32/ESP 固件。

## 产物

- 安装文件：交付包根目录 `仿生鱼控制器_v0.3.0_AP直连.apk`，Android 8.0（API26）及以上。
- 包名 `com.bionicfish.controller`，versionName `0.3.0`，versionCode `3`。
- 大小：20,393,413 字节。
- SHA-256：`987D7A5DFF871C94C2044A54D5CF79F5D44FE882F0BFAFC46AB97BBF8F8DEAC7`。
- `zipalign -c 4` 通过，APK Signature Scheme v2 签名验证通过；沿用 Android Debug 证书，不是生产发布签名。
- 工作工程保存 `artifacts/BionicFishController-v0.3.0-debug.apk`；桌面交付包只提供一个主安装 APK，不混入 AndroidTest APK 或旧版 APK。

## 最终构建与测试

```powershell
.\gradlew.bat testDebugUnitTest lintDebug assembleDebug assembleDebugAndroidTest connectedDebugAndroidTest --no-daemon --no-parallel
```

最终结果 `BUILD SUCCESSFUL`。JDK17、Gradle8.13、AGP8.13.2、Kotlin2.3.21；min/target/compile SDK 为26/36/36。

| 检查 | 最终结果 |
| --- | --- |
| Kotlin/Java/Compose/AndroidTest 编译 | 通过，无编译或链接错误。 |
| JVM 单元测试 | 50/50，失败0、错误0、跳过0。 |
| Repository 测试 | 19项：包括AP不扫描直连、保存不覆盖LAN、重连仍限Wi-Fi且先停止、缺STA拒绝握手、TCP前失败不误标握手失败。 |
| 协议/配置/其他传输测试 | 协议8、配置10、Mock1、TCP/UDP3。 |
| Socket/AP 路由专项 | 9项：直连子网过滤、拒绝默认/网关路由、拒绝DNS、取消/失败回收、旧会话隔离和回调注册竞态。 |
| Android lint | 错误0；警告10，仅依赖版本更新提示（AndroidGradlePluginVersion 1、GradleDependency 7、NewerVersionAvailable 2）。 |
| Android Compose instrumentation | 20/20，失败0、错误0、跳过0；Android15/API35 AOSP ATD 模拟器 BionicFishApi35。 |
| 新 AP UI 专项 | 7项：独立入口、不扫描、系统设置回调、状态/互锁、独立参数、去重复列表、320dp/200%字体与截图。其余13项原控制/导航测试完整回归。 |
| 渲染补采 | 在最终APK上额外执行2项截图测试，均通过；仅为截图采集，不加到20项计数。 |

SDK 命令行工具仍输出 SDK XML v3/v4 兼容提示，但未阻止上述检查。首次检查发现的 API29 `RouteInfo.hasGateway()` 已替换为 API21 可用的网关地址判断，没有提升 minSdk 或屏蔽 NewApi。AP 首卡新增后，旧测试已改为滚动到普通TCP选项再验证可见，保留蓝牙入口隐藏断言。

## 界面与实际应用冒烟

- 五张图均由最终 Android/Compose 渲染管线输出（1080×2280），非概念设计稿：`screenshots/ap-direct-device-page.png`、`ap-direct-200-percent-text.png`、`control-screen-light.png`、`control-screen-dark.png`、`control-screen-200-percent-text.png`。
- 已目视检查AP普通字体/200%字体：主要连接按钮可达，参数换行不溢出；小屏及停止按钮可见性由仪器测试覆盖。
- 模拟器实际启动 MainActivity，未连接鱼端AP时点击直连，得到“未找到能直达目标地址的 Wi-Fi 网络”，不进入已连接/可控制状态；握手应保持“未开始”。证据使用 UIAutomator XML（`ap-no-wifi-ui.xml`）。无真实ESP成功连接证据。
- 原始 `adb screencap` 在此无窗口ATD上仅返回黑帧，不作为验收图片；交付只使用Compose截图。

## 实物边界与参数来源

`BionicFish-AP`、初始密码 `12345678`、TCP `192.168.4.1:9000` 和用户已有 AT/+IPD 桥接来自用户提供的《AP直连_安卓端修改指引.md》。原件保存在 `docs/原始资料/`，不能把资料内“已联调”表述为本机已完成实测。

本次保留42步进、±30°舵机及原协议，未恢复N20。手机需手动加入热点、保留无互联网连接，再点击AP直连；应用不读取/验证SSID、不自动配网。AP Socket只绑定符合目标IPv4子网的Wi-Fi，普通LAN不受影响；V1握手只验证兼容性，不证明设备身份。

真实ESP、目标手机在移动数据开启时的路由稳定性、AT/+IPD收发、断电/离网后STM32停机时间、锁屏/后台及电机实际动作仍待实测。不要用此前不含AT/+IPD桥接的旧STM32二进制覆盖用户已经联调的固件。

实现依据：[Android Network.bindSocket](https://developer.android.com/reference/android/net/Network#bindSocket(java.net.Socket))、[ConnectivityManager 网络回调](https://developer.android.com/reference/android/net/ConnectivityManager)。应用按Socket绑定，不使用进程级网络切换。

## 报告位置

本轮HTML、XML和单元测试明细在 `ap-reports/`；旧 `构建验证结果.md` / `reports/` 属历史v0.2.0，不能替代本轮证据。桌面包保留当前源码、文档、报告和5张有效截图；不包含构建缓存、旧APK、测试安装包或任何私钥。
