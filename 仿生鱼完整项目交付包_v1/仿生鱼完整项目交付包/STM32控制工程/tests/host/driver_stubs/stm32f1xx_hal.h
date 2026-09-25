/* 仅供驱动主机测试的 GPIO 类型占位；不替代任何固件 HAL 实现。 */
#ifndef HOST_DRIVER_STM32F1XX_HAL_H
#define HOST_DRIVER_STM32F1XX_HAL_H

#include <stdint.h>
typedef struct { uint32_t unused; } GPIO_TypeDef;

#endif
