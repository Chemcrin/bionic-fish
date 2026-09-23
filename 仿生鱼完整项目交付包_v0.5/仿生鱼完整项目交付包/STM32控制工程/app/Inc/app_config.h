/**
 * @file app_config.h
 * @brief 本工程全部可调参数的唯一入口。
 *
 * 电机采用固定间隔、低占空比换相，只提供开/停控制，不声明实测机械 RPM。
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* 时钟：外部晶振标称值必须与实际板卡一致；此处 8 MHz 为常见值，待实测。 */
#define CFG_HSE_VALUE_HZ                         8000000UL
#define CFG_PLL_MULTIPLIER                        9UL
#define CFG_SYSCLK_HZ                            72000000UL
#define CFG_APB1_DIVIDER                          2UL
#define CFG_APB2_DIVIDER                          1UL
/* F1 在 APB 预分频不为 1 时，通用定时器时钟为 PCLK 的两倍。 */
#define CFG_APB1_PCLK_HZ                          (CFG_SYSCLK_HZ / CFG_APB1_DIVIDER)
#define CFG_APB1_TIMER_CLOCK_HZ                   ((CFG_APB1_DIVIDER == 1UL) ? \
                                                     CFG_APB1_PCLK_HZ : (CFG_APB1_PCLK_HZ * 2UL))

/* 串口和协议资源。所有数组均为固定大小，不使用动态内存。 */
#define CFG_UART_BAUD                            115200UL
#define CFG_ESP_RX_RING_BYTES                    256U     /* 必须为 2 的幂 */
#define CFG_UART_TX_RING_BYTES                   512U     /* 必须为 2 的幂 */
#define CFG_PROTOCOL_PAYLOAD_MAX                 192U
#define CFG_PROTOCOL_MAX_FIELDS                  16U
#define CFG_PROTOCOL_FRAME_TIMEOUT_MS            250UL    /* 收到 '<' 后的最大组帧静默时间 */

/* ESP-01S AT 固件的独立 AP/TCP 连接参数。 */
#define CFG_ESP_AP_SSID                          "BionicFish-AP"
#define CFG_ESP_AP_PASSWORD                      "12345678"
#define CFG_ESP_AP_IP                            "192.168.4.1"
#define CFG_ESP_AP_NETMASK                       "255.255.255.0"
#define CFG_ESP_TCP_PORT                         9000U
/* TCP 服务端并发连接上限（AT+CIPSERVERMAXCONN）。
 * 必须是 4：浏览器一次请求至少会开两条连接（页面本体 + /favicon.ico），
 * 上限为 1 时第二条会被拒从而让页面加载不稳定。
 * 固件仍然只接受一个"控制客户端"，多余连接按原有 reject_mask 逻辑立即关闭。 */
#ifndef CFG_ESP_MAX_CLIENTS
#define CFG_ESP_MAX_CLIENTS                      4U
#endif

/* Wi-Fi 工作模式（AT+CWMODE）：
 *   2 = 仅 SoftAP（原行为；手机必须加入 BionicFish-AP，但手机会失去外网）
 *   3 = AP + STA 并存（推荐）：保留 BionicFish-AP 作兜底，同时让 ESP 加入下面的
 *       外部网络，于是同一网络内的 PC/手机可直接访问 <STA_IP>:9000。
 * 实测环境：ESP-01S，AT 1.7.4.0 / SDK 3.0.5-dev，8Mbit(512KB+512KB)。
 * 该模块的 AT+CWJAP 凭据掉电保持，AT+RST 后会自动重连并取回同一 IP。 */
#ifndef CFG_ESP_WIFI_MODE
#define CFG_ESP_WIFI_MODE                        3U
#endif

/* 外部网络（STA）凭据。仅在 CFG_ESP_WIFI_MODE 含 STA 位时使用。
 * ⚠️ 明文写入源码；对外交付前应清空为 ""，由使用者自行填写。 */
#ifndef CFG_ESP_STA_SSID
#define CFG_ESP_STA_SSID                         "CTGU-T-Web"
#endif
#ifndef CFG_ESP_STA_PASSWORD
#define CFG_ESP_STA_PASSWORD                     "c+gu-+-web209"
#endif

#if (CFG_ESP_WIFI_MODE != 2U) && (CFG_ESP_WIFI_MODE != 3U)
#error "CFG_ESP_WIFI_MODE 只能是 2（仅 AP）或 3（AP+STA）。"
#endif
/* 注意：C 预处理器无法比较字符串，因此这里**不做**"SSID 不能为空"的编译期断言。
 * 当前固件也尚未下发 AT+CWJAP_CUR，STA 侧依赖模块 flash 里已保存的凭据自动重连；
 * 上面两个宏目前只作为记录，供后续"由固件自己配网"时使用。 */

/* 周期与失联策略（均需整机试验后确认）。 */
#define CFG_LINK_TIMEOUT_MS                      1000UL
#define CFG_STATUS_PERIOD_MS                     200UL
#define CFG_JY61P_SAMPLE_PERIOD_MS               50UL
#define CFG_OLED_FRAME_PERIOD_MS                 200UL
#define CFG_OLED_RETRY_MS                        2000UL
#define CFG_OLED_BOOT_DELAY_MS                   200UL
#define CFG_BUTTON_DEBOUNCE_MS                   30UL
#define CFG_SERVO_CENTER_ON_LINK_TIMEOUT         1U

/* 调试模式：让板载 K1(PB0)/K2(PB1)/K3(PA8) 在上位机不可用时直接驱动执行器。
 *   K1 切换 M1 驱动：按一次开始转，再按一次停止
 *   K2 舵机左转 CFG_SERVO_SAFE_MIN_DEG（-15°）
 *   K3 舵机右转 CFG_SERVO_SAFE_MAX_DEG（+15°）
 * 三个键均为低电平有效、内部上拉，按"按下沿"触发一次动作。
 * 注意：没有任何按键能控制 M2。
 *
 * ⚠️ 它有意放开"无链路即无运动"这条看门狗保证：K1 启动的转动会一直持续，直到再按
 * 一次 K1、或上位机链路超时触发失效保护。上位机仍然拥有最高优先级——任何被接受的
 * 远端命令都会立刻覆盖本地动作。仅用于台架调试，下水前必须设为 0。 */
#ifndef CFG_DEBUG_BUTTONS_ENABLED
#define CFG_DEBUG_BUTTONS_ENABLED                1U
#endif

/* TIM2：驱动两路 TB6612 H 桥的 PWM 载波。
 *
 * 2026-09-23 变更：PWM 周期由 20 kHz（50 µs）改为 **2 s 一周期**（0.5 Hz）。
 * 原因：台架发现 TB6612 无法可靠驱动直流减速电机，怀疑 PWM 边沿过密导致
 * 桥臂无法稳定使能。改为慢周期后，一个周期内的高电平是「一整段连续导通」，
 * 电机看到的是近似直流，而不是 20 kHz 斩波。
 *
 * 计算：TIM2 在 APB1 上，APB1=SYSCLK/2=36 MHz 时定时器时钟为 72 MHz。
 *       PSC=7199 → 计数频率 = 72 MHz / 7200 = 10 kHz
 *       ARR=19999 → 周期 = 20000 / 10 kHz = 2 s
 * 占空比仍由 CCR 给出：满速 100% → CCR=ARR+1（恒高，整周期导通，
 * 慢周期下这就是「连续直流」）；低速挡 60% → CCR=ARR*60/100。
 *
 * ⚠️ 慢周期只对「>0 且 <100」的占空比有意义：此时会出现「导通一长段、
 * 断开一长段」的脉动，电机会明显一顿一顿。若不需要调速，优先用 100%。 */
#define CFG_MOTOR_PWM_HZ                         1UL     /* 名义占位：见下方静态校验 */
#define CFG_MOTOR_TIM_PRESCALER                  7199U
#define CFG_MOTOR_TIM_PERIOD                     19999U
/* 实际 PWM 频率（0.5 Hz = 2 s 周期）。供文档/测试引用，不参与定时器换算。 */
#define CFG_MOTOR_PWM_PERIOD_MS                  2000UL

/* TIM3：1 us 分辨率、20 ms 周期（50 Hz）。IP65 舵机的实际脉宽范围待实测。 */
#define CFG_SERVO_TIM_PRESCALER                  71U
#define CFG_SERVO_TIM_PERIOD                     19999U
#define CFG_SERVO_MIN_PULSE_US                   1000U
#define CFG_SERVO_CENTER_PULSE_US                1500U
#define CFG_SERVO_MAX_PULSE_US                   2000U
/* 电气满量程：1000-2000 µs 这整段脉宽**名义上**对应多少度机械角。
 * 它只用来把"指令角度"换算成"脉宽偏移"，本身不是行程上限。 */
#define CFG_SERVO_FULL_SCALE_DEG                 30
/* 允许的指令角度（安全行程）。2026-09-23 用户要求由 ±20° 收窄为 ±15°。
 * 换算基准仍是 FULL_SCALE_DEG=30，因此 ±15° 只给出满量程 1/2 的脉宽偏移
 * （±250 µs → 1250 / 1750 µs），而**不是**把 15° 撑满 1000-2000 µs：
 * 后者会让舵机实际转动的角度一点都没变小，只是标签从 30 改成 15。 */
#define CFG_SERVO_SAFE_MIN_DEG                   (-15)
#define CFG_SERVO_SAFE_MAX_DEG                   15

/* TIM4 是 1 MHz 自由运行时基；只用其更新中断扩展为 32 位微秒计数。 */
#define CFG_TIMEBASE_TIM_PRESCALER               71U
#define CFG_TIMEBASE_TIM_PERIOD                  65535U

/* 软件 I2C：GPIO 开漏，只能配合 3.3 V 上拉。half period 5 us 名义 100 kHz。
 * 软 I2C 每个字节仍有极短的忙等，OLED 以小块发送来限制单次占用。 */
#define CFG_SOFT_I2C_HALF_PERIOD_US              5U
#define CFG_SOFT_I2C_ACK_TIMEOUT_US              100U
#define CFG_SOFT_I2C_CLOCK_STRETCH_TIMEOUT_US    100U
#define CFG_OLED_ADDR_7BIT                       0x3CU  /* 待实测：常见 0x3C/0x3D */
#define CFG_OLED_CHUNK_BYTES                     8U

/* UART3 调试口的 IMU 诊断周期。输出空闲线电平、最近一次软件 I2C 结果、累计
 * 失败次数和最近一次原始字节，用于区分“缺上拉/接错线”与“地址或协议不符”。
 * 设为 0 显式关闭。它只读取已有状态，不额外发起任何 I2C 事务。 */
#ifndef CFG_IMU_DIAG_PERIOD_MS
#define CFG_IMU_DIAG_PERIOD_MS                   1000UL
#endif

/* JY61P I2C 寄存器模式：这些默认值来自公开示例，必须用实际固件手册/I2C
 * 扫描确认。若模块只暴露 MPU6050 原始寄存器，本驱动不得直接当作姿态角用。 */
#define CFG_JY61P_ADDR_7BIT                      0x50U  /* 待实测 */
#define CFG_JY61P_ANGLE_START_REG                0x3DU  /* roll/pitch/yaw 各 int16 LE，待确认 */
#define CFG_JY61P_MAX_CONSECUTIVE_FAILURES       3U
/* 相邻有效样本的宽松初值为 90 度/50 ms，必须在台架按实际运动校准。
 * 0 显式禁用；启用值必须小于环绕后的最大差值 1800，否则筛选永远不拒绝。
 * 跳变检查不能替代校验和，也不能证明未知寄存器映射正确。 */
#ifndef CFG_JY61P_MAX_DELTA_DDEG
#define CFG_JY61P_MAX_DELTA_DDEG                 900
#endif

/* 台架兼容开关：PB6/PB7 缺外部上拉时，释放总线改用 STM32 内部约 40 kΩ 上拉把线
 * 拉高，拉低时再切回开漏输出。它能在没有 4.7 kΩ 电阻的情况下先把总线跑起来，但
 * 驱动很弱、边沿慢、抗 EMI 差，只适合台架验证姿态数据，不适合下水。
 * 焊上真正的 4.7 kΩ 上拉后应改回 0，恢复标准的纯开漏时序。
 * 0 显式关闭；只影响 JY61P 总线，OLED 总线始终使用标准开漏。 */
#ifndef CFG_JY61P_INTERNAL_PULLUP
#define CFG_JY61P_INTERNAL_PULLUP                1U
#endif

#if (CFG_JY61P_INTERNAL_PULLUP > 1U)
#error "CFG_JY61P_INTERNAL_PULLUP 只能为 0 或 1。"
#endif

/* 两路直流减速电机（2026-09-12：推进方案由 42 两相双极步进改用直流减速电机）。
 *
 * TB6612 的两个 H 桥现在各驱动**一路有刷直流减速电机**：
 *   M1 = 主推进 370 直流减速电机      — 要满扭矩带动鱼尾，按 100% 占空比运行
 *   M2 = N20 直流减速电机（12V 版本） — 要正反转与启停，同样按 100% 占空比运行
 *
 * ⚠️ 占空比的物理含义是**平均电压**：TB6612 的 VM 接 12V，电机两端得到的是
 * 「12V × 占空比」。两路都是 12V 规格，所以默认都给 100%（≈12V 全压），
 * 不再需要靠 PWM 降压。若将来换成非 12V 规格的电机，先降占空比再上电。
 *
 * 直流电机不需要换相节拍：没有换相周期、没有每转步数、没有 RPM 换算、
 * 也没有位置闭环；启停与换向只由 IN1/IN2 两个 GPIO 决定。
 *
 * 注意 CFG_MOTOR_PWM_HZ / CFG_MOTOR_TIM_PRESCALER / CFG_MOTOR_TIM_PERIOD 仍在
 * 上方定义：直流电机的 PWM 载波依旧由 TIM2 提供，占空比经 CCR 给出。 */
#ifndef CFG_MOTOR_A_ENABLED
#define CFG_MOTOR_A_ENABLED                       1U
#endif
#ifndef CFG_MOTOR_B_ENABLED
#define CFG_MOTOR_B_ENABLED                       1U
#endif
/* TB6612 待机/使能脚（STBY）是否由 STM32 控制。
 *   0（默认）= 硬件把 STBY 硬接 3.3 V，固件不碰这个脚（历史行为）。
 *   1        = STBY 接到 PB5，固件在板级初始化时把它置高以解除待机。
 * 排障顺序里 STBY 是「代码全对但电机不转」的头号嫌疑：只要 STBY 为低，
 * 两个 H 桥输出全部高阻，PWM 和方向给得再全也没有输出。
 * 若把 STBY 改接到 PB5，请把此开关置 1 并在 BSP_Board_Init 之前完成接线。 */
#ifndef CFG_MOTOR_STBY_PIN_ENABLED
#define CFG_MOTOR_STBY_PIN_ENABLED               0U
#endif

/* 每路独立占空比。0 不合法：要停用某一路请用上面的 *_ENABLED=0。
 * 2026-09-23：新增 60% 低速挡，由网页端「低速挡」开关在运行时切换。
 *
 * ⚠️ 满速挡为何是 95% 而不是 100%：
 * 旧代码把 100% 落到 CCR = ARR+1，输出恒定高电平、**永不产生下降沿**。
 * TB6612 的 PWM 输入是驱动高侧栅极/自举的门极驱动信号，长时间没有任何
 * 开关边沿时高侧驱动可能无法持续建立，表现为「有 12V、STBY 也有效，却带不动
 * 负载」。改用 95% 后每个 2 s 周期都有一次完整的低电平沿，高侧驱动被持续刷新，
 * 同时 95% × 12 V = 11.4 V，转速与扭矩几乎无损，仍是「满速满扭矩」。
 * 若实测证明恒高也能稳定工作，可把 CFG_MOTOR_FULL_DUTY_PERCENT 改回 100。 */
#define CFG_MOTOR_FULL_DUTY_PERCENT              95U
#define CFG_MOTOR_LOW_DUTY_PERCENT               60U
#ifndef CFG_MOTOR_A_DUTY_PERCENT
#define CFG_MOTOR_A_DUTY_PERCENT                  CFG_MOTOR_FULL_DUTY_PERCENT
#endif
#ifndef CFG_MOTOR_B_DUTY_PERCENT
#define CFG_MOTOR_B_DUTY_PERCENT                  CFG_MOTOR_FULL_DUTY_PERCENT
#endif

/* 板级逻辑电平。按键和 PC13 极性均必须与实际板卡核对。 */
#define CFG_BUTTON_ACTIVE_LOW                     1U
#define CFG_LED_ACTIVE_LOW                        1U

#if (CFG_SERVO_CENTER_ON_LINK_TIMEOUT > 1U) || (CFG_DEBUG_BUTTONS_ENABLED > 1U)
#error "布尔配置开关只能为 0 或 1。"
#endif

#if (CFG_MOTOR_STBY_PIN_ENABLED != 0U) && (CFG_MOTOR_STBY_PIN_ENABLED != 1U)
#error "CFG_MOTOR_STBY_PIN_ENABLED 只能为 0 或 1。"
#endif

/* 两挡占空比必须落在 1..100，且低速挡不得高于满速挡。 */
#if (CFG_MOTOR_FULL_DUTY_PERCENT == 0U) || (CFG_MOTOR_FULL_DUTY_PERCENT > 100U)
#error "CFG_MOTOR_FULL_DUTY_PERCENT 必须在 1..100。"
#endif
#if (CFG_MOTOR_LOW_DUTY_PERCENT == 0U) || (CFG_MOTOR_LOW_DUTY_PERCENT > CFG_MOTOR_FULL_DUTY_PERCENT)
#error "CFG_MOTOR_LOW_DUTY_PERCENT 必须在 1..CFG_MOTOR_FULL_DUTY_PERCENT。"
#endif

/* 舵机门禁：一个决定"能转多远"，一个决定"多少度对应满脉宽"。配错会让安全行程
 * 静默地撑到电气边界以外，或者让"收窄行程"这件事根本没生效。 */
#if (CFG_SERVO_FULL_SCALE_DEG <= 0)
#error "CFG_SERVO_FULL_SCALE_DEG 必须为正：它是角度到脉宽的换算基准。"
#endif
#if (CFG_SERVO_SAFE_MIN_DEG >= 0) || (CFG_SERVO_SAFE_MAX_DEG <= 0)
#error "舵机安全行程必须跨越 0（MIN < 0 < MAX）。"
#endif
#if (CFG_SERVO_SAFE_MAX_DEG > CFG_SERVO_FULL_SCALE_DEG) || \
    ((-CFG_SERVO_SAFE_MIN_DEG) > CFG_SERVO_FULL_SCALE_DEG)
#error "安全行程不得超过 CFG_SERVO_FULL_SCALE_DEG，否则会请求超出电气范围的脉宽。"
#endif

/* 每路使能只接受 0/1：其它值会让 `if (CFG_MOTOR_x_ENABLED == 0U)` 静默变味。 */
#if (CFG_MOTOR_A_ENABLED != 0U) && (CFG_MOTOR_A_ENABLED != 1U)
#error "CFG_MOTOR_A_ENABLED 只能为 0 或 1。"
#endif
#if (CFG_MOTOR_B_ENABLED != 0U) && (CFG_MOTOR_B_ENABLED != 1U)
#error "CFG_MOTOR_B_ENABLED 只能为 0 或 1。"
#endif

/* 占空比 1..100；0 会把"运行"变成不励磁，禁用请走 *_ENABLED。 */
#if (CFG_MOTOR_A_DUTY_PERCENT == 0U) || (CFG_MOTOR_A_DUTY_PERCENT > 100U)
#error "CFG_MOTOR_A_DUTY_PERCENT 必须在 1..100；禁用 M1 请用 CFG_MOTOR_A_ENABLED=0。"
#endif
#if (CFG_MOTOR_B_DUTY_PERCENT == 0U) || (CFG_MOTOR_B_DUTY_PERCENT > 100U)
#error "CFG_MOTOR_B_DUTY_PERCENT 必须在 1..100；禁用 M2 请用 CFG_MOTOR_B_ENABLED=0。"
#endif

#if (CFG_JY61P_MAX_DELTA_DDEG < 0) || (CFG_JY61P_MAX_DELTA_DDEG >= 1800)
#error "姿态跳变筛选使用 0 禁用，或设置实测确认的 1..1799 ddeg 阈值。"
#endif

#if (CFG_JY61P_MAX_CONSECUTIVE_FAILURES == 0U) || (CFG_JY61P_MAX_CONSECUTIVE_FAILURES > 255U)
#error "JY61P 连续失败阈值必须在 1..255。"
#endif

#if (CFG_OLED_CHUNK_BYTES == 0U) || (CFG_OLED_CHUNK_BYTES > 128U)
#error "OLED 每次传输块必须在 1..128 字节。"
#endif

#if (CFG_PROTOCOL_FRAME_TIMEOUT_MS == 0UL)
#error "CFG_PROTOCOL_FRAME_TIMEOUT_MS 必须大于 0。"
#endif

/* 下列静态校验让时钟/PWM 名义值不是只写在文档中的孤立参数。TIM2/3/4 都在
 * APB1 上；当前 APB1=SYSCLK/2 时它们的实际输入时钟仍是 72 MHz。 */
#if ((CFG_HSE_VALUE_HZ * CFG_PLL_MULTIPLIER) != CFG_SYSCLK_HZ)
#error "HSE 与 PLL 倍频必须严格计算出 CFG_SYSCLK_HZ。"
#endif

#if ((CFG_SYSCLK_HZ / CFG_APB1_DIVIDER) > 36000000UL)
#error "STM32F103 的 APB1 PCLK 不得超过 36 MHz。"
#endif

/* TIM2 现在刻意运行在 0.5 Hz（2 s 周期），不再是 20 kHz。这里校验的是
 * 「计数频率 × 周期 = 2 s」这条换算链，而不是某个 PWM 频率常量。 */
#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_MOTOR_TIM_PRESCALER + 1UL) / \
      (CFG_MOTOR_TIM_PERIOD + 1UL)) != 0UL)
#error "TIM2 的 PSC/ARR 换算得到 0 Hz：整数除法被截断了。"
#endif
/* 2 s 周期 = 定时器时钟 / (PSC+1) / (ARR+1) 必须正好是 1/2 Hz。
 * 用「计数频率 × 周期秒数」的方式校验，避免浮点：10 kHz × 2 s = 20000 次计数。 */
#if (((CFG_APB1_TIMER_CLOCK_HZ / (CFG_MOTOR_TIM_PRESCALER + 1UL)) * \
      CFG_MOTOR_PWM_PERIOD_MS / 1000UL) != (CFG_MOTOR_TIM_PERIOD + 1UL))
#error "TIM2 的 PSC/ARR 与 CFG_MOTOR_PWM_PERIOD_MS(2 s) 不一致。"
#endif

#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_SERVO_TIM_PRESCALER + 1UL) / \
      (CFG_SERVO_TIM_PERIOD + 1UL)) != 50UL)
#error "TIM3 的 PSC/ARR 必须得到 50 Hz 舵机周期。"
#endif

#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_TIMEBASE_TIM_PRESCALER + 1UL)) != 1000000UL)
#error "TIM4 的 PSC 必须得到 1 MHz 微秒时基。"
#endif

#endif /* APP_CONFIG_H */
