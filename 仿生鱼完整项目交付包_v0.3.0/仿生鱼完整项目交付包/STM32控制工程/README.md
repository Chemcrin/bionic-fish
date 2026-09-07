# 仿生鱼 STM32F103C8T6 控制工程

本工程为 STM32F103C8T6 的 HAL/CubeMX 风格控制基线，按 `bsp`、`drivers`、`app`、`protocol`、`ui`、`control` 分层。它的目标是让样机能被逐项验证，而不是用未经确认的硬件参数制造“已经可用”的假象。

> 重要硬件调整：TB6612 的 A、B 两个 H 桥现在分别驱动一颗两相双极步进电机的线圈 A、线圈 B：A01/A02（M1 端）只接线圈 A，B01/B02（M2 端）只接线圈 B。原 N20 电机和抽吸机构必须从 M2 端物理断开；本工程不再有 N20 控制、抽吸逻辑或 N20 状态字段。

> 重要安全门禁：默认 `CFG_STEPPER_PARAMETERS_CONFIRMED=0`、`CFG_STEPPER_COMMUTATIONS_PER_OUTPUT_REV=0`、`CFG_STEPPER_WINDING_PWM_PERCENT=0`。因此即便收到 60/100 RPM 命令，固件也会保持两桥 coast（无励磁）并报告步进硬件未确认。只有完成线圈、电流、步距角、减速比、每输出转换相数、安全 PWM 和 `STBY` 的实测确认后，才能解除该门禁。

## 功能范围

- TB6612 A/B 两桥以四拍全步换相驱动一颗两相双极步进电机；命令档位为 60 RPM 和 100 RPM。当前默认只允许 `move=F` 单方向，`move=R` 会明确拒绝；若以后经硬件确认允许反向，必须显式修改配置并复测相序。
- PA6 输出 IP65 舵机脉冲，软件限制相对中位的 -30°、0°、+30°范围。真实中心脉宽、机械限位和 3.3 V 信号兼容性仍待实测。
- PB8/PB9 为 SSD1306 的独立软件 I²C；PB6/PB7 为 JY61P 的独立软件 I²C。两条总线均使用 GPIO 开漏和超时/恢复逻辑。
- ESP-01S 经 USART2（PA2/PA3）接收 ASCII 控制帧；CH340N 经 USART3（PB10/PB11）以 115200-8-N-1 输出调试/状态帧。
- OLED 以固定内存、分块传输和服务式刷新显示步进命令、舵机、姿态、链路与故障。它是对 `oled-ui-astra` 信息层级/非阻塞思路的兼容性重构，不是上游代码的原样移植。
- 未接入编码器、霍尔或外部测速时，`step_rpm`/`step_est` 只能是目标值或换相节拍估算；状态帧固定报告 `step_actual=NA`，不能称为真实机械转速。

## 工程目录与关键文件

```text
bionic-fish-stm32f103c8t6/
├─ Core/
│  ├─ Inc/main.h                    # 唯一业务引脚定义
│  └─ Src/main.c                    # CubeMX 初始化后的主循环入口
├─ app/
│  ├─ Inc/app_config.h              # 时钟、时序、门禁和安全策略的唯一配置入口
│  └─ Src/app.c                     # 主循环服务、失联/溢出安全处理、状态汇总
├─ bsp/
│  └─ Src/
│     ├─ bsp_board.c                # 两桥 coast/换相、舵机、按键、LED
│     ├─ bsp_time.c                 # 毫秒/微秒时基
│     └─ bsp_uart.c                 # USART2/USART3 非阻塞收发
├─ control/
│  └─ Src/
│     ├─ control_arbiter.c          # 序号、语义校验、失联会话
│     ├─ motor_control.c            # 双桥两相全步换相与参数门禁
│     └─ servo_control.c            # 角度/脉宽双重限幅
├─ drivers/
│  └─ Src/
│     ├─ soft_i2c.c                 # 软件 I²C、ACK 超时、9 脉冲恢复
│     ├─ jy61p.c                    # JY61P 探测、读取、合理性检查
│     └─ ssd1306.c                  # 固定缓冲 SSD1306 驱动
├─ protocol/
│  └─ Src/
│     ├─ ring_buffer.c              # 固定大小 UART 环形缓冲
│     └─ ascii_protocol.c           # CMD/ACK/ERR/STA 编解码
├─ ui/Src/oled_ui.c                 # 页面状态、脏刷新和分块服务
├─ tests/host/Src/protocol_host_test.c
├─ bionic_fish.ioc                  # CubeMX 外设/引脚配置
├─ CMakeLists.txt                   # 主机协议测试和可选交叉编译
└─ cmake/                           # Arm GNU Toolchain/链接脚本
```

中断/DMA 回调只能放入/取出固定大小缓冲、置标志和重新挂接接收；协议解析、换相调度、OLED 发送与 I²C 恢复均在主循环执行。工程不使用动态内存，也不允许在中断中调用 `HAL_Delay`。

## 编译与 CubeMX 配置

### 主机侧协议检查

主机检查只构建不依赖 HAL 的协议/仲裁逻辑，适合首先验证帧格式、序号和边界条件：

```powershell
cmake -S . -B build-host
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

本次交付已在 2026-09-03 使用主机 C 编译器以 `-Wall -Wextra -Werror` 运行同一测试，结果为 `protocol_host_test: PASS`；全量业务源码也已与 HAL 桩完成零诊断语法/链接检查。该结果不代替 Arm GNU Toolchain 或 CubeIDE 的目标板交叉编译。

### STM32 交叉编译

完整固件需要 Arm GNU Toolchain 和已安装的 STM32CubeF1 固件包。以下路径必须替换为本机实际路径：

```powershell
cmake -S . -B build-firmware `
  -DCMAKE_TOOLCHAIN_FILE=cmake/arm-none-eabi-gcc.cmake `
  -DBIONIC_FISH_BUILD_FIRMWARE=ON `
  -DSTM32CUBE_F1_PATH=C:/path/to/STM32Cube_FW_F1_Vx.y.z
cmake --build build-firmware
```

也可用 STM32CubeIDE 打开/生成 `bionic_fish.ioc` 对应工程。重新生成前后必须核对 `main.h`、`gpio.c`、`tim.c`、`usart.c` 和 `app_config.h` 的数值一致；不能只因 CubeMX 没有报冲突就假定电机接法正确。

### 烧录

使用 ST-LINK 经 PA13/PA14（SWDIO/SWCLK）和 GND 连接目标板，在 STM32CubeProgrammer 或 STM32CubeIDE 中下载构建得到的 `bionic_fish.elf`/`bionic_fish.bin`。第一次烧录只接 MCU 逻辑电源，不接步进电机电源；下载后先观察 PC13、USART3 和 `STA.err`，确认 `BF_FAULT_STEPPER_HW_UNCONFIRMED`（bit4）存在且两桥未励磁，才进行后续接线检查。

| 项目 | 当前配置基线 | 说明 |
| --- | --- | --- |
| 时钟 | HSE 8 MHz、PLL ×9、SYSCLK 72 MHz | HSE 标称/起振仍待板卡实测。 |
| 电机 PWM | TIM2 CH1=PA0、CH2=PA1，20 kHz | A/B 两个通道均属于同一颗步进电机；PWM 不是恒流控制。 |
| 舵机 PWM | TIM3 CH1=PA6，50 Hz，1/1.5/2 ms 初值 | 脉宽只是不经实测的初始标定值。 |
| 微秒时基 | TIM4，1 MHz | 供软件 I²C 与换相调度使用。 |
| 控制链路 | USART2/USART3 115200；帧超时 250 ms，失联超时 1000 ms | 周期和超时均待整机实测。 |
| 软件 I²C | 5 µs 半周期（名义 100 kHz） | 上拉、线长、地址和时钟拉伸能力待确认。 |
| 步进门禁 | `PARAMETERS_CONFIRMED=0`、换相数/PWM=0 | 默认禁止两桥输出，避免未知线圈通电。 |

## 首次上电顺序

1. 先将原 N20 电机从 M2/B01/B02 端物理断开，并用万用表确认 M1 端与 M2 端分别只连到同一颗步进电机的两组独立线圈。
2. 仅接 MCU、SWD 和 3.3 V 逻辑电源，确认 HSE、SysTick、PC13、下载与复位行为。
3. 测 PA0、PA1、PB12~PB15 的空载波形和安全 coast 初始态；在 `STBY` 未确认前不得接入电机电源。
4. 接 OLED，确认其 VCC、SCL、SDA 上拉全部是 3.3 V，再确认地址和刷新。
5. 用 CH340N 验证 USART3，再验证 ESP-01S 与 USART2 的交叉收发和供电压降。
6. 接 JY61P 前确认它真的处于 I²C 模式，且 5 V 供电时 SDA/SCL 不会将 PB6/PB7 拉到超过 3.3 V。
7. 最后才连接步进电机，并保持参数确认门禁关闭。先确认线圈对、极性、额定电流和 `STBY`，再低占空比、无机械负载、短时地验证相序、温升和转速。

所有模块必须共地。外部输入不得超过 3.3 V；JY61P 标有 5 V 供电并不代表其 I²C 逻辑一定安全，舵机电源也不得反向把 5 V 信号送入 PA6。

## 运行边界与已知限制

- TB6612 是双全桥直流驱动器，不是带 STEP/DIR、微步和恒流斩波的专用步进驱动器。本工程明确使用 A/B 两桥做四拍全步换相，不会虚构不存在的 STEP/DIR 引脚。
- “42 型”只说明外形等级，不能推出线数、线圈对、步距角、每转步数、减速比、额定电流或额定电压。上述所有参数均为待实测/待确认。
- `STBY` 没有已确认的 STM32 控制脚。它必须在硬件上可靠使能，且掉电、复位、HardFault 时应有独立的安全关断路径；软件 coast 不能代替硬件急停。
- 当前失联和 ESP RX 溢出会停止步进；舵机是否回中由 `CFG_SERVO_CENTER_ON_LINK_TIMEOUT` 决定，默认值为 1。即使如此，实际水中/无人值守使用仍需要完成硬件失效保护验证。
- JY61P 的 I²C 地址、寄存器、字节序、校验和 5 V 逻辑高电平均待确认。其未接出的 RX/TX 绝不会被误用为 STM32 UART。
- 上游 `oled-ui-astra` 参考仓库具有不同的引脚、依赖和 GPL-3.0 边界；详见 `third_party/oled-ui-astra.UPSTREAM.md`。本工程没有直接复制或链接其上游源码。

## 关联文档

- [通信协议.md](通信协议.md)：命令/状态帧、序号、错误码和超时策略。
- [硬件引脚与驱动说明.md](硬件引脚与驱动说明.md)：逐引脚核对、两桥两相接法和电平边界。
- [验收检查清单.md](验收检查清单.md)：编译、接线、参数确认和实测放行条件。
