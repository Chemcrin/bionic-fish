#include "bsp_board.h"

#include "app_config.h"
#include "bsp_time.h"
#include "main.h"
#include "tim.h"

static bool s_pwm_started;
static BspDutyMode s_duty_mode = BSP_DUTY_FULL;

/* 占空比挡位对应的百分比。BSP_DutyPercentFor 是唯一映射点。 */
uint8_t BSP_DutyPercentFor(BspDutyMode mode)
{
    return (mode == BSP_DUTY_LOW) ? (uint8_t)CFG_MOTOR_LOW_DUTY_PERCENT
                                  : (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT;
}

static uint32_t PercentToCompare(TIM_HandleTypeDef *timer, uint8_t duty_percent)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(timer) + 1UL;
    if (duty_percent > 100U) {
        duty_percent = 100U;
    }
    return (period * duty_percent) / 100UL;
}

static bool SetBridgeCompare(uint32_t channel, uint8_t duty_percent)
{
    uint32_t compare = PercentToCompare(&htim2, duty_percent);

    if (__HAL_TIM_GET_COMPARE(&htim2, channel) == compare) {
        return true;
    }
    /* HAL enables OC preload: a CCR write alone leaves the old duty active.
     *
     * 2026-09-23：TIM2 周期已改为 2 s，**不能再等自然更新事件**——一次等待
     * 最长要 2 秒，会把控制循环彻底卡死。改为写 CCR 后主动触发一次更新事件
     * (UG) 立即把影子寄存器装载到活动寄存器：新占空比立刻生效。
     * UG 会让计数器从零重启，两桥共用 TIM2 因而相位一起重置——本工程两路
     * 电机本就允许同时重新起相位，无副作用。 */
    __HAL_TIM_SET_COMPARE(&htim2, channel, compare);
    (void)HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);
    return true;
}

static void SetAInputs(GPIO_PinState in1, GPIO_PinState in2)
{
    HAL_GPIO_WritePin(TB6612_AIN1_GPIO_Port, TB6612_AIN1_Pin, in1);
    HAL_GPIO_WritePin(TB6612_AIN2_GPIO_Port, TB6612_AIN2_Pin, in2);
}

static void SetBInputs(GPIO_PinState in1, GPIO_PinState in2)
{
    HAL_GPIO_WritePin(TB6612_BIN1_GPIO_Port, TB6612_BIN1_Pin, in1);
    HAL_GPIO_WritePin(TB6612_BIN2_GPIO_Port, TB6612_BIN2_Pin, in2);
}

static void ApplyBridgeA(BspBridgeState state)
{
    switch (state) {
    case BSP_BRIDGE_FORWARD:
        SetAInputs(GPIO_PIN_SET, GPIO_PIN_RESET);
        break;
    case BSP_BRIDGE_REVERSE:
        SetAInputs(GPIO_PIN_RESET, GPIO_PIN_SET);
        break;
    case BSP_BRIDGE_BRAKE:
        SetAInputs(GPIO_PIN_SET, GPIO_PIN_SET);
        break;
    case BSP_BRIDGE_COAST:
    default:
        SetAInputs(GPIO_PIN_RESET, GPIO_PIN_RESET);
        break;
    }
}

static void ApplyBridgeB(BspBridgeState state)
{
    switch (state) {
    case BSP_BRIDGE_FORWARD:
        SetBInputs(GPIO_PIN_SET, GPIO_PIN_RESET);
        break;
    case BSP_BRIDGE_REVERSE:
        SetBInputs(GPIO_PIN_RESET, GPIO_PIN_SET);
        break;
    case BSP_BRIDGE_BRAKE:
        SetBInputs(GPIO_PIN_SET, GPIO_PIN_SET);
        break;
    case BSP_BRIDGE_COAST:
    default:
        SetBInputs(GPIO_PIN_RESET, GPIO_PIN_RESET);
        break;
    }
}

void BSP_Board_Init(void)
{
#if (CFG_MOTOR_STBY_PIN_ENABLED == 1U)
    /* STBY 必须先置高、再启动 PWM：若 STBY 为低，两桥输出高阻，
     * 此时给方向与占空比也不会有任何输出。 */
    {
        GPIO_InitTypeDef stby = {0};
        __HAL_RCC_GPIOB_CLK_ENABLE();
        HAL_GPIO_WritePin(TB6612_STBY_GPIO_Port, TB6612_STBY_Pin, GPIO_PIN_SET);
        stby.Pin = TB6612_STBY_Pin;
        stby.Mode = GPIO_MODE_OUTPUT_PP;
        stby.Pull = GPIO_NOPULL;
        stby.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(TB6612_STBY_GPIO_Port, &stby);
    }
#endif
    if ((HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)) {
        Error_Handler();
    }
    BSP_Board_SetStartupSafeState();
    s_pwm_started = true;
}

void BSP_Board_SetDutyMode(BspDutyMode mode)
{
    s_duty_mode = (mode == BSP_DUTY_LOW) ? BSP_DUTY_LOW : BSP_DUTY_FULL;
    /* 已经在转的那一路要立刻按新挡位改写占空比；coast 的桥保持不动。 */
    if (__HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_1) !=
        PercentToCompare(&htim2, 100U)) {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1,
                              PercentToCompare(&htim2, BSP_DutyPercentFor(s_duty_mode)));
    }
    if (__HAL_TIM_GET_COMPARE(&htim2, TIM_CHANNEL_2) !=
        PercentToCompare(&htim2, 100U)) {
        __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2,
                              PercentToCompare(&htim2, BSP_DutyPercentFor(s_duty_mode)));
    }
    (void)HAL_TIM_GenerateEvent(&htim2, TIM_EVENTSOURCE_UPDATE);
}

BspDutyMode BSP_Board_GetDutyMode(void)
{
    return s_duty_mode;
}

void BSP_Board_SetStartupSafeState(void)
{
    /* TB6612 表中 IN1=IN2=L、PWM=H 为 coast；PWM=L 不是“断电”，可能是
     * short brake。因此安全停机统一走 coast，而不是只把 PWM 写 0。 */
    BSP_BridgeA_Coast();
    BSP_BridgeB_Coast();
    BSP_Servo_SetPulseUs(CFG_SERVO_CENTER_PULSE_US);
    BSP_RunLed_Set(false);
}

void BSP_Board_EmergencyCoast(void)
{
    /* 仅在 PWM 成功启动后执行。它用于普通 Error_Handler 的尽力而为降险，
     * 不能替代 HardFault/掉电时的独立 STBY 或硬件关断。 */
    if (s_pwm_started) {
        BSP_BridgeA_Coast();
        BSP_BridgeB_Coast();
    }
}

void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty_percent)
{
    if (state == BSP_BRIDGE_COAST) {
        BSP_BridgeA_Coast();
        return;
    }
    if (SetBridgeCompare(TIM_CHANNEL_1, duty_percent)) {
        ApplyBridgeA(state);
    }
}

void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty_percent)
{
    if (state == BSP_BRIDGE_COAST) {
        BSP_BridgeB_Coast();
        return;
    }
    if (SetBridgeCompare(TIM_CHANNEL_2, duty_percent)) {
        ApplyBridgeB(state);
    }
}

void BSP_BridgeA_Coast(void)
{
    ApplyBridgeA(BSP_BRIDGE_COAST);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, PercentToCompare(&htim2, 100U));
}

void BSP_BridgeB_Coast(void)
{
    ApplyBridgeB(BSP_BRIDGE_COAST);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, PercentToCompare(&htim2, 100U));
}

void BSP_Servo_SetPulseUs(uint16_t pulse_us)
{
    if (pulse_us < CFG_SERVO_MIN_PULSE_US) {
        pulse_us = CFG_SERVO_MIN_PULSE_US;
    } else if (pulse_us > CFG_SERVO_MAX_PULSE_US) {
        pulse_us = CFG_SERVO_MAX_PULSE_US;
    }
    /* TIM3 已配置 1 us / count，CCR 即高电平脉宽。 */
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pulse_us);
}

bool BSP_ButtonPressed(uint8_t index)
{
    GPIO_TypeDef *port = 0;
    uint16_t pin = 0U;
    GPIO_PinState level;
    if (index == 1U) {
        port = KEY1_GPIO_Port;
        pin = KEY1_Pin;
    } else if (index == 2U) {
        port = KEY2_GPIO_Port;
        pin = KEY2_Pin;
    } else if (index == 3U) {
        port = KEY3_GPIO_Port;
        pin = KEY3_Pin;
    } else {
        return false;
    }
    level = HAL_GPIO_ReadPin(port, pin);
    return CFG_BUTTON_ACTIVE_LOW ? (level == GPIO_PIN_RESET) : (level == GPIO_PIN_SET);
}

void BSP_RunLed_Set(bool on)
{
    GPIO_PinState level = on ? GPIO_PIN_SET : GPIO_PIN_RESET;
    if (CFG_LED_ACTIVE_LOW) {
        level = on ? GPIO_PIN_RESET : GPIO_PIN_SET;
    }
    HAL_GPIO_WritePin(RUN_LED_GPIO_Port, RUN_LED_Pin, level);
}

void BSP_RunLed_Toggle(void)
{
    HAL_GPIO_TogglePin(RUN_LED_GPIO_Port, RUN_LED_Pin);
}
