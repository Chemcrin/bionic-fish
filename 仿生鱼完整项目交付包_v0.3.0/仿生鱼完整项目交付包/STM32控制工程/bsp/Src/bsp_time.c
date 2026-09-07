#include "bsp_time.h"

#include "stm32f1xx_hal.h"
#include "main.h"
#include "tim.h"

static volatile uint32_t s_tim4_overflows;

void BSP_Time_Init(void)
{
    s_tim4_overflows = 0U;
    if (HAL_TIM_Base_Start_IT(&htim4) != HAL_OK) {
        Error_Handler();
    }
}

void BSP_Time_OnTim4Overflow(void)
{
    s_tim4_overflows++;
}

uint32_t BSP_Millis(void)
{
    return HAL_GetTick();
}

uint32_t BSP_Micros(void)
{
    uint32_t high_before;
    uint32_t high_after;
    uint16_t low;

    /* 处理读取 TIM4 CNT 时恰好发生更新中断的竞争；Cortex-M3 对 32 位读写原子。 */
    do {
        high_before = s_tim4_overflows;
        low = (uint16_t)__HAL_TIM_GET_COUNTER(&htim4);
        high_after = s_tim4_overflows;
    } while (high_before != high_after);
    return (high_after << 16U) | low;
}

void BSP_DelayUs(uint16_t microseconds)
{
    uint32_t started_at = BSP_Micros();
    /* 只服务于软件 I2C 的 1..100 us 位时序；主循环中严禁使用 HAL_Delay。 */
    while ((uint32_t)(BSP_Micros() - started_at) < (uint32_t)microseconds) {
        __NOP();
    }
}
