# 仿生鱼 Android 控制器 —— APK 重编说明（目标版本 0.4.0）

> **文档生成时间**：2026-09-11
> **目标**：从现有源码重新构建一个**可安装、能通过 V1 握手**的 APK，版本号 **0.4.0 / versionCode 6**
> **本文档面向执行构建的 AI/开发者（Codex）**。请**先完整读完再动手**，尤其是 §4、§5、§9。

---

## 1. 一句话目标

源码里**已经包含了修复"握手永远失败"的那处改动**。你的任务是**不要再改逻辑**，只做版本号提升（已由我改好）→ 跑单测 → 出 APK → 记录哈希。

**这次构建不是为了改 bug，而是为了把已经在源码里的修复编译成 APK。**

---

## 2. 源码位置（绝对路径）

```
工程根目录：
C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\仿生鱼完整项目交付包_v0.3.1\仿生鱼完整项目交付包\仿生鱼安卓AP直连_v0.3.0_2026-09-06\Android工程
```

关键文件：

| 路径 | 说明 |
| --- | --- |
| `settings.gradle.kts` | 工程名 `BionicFishController`，`include(":app")` |
| `build.gradle.kts` | AGP `8.13.2`、Kotlin `2.3.21`、Compose 编译器插件 `2.3.21` |
| `app/build.gradle.kts` | **版本号在这里**（见 §3） |
| `gradle/wrapper/gradle-wrapper.properties` | Gradle **8.13**，带 `distributionSha256Sum` 校验 |
| `app/src/main/java/com/bionicfish/controller/protocol/AsciiProtocol.kt` | **最关键的文件**，见 §4 |
| `app/src/main/java/com/bionicfish/controller/device/DefaultBionicFishRepository.kt` | 握手流程 + 我加的保活，见 §5 |
| `app/src/test/java/...` | JVM 单元测试 |

> ⚠️ 父目录 `仿生鱼安卓AP直连_v0.3.0_2026-09-06\` 下的 `仿生鱼控制器_v0.3.0_AP直连.apk` 是 **2026-09-06 的旧产物，已过时**，不要拿它当基准，也不要覆盖它（见 §10）。

---

## 3. 版本号（**我已经改好了，请勿再改**）

`app/build.gradle.kts` 第 16-17 行：

```kotlin
        versionCode = 6
        versionName = "0.4.0"
```

变更记录：`versionCode` `5 → 6`，`versionName` `"0.3.2" → "0.4.0"`。

其余构建参数（**不要改动**）：

```
namespace / applicationId : com.bionicfish.controller
compileSdk                : 36
buildToolsVersion        : 36.0.0
minSdk                   : 26
targetSdk                : 36
Java / Kotlin target     : 17
buildTypes.release       : isMinifyEnabled = false（无 signingConfig，见 §6）
```

同时我已同步更新 `Android工程\README.md` 第 3 行的版本描述为 `0.4.0 / versionCode 6`，保持源码自洽。

---

## 4. 【最重要】导致握手失败的 bug —— 已在源码中修复，**不要回退**

### 4.1 现象（旧 APK 的实际表现）

手机 App 报：

> TCP 已连接，但 STM32 未完成 ACK + STA 验证；握手超时：未在 2000 ms 内同时收到 ACK 与有效 STA

### 4.2 根因

固件的状态帧**硬编码**发送 `step_rpm=NA`、`step_est=NA`（`ascii_protocol.c` 里写死的字符串，不是数值）。旧版 `AsciiProtocol.kt` 的 `parseStatus()` 里：

```kotlin
// ❌ 旧版（错误）：NA 解析成 null 后命中第一个条件，整条 STA 被判 Malformed 丢弃
if (
    sequence == null || link == null || stepTarget == null || stepEstimate == null ||
    ...
) {
    return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
}
```

于是**每一条 STA 都被丢弃** → 握手的 `statusSeen` 永远为 false → 只能等到 2000 ms 超时。而 ACK 解析正常，所以 App 能收到 ACK 却永远"未完成验证"——**症状与根因完全吻合**。

### 4.3 修复（**当前源码已是修复后的状态，请原样保留**）

```kotlin
// ✅ 现行（正确）：NA 是合法值，只有"既不是数字也不是 NA"才判 Malformed
if (
    sequence == null || link == null ||
    (stepTarget == null && fields["step_rpm"] != "NA") ||
    (stepEstimate == null && fields["step_est"] != "NA") ||
    stepActual === InvalidNumber || servo == null || roll === InvalidNumber ||
    pitch === InvalidNumber || yaw === InvalidNumber || faults == null
) {
    return DecodeResult.Malformed(ProtocolParseError.INVALID_VALUE, raw)
}
```

**请确认这两行 `!= "NA"` 的存在。如果它们在，就不要动 `parseStatus()`。**

该文件在 git 中的修复提交为 `9b97230`（2026-09-09，提交信息 "fix: stabilize TCP control and simplify fixed low-speed propulsion"），旧 APK 早于它。

### 4.4 `parseStatus` 的完整契约（供你判断，不要擅自"简化"）

| 字段 | 固件实际发送 | 解析要求 |
| --- | --- | --- |
| `seq` | 0..65535 | `toIntOrNull()` 且落在 `SEQUENCE_RANGE` |
| `link` | `"0"` / `"1"` | 映射为 Boolean，其他值 → Malformed |
| `step_rpm` | **`NA`** | `NA` → `null` **且必须被接受** |
| `step_est` | **`NA`** | 同上 |
| `step_actual` | **`NA`** | `nullableNumber("NA")` → `null` |
| `step_on` | `"0"` / `"1"` | `null`（旧固件）→ 由 `step_rpm > 0` 推断；非法值 → Malformed |
| `servo` | `-30..30` 整数 | 超范围 → Malformed |
| `roll`/`pitch`/`yaw` | 一位小数的数值（如 `1.2`、`-5.1`、`74.7`） | `toDoubleOrNull()` 且 `isFinite()` |
| `err` | 十进制非负整数（如 `0`、`160`、`163`） | `toLongOrNull()` 且 `>= 0` |

**`err` 非零完全正常，绝不能因为 `err != 0` 就拒绝 STA。** 见 §9.2。

---

## 5. 我在源码里做的一处**有意**改动 —— 不要当作"多余代码"删掉

`DefaultBionicFishRepository.kt`（`performV1Handshake` 附近）新增了**握手期间的链路保活**：

| 新增内容 | 作用 |
| --- | --- |
| 字段 `private var handshakeKeepaliveJob: Job? = null` | 保活任务句柄 |
| `startHandshakeKeepalive(command, ticket)` | 每 `HANDSHAKE_KEEPALIVE_PERIOD_MILLIS = 400 ms` 重发**同一条命令、同一个 seq** |
| `stopHandshakeKeepalive()` | 在 `finally` 里停止，覆盖 ACK 等待段与所有异常路径 |
| 常量 `HANDSHAKE_KEEPALIVE_PERIOD_MILLIS = 400L` | 位于文件末尾的 `private companion object` |

**为什么必须复用同一个 seq**：固件对"同 seq 同载荷"只刷新保活计时、**不推进 `last_sequence`**，所以状态帧的 `seq` 始终等于握手 `seq`，握手探针的同序号匹配不会被打断。**如果这里改成新 seq，握手将永远无法通过。** 保活帧也不注册 ACK 等待器（会与 `queueAndAwait` 的同序号等待器冲突并触发其内部 `check`）。

**为什么需要它**：固件在最后一条被接受的命令后 `CFG_LINK_TIMEOUT_MS = 1000 ms` 就会主动关闭 TCP，而 App 的握手窗口是 `handshakeTimeoutMillis = 2000 ms`，且 `startPeriodicControl()` 要等握手通过才启动 —— 握手期间只有一条命令在续命。这是真实的潜在竞态；400 ms 的续命让它不再可能发生。

**这个改动在正常路径上是无副作用的**：握手健康时 < 400 ms 就完成，`finally` 先取消任务，保活帧一帧都不会发出。

---

## 6. 构建环境要求

| 项 | 要求 | 备注 |
| --- | --- | --- |
| JDK | **17 或以上** | 本机可用的为 JDK 21（`D:\JAVA\jdk-21.0.9.10-hotspot`）。AGP 8.13.2 需要 JDK 17+ |
| Android SDK | **必须已安装**（本机原本没有） | 需要 `platforms;android-36` 与 `build-tools;36.0.0` |
| `ANDROID_HOME` / `ANDROID_SDK_ROOT` | 指向 SDK 根目录 | 或写 `local.properties`：`sdk.dir=...` |
| 网络 | **首次构建需要联网** | Gradle 8.13 发行包 + AGP/Kotlin/Compose 依赖（数百 MB） |
| Gradle | 用仓库自带 wrapper（8.13） | `gradlew.bat` 已存在，**不要**改成别的版本 |

> ⚠️ **本机（生成此文档的机器）没有 Android SDK，也没有 `~/.gradle/caches`，因此我无法编译验证。** 构建必须在有 SDK 的机器/环境上完成。

---

## 7. 构建步骤

### 7.1 设置 SDK 路径

在 `Android工程\` 下创建 `local.properties`：

```properties
sdk.dir=C\:\\path\\to\\Android\\Sdk
```

（或设置环境变量 `ANDROID_HOME`。）

### 7.2 跑单元测试（**必须先跑，它是这次修复的回归证据**）

```bat
cd /d "C:\Users\CloudyRain\Desktop\待办\仿生鱼工程\仿生鱼完整项目交付包_v0.3.1\仿生鱼完整项目交付包\仿生鱼安卓AP直连_v0.3.0_2026-09-06\Android工程"
gradlew.bat :app:testDebugUnitTest
```

**期望：全部通过。** 其中与本次修复直接相关的用例是：

- `DefaultBionicFishRepositoryTest` → `mock scan handshake control telemetry and safe stop share one protocol path`
- `DefaultBionicFishRepositoryTest` → `handshake still requires status in addition to acknowledgement`（`ACK_ONLY` 模式，`handshakeTimeoutMillis = 100L`，**必须仍然失败并报"握手超时"** —— 这是"缺 STA 不能通过"的负向断言，不是 bug）
- `AsciiProtocolTest`（若存在）中针对 `step_rpm=NA` 的解析用例

**如果出现失败，先判断它是不是上述"负向断言"，不要为了让测试变绿而改协议解析逻辑。**

### 7.3 出 APK

```bat
gradlew.bat :app:assembleDebug
```

**请用 `assembleDebug`，不要用 `assembleRelease`。** 原因见下。

### 7.4 ⚠️ 签名陷阱（很容易踩）

`app/build.gradle.kts` 的 `buildTypes.release` **没有配置 `signingConfig`**。因此：

- `assembleRelease` 产出的是**未签名 APK，无法安装**（除非你另行配置签名）。
- `assembleDebug` 由 Gradle 用自动生成的 debug keystore 签名，**可以直接安装**。

历史交付也是走 debug 产物（`artifacts\BionicFishController-v0.3.0-debug.apk`）。

> 如果确实需要 release 签名版：请新建 keystore 并在 `app/build.gradle.kts` 中补 `signingConfigs`，**但那属于新增变更，需要单独说明**，不要悄悄加进去。

### 7.5 产物位置

```
app\build\outputs\apk\debug\app-debug.apk
```

请把它**复制/重命名**为交付用名字，建议：

```
C:\Users\CloudyRain\Desktop\仿生鱼控制器_v0.4.0_AP直连.apk
```

**不要覆盖**父目录里 2026-09-06 的 `仿生鱼控制器_v0.3.0_AP直连.apk`（保留作为历史对照）。

---

## 8. 构建后必须记录的信息

请在构建完成后**填写并回报**：

| 项 | 值 |
| --- | --- |
| 实际 `versionName` / `versionCode` | （应为 `0.4.0` / `6`） |
| APK 文件大小 | ______ 字节 |
| APK SHA-256 | ______ |
| 单元测试结果 | ______ 通过 / ______ 失败 |
| 构建用的 JDK 版本 | ______ |
| 构建用的 AGP / Gradle 版本 | `8.13.2` / `8.13` |
| 构建时间 | ______ |

核对版本号的方法（无需额外工具）：

```bat
powershell -c "Add-Type -AssemblyName System.IO.Compression.FileSystem; $z=[System.IO.Compression.ZipFile]::OpenRead('app\build\outputs\apk\debug\app-debug.apk'); $e=$z.Entries | ? {$_.Name -eq 'AndroidManifest.xml'}; 'manifest entries:'; $z.Entries.Count; $z.Dispose()"
```

或安装后用 `adb shell dumpsys package com.bionicfish.controller | findstr versionName`。

---

## 9. 现场验收：装上新 APK 后应该看到什么

### 9.1 成功判据

1. 手机加入热点 `BionicFish-AP`（密码 `12345678`），选"保持连接"。
2. App → **设备**页 → 点「AP 直连 / 连接」。
3. **应在约 1 秒内显示已连接**，App 日志出现：
   ```
   TX      <CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>
   RX      Acknowledgement(sequence=1, duplicate=false)
   RX      Status(sequence=1, linkAlive=true, stepTargetRpm=null, stepEstimatedRpm=null,
                  stepActualRpm=null, servoDegrees=0, rollDegrees=…, pitchDegrees=…,
                  yawDegrees=…, faultBits=…)
   SYSTEM  V1 兼容握手通过：安全停止 ACK + STA
   ```
   **关键：`stepTargetRpm` 应为 `null`**（因为固件发的是 `step_rpm=NA`）。
   旧 APK 在此处会直接卡到 `握手超时` 并显示 `stepTargetRpm=0`。

4. 进入控制页，逐项验证：停止、前进（固定低速）、舵机 ±30°、姿态角实时变化。

> 我之前用 PC 直连固件实测过同样的握手，固件侧的行为是确定的：
> ```
> → <CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>
> ← <ACK,seq=1,result=OK>
> ← <STA,seq=1,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,servo=0,roll=1.2,pitch=-5.1,yaw=74.7,err=160>
> ```
> 也就是说 **ACK 与同序号、`link=1` 的 STA 都会在毫秒级返回**。

### 9.2 以下现象**都是正常的，不要当 bug 追**

| 现象 | 说明 |
| --- | --- |
| `faultBits` / `err` ≠ 0（常见 `160`、`163`） | `128`=OLED I²C 异常、`32`=串口发帧丢弃（**一旦置位会锁存到断电**）、`2`=ESP 收字节丢失、`1`=链路超时。**协议只要求 `err >= 0`，非零不影响握手。** |
| `step_rpm=NA`、`step_est=NA`、`step_actual=NA` | 固件**故意**这样发：本机无编码器，60/100 只作旧协议兼容字段，不声明真实转速 |
| 重复发送同一帧时收到 `result=DUP` | 正确行为：固件对"同 seq 同载荷"判重并回 `DUP` |
| 同 seq 不同载荷收到 `<ERR,seq=N,code=E_SEQ_CONFLICT>` | 正确行为 |
| 姿态角长时间几乎不变 | 静止台面上 roll/pitch 恒定、yaw 只以 0.1° 步进缓慢变化，属正常 |
| 舵机指令后约 1 秒电机自动停 | 固件 `CFG_LINK_TIMEOUT_MS = 1000 ms` 失联保护；App 需周期保活 |

### 9.3 另一条独立验证路径（不依赖 APK）

固件当前工作在 **AP+STA 双模**，若手机/PC 与 ESP 处于同一外部网络（本项目实测为 `CTGU-T-Web`，ESP 地址 `192.168.3.40`），可直接从 PC 直连：

```powershell
$c = New-Object System.Net.Sockets.TcpClient
$c.Connect("192.168.3.40", 9000)
$fr = "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>`n"
$b = [Text.Encoding]::ASCII.GetBytes($fr)
$c.GetStream().Write($b,0,$b.Length); $c.GetStream().Flush()
Start-Sleep -Milliseconds 800
$rb = New-Object byte[] 512; $n = $c.GetStream().Read($rb,0,512)
[Text.Encoding]::ASCII.GetString($rb,0,$n)
$c.Close()
```

**这条路径已经实测通过**，可作为"固件没问题、问题在 App"的对照。

---

## 10. 明确**不要**做的事

1. **不要**修改 `AsciiProtocol.kt` 的 `parseStatus()` / `parseAcknowledgement()` 逻辑（§4 的两行 `!= "NA"` 必须在）。
2. **不要**删除 §5 的握手保活代码，也不要把它的 seq 改成新号。
3. **不要**改 `CFG`/协议语义、帧格式、字段名 —— 固件侧已冻结，改任何一侧都会破坏兼容。
4. **不要**动 `gradle-wrapper.properties`、AGP/Kotlin/Compose 版本、`compileSdk`/`targetSdk`/`minSdk`。
5. **不要**为 release 添加签名配置（除非明确要求并单独说明）。
6. **不要**覆盖或删除父目录里 2026-09-06 的旧 APK。
7. **不要**把 `build/`、`.gradle/`、`local.properties` 等产物提交进版本库。

---

## 11. 故障排查

| 症状 | 处理 |
| --- | --- |
| `SDK location not found` | 设 `ANDROID_HOME` 或写 `local.properties` 的 `sdk.dir` |
| `Unsupported class file major version` / JDK 报错 | 换 JDK 17 或 21，并确认 `JAVA_HOME` 指向它 |
| 首次构建卡在下载 | 需要联网；确认能访问 `dl.google.com` 与 `repo.maven.apache.org` |
| `assembleRelease` 出的包装不上 | 见 §7.4，改用 `assembleDebug` |
| 单测失败于 `握手超时` | 检查是不是 §7.2 说的那条**负向断言**；若是其他用例失败，先看 §4 |
| 装上新 APK 仍握手失败 | 先把 App 的 **RX 日志**导出来比对 §9.1：若能看到 `Status(...)` 行即说明解析已修好，问题在别处 |

**导出 App 日志的方法**：底部导航 → **遥测**（心跳图标）页 → 滚到最底部 → 「本地通信日志」卡片 → 「**导出日志**」。会调起系统"保存到…"对话框，文件名 `bionic-fish-log-<时间戳>.txt`，内容是 TSV（`timestamp_epoch_ms` / `direction` / `message`，direction 为 `RX`/`TX`/`SYSTEM`）。
⚠️ 日志**只存在内存里**（上限 500 条），App 进程被杀就没了，**必须在握手尝试之后、App 还活着时立刻导出**。

---

## 12. 背景：这套系统是什么（便于判断改动是否合理）

```
手机 App ──TCP:9000──> ESP-01S (AT 固件, AP+STA) ──UART2 115200──> STM32F103C8T6
                                                                    ├─ TB6612 双桥 → 两相步进
                                                                    ├─ TIM3 50Hz → IP65 舵机
                                                                    ├─ 软件 I²C → JY901S 姿态
                                                                    └─ 软件 I²C → SSD1306 OLED
```

- **所有控制逻辑、失效保护、协议解析都在 STM32 上**；手机只是遥控界面。
- 帧格式：`<TYPE,key=value,...>\n`，TYPE ∈ `CMD` / `ACK` / `ERR` / `STA`。
- 握手流程：App 发一条**安全停止** `CMD`（`move=S,turn=C,step_speed=60,servo=0`）→ 要求在同序号 `ACK` 与 `link=1` 的 `STA` **都在窗口内到达**才算通过。
- 固件失联策略：最后一条被接受的命令后 **1000 ms** 无新命令 → 停步进、舵机回中、清序号窗口、关闭该 TCP 连接。
- ESP 固件版本（实测）：`AT version:1.7.4.0` / `SDK 3.0.5-dev`，**8Mbit(512KB+512KB)** ⇒ 属 NONOS AT 1.7.4 Nano 配置，**不要重刷 ESP**。

---

## 13. 交付清单（构建完成后应产出）

- [ ] `仿生鱼控制器_v0.4.0_AP直连.apk`（debug 签名，可安装）
- [ ] 单元测试全部通过的输出记录
- [ ] APK 的 SHA-256 与字节数（填入 §8 表格）
- [ ] 装机后 §9.1 的握手成功日志（App 导出的 `bionic-fish-log-*.txt`）

---

## 14. 一句话总结给执行者

> **只提升版本号（已改好）、跑单测、`assembleDebug` 出包。**
> **不要碰 `AsciiProtocol.kt` 的 `NA` 判断和握手保活 —— 那正是本次构建要交付的修复。**
