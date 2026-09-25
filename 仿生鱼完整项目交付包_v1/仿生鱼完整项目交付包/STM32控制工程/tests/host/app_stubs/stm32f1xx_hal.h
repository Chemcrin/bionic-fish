/* Hardware types only. Application/control/protocol/UI code is compiled unchanged. */
#ifndef HOST_APP_STM32F1XX_HAL_H
#define HOST_APP_STM32F1XX_HAL_H
#include <stdint.h>

typedef struct { uint32_t unused; } GPIO_TypeDef;
typedef struct { uint32_t unused; } UART_HandleTypeDef;
typedef struct {
    struct { uint32_t Prescaler; uint32_t Period; } Init;
} TIM_HandleTypeDef;

extern GPIO_TypeDef host_gpio_a, host_gpio_b, host_gpio_c;
#define GPIOA (&host_gpio_a)
#define GPIOB (&host_gpio_b)
#define GPIOC (&host_gpio_c)
#define GPIO_PIN_0  (1U << 0)
#define GPIO_PIN_1  (1U << 1)
#define GPIO_PIN_6  (1U << 6)
#define GPIO_PIN_7  (1U << 7)
#define GPIO_PIN_8  (1U << 8)
#define GPIO_PIN_9  (1U << 9)
#define GPIO_PIN_12 (1U << 12)
#define GPIO_PIN_13 (1U << 13)
#define GPIO_PIN_14 (1U << 14)
#define GPIO_PIN_15 (1U << 15)
#endif
