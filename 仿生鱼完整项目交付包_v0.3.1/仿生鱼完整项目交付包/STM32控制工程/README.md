# STM32 仿生鱼控制工程

当前交付：STM32F103C8T6 + ESP-01S AT/TCP + Android V1 ASCII 控制器。两相双极步进电机由 TB6612 的 A/B 两桥驱动；N20 不受支持。HarmonyOS、ESP32 WebSocket、自定义 ESP 透明桥均不在本轮链路中。

## 硬件和软件边界

| 功能 | 配置 |
| --- | --- |
| MCU / 时钟 | STM32F103C8T6，64 KiB Flash、20 KiB RAM；HSE 8 MHz、SYSCLK 72 MHz，晶振需与实板一致 |
| 推进电机 | TIM2 CH1/2，20 kHz、10% PWM；A/B 两桥各接一组线圈，固定每 50 ms 四拍换相，无位置/RPM控制 |
| 舵机 | PA6 / TIM3 CH1，50 Hz，1000–2000 µs，默认中位 1500 µs；相对角 ±30° |
| ESP 通信 | PA2 TX → ESP RX，PA3 RX ← ESP TX；115200 / 8N1，无 RTS/CTS |
| 调试 | PB10 USART3 TX → CH340 RX；仅输出诊断及 V1 帧，不接收控制命令 |
| 姿态 | JY61P，PB6/PB7 软件 I²C；地址 0x50、起始寄存器 0x3D 为待实板核实的配置 |
| 屏幕 | SSD1306 128×64，PB8/PB9；尝试 0x3C/0x3D；保留原单页 ASCII 布局 |
| 未使用接口 | KEY1/2/3、PA7 第二舵机、PA9/PA10 USART1 未启用；不据此判断物理悬空 |

所有可调参数在 `app/Inc/app_config.h`。默认允许前进并持续固定低速换相，STOP 或失联时两桥 coast。已删除每转步数、RPM 换算和硬件参数确认门禁；只需调整线圈 PWM 和换相周期。`CFG_STEPPER_DRIVER_ENABLED=0` 可显式禁用推进。10% 为本轮低功率试运行初值，实际转动和温升仍需上板观察。

没有转速编码器或舵角反馈。`step_rpm`、`step_est`、`step_actual` 均为 `NA`；`step_on=0/1` 表示驱动命令为停止/运行，不证明电机实际转动。JY61P 无效/过期数据上报 `NA`；相邻样本变化筛选默认 90°/50ms，为宽松可配置初值。

## ESP-01S 固件要求

已收到用户提供的 `AT+GMR` 回包：**AT 1.7.4.0，SDK 3.0.5-dev(52383f9)，编译于2020-08-28**，匹配本工程 NONOS 分支，保留现有固件。最初查询启用了 RTS，提供保存修正命令后，用户复查得到 `+UART_CUR:115273,8,1,0,0`，当前串口参数已匹配约 115200、8N1、无流控要求。完整回包与 `UART_DEF` 操作见[硬件说明](硬件引脚与驱动说明.md)。STM32/TCP 实板联调尚待验证，Flash 容量仅在需要重刷时进一步核对。

`bsp/Src/esp_link.c` 使用普通 AT 收发模式，**不能与裸 UART 透明桥一起使用**。GMR 区分 NONOS AT 1.7.4/1.7.5 与 ESP-AT 2.x 命令差异；现场必须核对实际 GMR 结果、Flash 容量及 AT UART 的 GPIO 映射。

ESP-01S 常见容量为 1 MB。不能把面向更大 Flash 或不同 AT 引脚的 ESP8266 通用镜像直接视为兼容镜像。可参考官方 [NONOS SDK v3.0.4 的 AT/Nano 发布说明](https://github.com/espressif/ESP8266_NONOS_SDK/releases/tag/v3.0.4)，选择与真实容量和引脚匹配的固件；未在实物上验证前不把镜像标为已验收。

默认参数与 Android AP 预置相同：

| 项目 | 默认值 |
| --- | --- |
| SSID / 密码 | `BionicFish-AP` / `12345678` |
| AP / 网关 | `192.168.4.1` |
| 掩码 | `255.255.255.0` |
| 传输 | TCP server，端口 `9000`，单控制客户端 |

固件逐命令配置并等待自身响应，核查版本、串口和 AP 地址后创建服务，并完成固定 3 秒 TCP 空闲超时配置；不会用 ATE0 的 OK 代替 CIPSERVER 成功。收到 CONNECT 后记录连接 ID，拒绝额外控制客户端；CLOSED、ESP 重启、接收丢失或发送失败会撤销控制会话并进入失联安全态。

发送先等 CIPSEND 的 `>`，再发送声明的准确字节数，最后等 SEND OK。ACK/ERR 走事件 FIFO，STA 只有一个可替换的最新状态槽。不能确认 ESP 是否仍等待数据时，不把 AT+RST 或 +++ 盲发成载荷：诊断出现 `reset=1` 表示需要实际复位 ESP，观察到 `ready` 后重新初始化。详细兼容条件和异常回归见 `tests/esp_link/README.md`。

## 构建、烧录与验证

唯一固件源列表为 CMake，固定依赖及跨平台用法见 [scripts/BUILDING.md](scripts/BUILDING.md)。Windows 首次构建：

```powershell
python scripts/build.py firmware --bootstrap
python scripts/build.py host --bootstrap --host-cc C:/path/to/native-gcc.exe
```

输出 `build-firmware/bionic_fish.elf`、`.bin`、`.map` 和 `build-info.json`。当前没有已提交的 STM32 BIN/ELF/MAP，必须以本次源码构建产物为准。主机编译器用于 host 测试，Arm 编译器用于固件，不能相互替代。

ST-LINK 使用 PA13/PA14 和 GND。烧录脚本与手动步骤见 [scripts/README.md](scripts/README.md)。构建不会自动烧录。CubeMX 必须在独立目录生成后审核合并；不能只核对引脚数值就覆盖受维护的 Core 代码。

## 首次连接

1. 完成 USART2 交叉收发、ESP EN/供电、两条 I²C 外部 3.3V 上拉的检查。
2. 启动后在 USART3 观察 `ESP server=... tcp=... reset=... error=...`。`server=1` 表示已完成服务初始化，`tcp=1` 表示有控制客户端；它们不同于应用 CMD 保活。
3. 手机连接上述 AP 并保持无互联网 Wi-Fi，Android 设备页点击“AP 直连”。应用将 TCP Socket 绑定到 Wi-Fi，不需要通过关闭所有蜂窝数据解决路由。
4. 握手发送完整合法的 STOP CMD，收到同序号 ACK 与 `link=1` 的 STA 后才显示已连接。
5. 点击前进验证固定低速连续转动，再验证停止、舵机、姿态和失联停机。旧命令字段 `step_speed=60/100` 仍可解析，但不再改变换相频率；安卓已取消转速档位选择。

## 运行与故障排查

- `LNK`/`STA.link` 表示有效 CMD/幂等重传保活，不证明对端应用身份。正常保活不得重排电机换相。
- 1000 ms 无有效命令时停止步进、默认舵机回中、清除序号窗口并主动关闭旧 TCP，释放控制客户端槽位。UART 丢字节、TCP 断开和发送故障更早触发同一套停止处理。
- OLED 启动不再进行近一秒忙等。未初始化时后台轮流尝试两个地址；整组初始化在步进停止时执行。运行期 I²C 错误使屏幕失效，停止后重新初始化；原数据布局不变。
- PC13 正常每 500 ms 翻转；OLED 未初始化时每 120 ms 翻转。完整亮灭周期分别约 1 s / 240 ms。`I2CSCAN` 分步报告屏幕总线应答地址；`none` 表示当次扫描未发现应答，不能单凭它断言哪根线错误。
- OLED `E:` 是低 12 位十六进制；STA `err=` 是十进制故障位图，详见 [通信协议.md](通信协议.md)。
- AP 能加入但 TCP 失败：先看 `server`、AT 版本/错误及端口。TCP 已连接但 ACK/STA 验证失败：看 `tcp`、USART2 数据、帧尾及发送错误，并确认安装支持 `step_on` 和 RPM 为 `NA` 的新版 APK。
- `reset=1`：按诊断先解决串口/固件或供电问题，再实际复位 ESP；不能依赖未知发送模式下的“自动逃逸”。

## 验收边界

详见 [验收检查清单.md](验收检查清单.md)。本轮固定低速版本通过17/17 CTest及真实Arm交叉构建；BIN为24,604字节，Flash37.54%，RAM含堆栈预留7,816/20,480字节（38.16%）。产物哈希和工具版本见 `build-firmware/build-info.json`。

BIN SHA-256：`fca1a613510e9c4c26fbbda0048f04e6d488b8fb2967f8891cd74e6d925bdafb`。详细记录在 `build-firmware/`，链接告警及本机工具来源边界见构建说明。业务代码使用固定缓冲，无显式动态分配；不对 newlib 内部路径作未经实测的零堆承诺。

实板尚未验收，尤其是电机参数、实际换相波形、供电跌落、JY61P 寄存器和 OLED 热恢复。

本状态页是自写固定内存实现；没有复制或链接上游 Astra，详见 `third_party/oled-ui-astra.UPSTREAM.md`。本轮不做 Astra 移植、分页或按键交互。
