/**
 * @file app_config.h
 * @brief 本工程全部可调参数的唯一入口。
 *
 * 修改本文件前请先完成《硬件引脚与驱动说明.md》中的待确认项。尤其不能在
 * 未确认 TB6612 接法、步进电机额定电流和每输出转换相数时开启步进功能。
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

/* 周期与失联策略（均需整机试验后确认）。 */
#define CFG_LINK_TIMEOUT_MS                      1000UL
#define CFG_STATUS_PERIOD_MS                     200UL
#define CFG_JY61P_SAMPLE_PERIOD_MS               50UL
#define CFG_OLED_FRAME_PERIOD_MS                 200UL
#define CFG_BUTTON_DEBOUNCE_MS                   30UL
#define CFG_STEPPER_STOP_ON_LINK_TIMEOUT         1U
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
#define CFG_JY61P_MAX_DELTA_DDEG                 1800   /* 单个采样最大合理变化 180.0 度 */

/* TB6612 的 A/B 两个 H 桥全部用于一颗两相双极步进电机，不再支持 N20。
 * 代码保留完整四拍换相，但未知参数不能靠“42”外形自行推断。完成线圈、电流、
 * 步距角和减速比确认后，填写下面两项并将 PARAMETERS_CONFIRMED 改为 1。 */
#define CFG_STEPPER_DRIVER_ENABLED                1U
#define CFG_STEPPER_PARAMETERS_CONFIRMED          0U
#define CFG_STEPPER_COMMUTATIONS_PER_OUTPUT_REV   0UL
#define CFG_STEPPER_WINDING_PWM_PERCENT           0U
#define CFG_STEPPER_PHASE_REVERSED                0U
#define CFG_STEPPER_UNIDIRECTIONAL                1U

/* 板级逻辑电平。按键和 PC13 极性均必须与实际板卡核对。 */
#define CFG_BUTTON_ACTIVE_LOW                     1U
#define CFG_LED_ACTIVE_LOW                        1U

#if (CFG_STEPPER_DRIVER_ENABLED > 1U) || (CFG_STEPPER_PARAMETERS_CONFIRMED > 1U) || \
    (CFG_STEPPER_PHASE_REVERSED > 1U) || (CFG_STEPPER_UNIDIRECTIONAL > 1U) || \
    (CFG_STEPPER_STOP_ON_LINK_TIMEOUT > 1U) || (CFG_SERVO_CENTER_ON_LINK_TIMEOUT > 1U)
#error "布尔配置开关只能为 0 或 1。"
#endif

#if (CFG_STEPPER_WINDING_PWM_PERCENT > 100U)
#error "CFG_STEPPER_WINDING_PWM_PERCENT 必须在 0..100。"
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

#if (CFG_STEPPER_PARAMETERS_CONFIRMED != 0U) && \
    ((CFG_STEPPER_COMMUTATIONS_PER_OUTPUT_REV == 0UL) || \
     (CFG_STEPPER_WINDING_PWM_PERCENT == 0U))
#error "确认步进参数前必须填写每输出转换相数和安全线圈 PWM。"
#endif

#endif /* APP_CONFIG_H */
