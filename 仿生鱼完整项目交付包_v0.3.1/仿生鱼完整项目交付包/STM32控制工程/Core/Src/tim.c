#include "tim.h"

#include "app_config.h"

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

void MX_TIM2_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim2.Instance = TIM2;
    htim2.Init.Prescaler = CFG_MOTOR_TIM_PRESCALER;
    htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim2.Init.Period = CFG_MOTOR_TIM_PERIOD;
    htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim2) != HAL_OK) {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 0U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if ((HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) ||
        (HAL_TIM_PWM_ConfigChannel(&htim2, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)) {
        Error_Handler();
    }
    /* HAL enables OC1/OC2 preload independently of AutoReloadPreload above.
     * Keep it enabled; the BSP waits for duty changes before driving a bridge. */
    HAL_TIM_MspPostInit(&htim2);
}

void MX_TIM3_Init(void)
{
    TIM_OC_InitTypeDef sConfigOC = {0};

    htim3.Instance = TIM3;
    htim3.Init.Prescaler = CFG_SERVO_TIM_PRESCALER;
    htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim3.Init.Period = CFG_SERVO_TIM_PERIOD;
    htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_PWM_Init(&htim3) != HAL_OK) {
        Error_Handler();
    }
    sConfigOC.OCMode = TIM_OCMODE_PWM1;
    sConfigOC.Pulse = 1500U;
    sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
    sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
    if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK) {
        Error_Handler();
    }
    HAL_TIM_MspPostInit(&htim3);
}

void MX_TIM4_Init(void)
{
    htim4.Instance = TIM4;
    htim4.Init.Prescaler = CFG_TIMEBASE_TIM_PRESCALER;
    htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim4.Init.Period = CFG_TIMEBASE_TIM_PERIOD;
    htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&htim4) != HAL_OK) {
        Error_Handler();
    }
}
