/** @file bsp_time.h @brief HAL 毫秒时基之外的、可回绕比较的微秒时基。 */
#ifndef BSP_TIME_H
#define BSP_TIME_H

#include <stdint.h>

void BSP_Time_Init(void);
void BSP_Time_OnTim4Overflow(void);
uint32_t BSP_Millis(void);
uint32_t BSP_Micros(void);
void BSP_DelayUs(uint16_t microseconds);

#endif /* BSP_TIME_H */
