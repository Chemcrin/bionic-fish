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

/* 周期与失联策略（均需整机试验后确认）。 */
#define CFG_LINK_TIMEOUT_MS                      1000UL
#define CFG_STATUS_PERIOD_MS                     200UL
#define CFG_JY61P_SAMPLE_PERIOD_MS               50UL
#define CFG_OLED_FRAME_PERIOD_MS                 200UL
#define CFG_OLED_RETRY_MS                        2000UL
#define CFG_OLED_BOOT_DELAY_MS                   200UL
#define CFG_BUTTON_DEBOUNCE_MS                   30UL
#define CFG_SERVO_CENTER_ON_LINK_TIMEOUT         1U

/* TIM2：APB1=36 MHz、分频器为 1 时，定时器时钟为 72 MHz。
 * PSC=0、ARR=3599 得 20 kHz。TB6612 可接受，但仍须结合电机/EMI 实测。 */
#define CFG_MOTOR_PWM_HZ                         20000UL
#define CFG_MOTOR_TIM_PRESCALER                  0U
#define CFG_MOTOR_TIM_PERIOD                     3599U

/* TIM3：1 us 分辨率、20 ms 周期（50 Hz）。IP65 舵机的实际脉宽范围待实测。 */
#define CFG_SERVO_TIM_PRESCALER                  71U
#define CFG_SERVO_TIM_PERIOD                     19999U
#define CFG_SERVO_MIN_PULSE_US                   1000U
#define CFG_SERVO_CENTER_PULSE_US                1500U
#define CFG_SERVO_MAX_PULSE_US                   2000U
#define CFG_SERVO_SAFE_MIN_DEG                   (-30)
#define CFG_SERVO_SAFE_MAX_DEG                   30

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

/* 两桥继续四拍换相；默认每 50 ms 换一相、10% PWM，收到前进命令后持续运行。
 * 这是固定低速输出设置，不依赖每转步数，也不把旧协议的 60/100 当作 RPM。 */
#ifndef CFG_STEPPER_DRIVER_ENABLED
#define CFG_STEPPER_DRIVER_ENABLED                1U
#endif
#ifndef CFG_STEPPER_COMMUTATION_PERIOD_US
#define CFG_STEPPER_COMMUTATION_PERIOD_US         50000UL
#endif
#ifndef CFG_STEPPER_WINDING_PWM_PERCENT
#define CFG_STEPPER_WINDING_PWM_PERCENT           10U
#endif
#define CFG_STEPPER_PHASE_REVERSED                0U
#define CFG_STEPPER_UNIDIRECTIONAL                1U

/* 板级逻辑电平。按键和 PC13 极性均必须与实际板卡核对。 */
#define CFG_BUTTON_ACTIVE_LOW                     1U
#define CFG_LED_ACTIVE_LOW                        1U

#if (CFG_STEPPER_DRIVER_ENABLED > 1U) || \
    (CFG_STEPPER_PHASE_REVERSED > 1U) || (CFG_STEPPER_UNIDIRECTIONAL > 1U) || \
    (CFG_SERVO_CENTER_ON_LINK_TIMEOUT > 1U)
#error "布尔配置开关只能为 0 或 1。"
#endif

#if (CFG_STEPPER_WINDING_PWM_PERCENT == 0U) || (CFG_STEPPER_WINDING_PWM_PERCENT > 100U)
#error "电机 PWM 必须在 1..100；禁用驱动使用 CFG_STEPPER_DRIVER_ENABLED=0。"
#endif

#if (CFG_STEPPER_COMMUTATION_PERIOD_US == 0UL) || (CFG_STEPPER_COMMUTATION_PERIOD_US > 0x7FFFFFFFUL)
#error "换相间隔必须在 1..0x7FFFFFFF us，满足 uint32_t 回绕调度边界。"
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

#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_MOTOR_TIM_PRESCALER + 1UL) / \
      (CFG_MOTOR_TIM_PERIOD + 1UL)) != CFG_MOTOR_PWM_HZ)
#error "TIM2 的 PSC/ARR 与 CFG_MOTOR_PWM_HZ 不一致。"
#endif

#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_SERVO_TIM_PRESCALER + 1UL) / \
      (CFG_SERVO_TIM_PERIOD + 1UL)) != 50UL)
#error "TIM3 的 PSC/ARR 必须得到 50 Hz 舵机周期。"
#endif

#if ((CFG_APB1_TIMER_CLOCK_HZ / (CFG_TIMEBASE_TIM_PRESCALER + 1UL)) != 1000000UL)
#error "TIM4 的 PSC 必须得到 1 MHz 微秒时基。"
#endif

#endif /* APP_CONFIG_H */
