#include "bsp_board.h"

#include "app_config.h"
#include "main.h"
#include "tim.h"

static bool s_pwm_started;

static uint32_t PercentToCompare(TIM_HandleTypeDef *timer, uint8_t duty_percent)
{
    uint32_t period = __HAL_TIM_GET_AUTORELOAD(timer) + 1UL;
    if (duty_percent > 100U) {
        duty_percent = 100U;
    }
    return (period * duty_percent) / 100UL;
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
    if ((HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_2) != HAL_OK) ||
        (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK)) {
        Error_Handler();
    }
    BSP_Board_SetStartupSafeState();
    s_pwm_started = true;
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
    ApplyBridgeA(state);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, PercentToCompare(&htim2, duty_percent));
}

void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty_percent)
{
    if (state == BSP_BRIDGE_COAST) {
        BSP_BridgeB_Coast();
        return;
    }
    ApplyBridgeB(state);
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_2, PercentToCompare(&htim2, duty_percent));
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
