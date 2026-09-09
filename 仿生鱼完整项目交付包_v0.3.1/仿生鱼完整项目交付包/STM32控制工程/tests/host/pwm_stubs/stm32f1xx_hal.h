#ifndef HOST_PWM_STM32F1XX_HAL_H
#define HOST_PWM_STM32F1XX_HAL_H

#include <stdint.h>

typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR } HAL_StatusTypeDef;
typedef struct { uint32_t ODR; } GPIO_TypeDef;
/* No counter, EGR or CCMR write interface: the tested BSP may only update CCR. */
typedef struct { unsigned int identity; } TIM_HandleTypeDef;

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
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 4U
#define TIM_FLAG_UPDATE 1U
#define RESET 0U

uint32_t HostPwmGetAutoreload(TIM_HandleTypeDef *timer);
uint32_t HostPwmGetCompare(TIM_HandleTypeDef *timer, uint32_t channel);
void HostPwmSetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);
void HostPwmClearFlag(TIM_HandleTypeDef *timer, uint32_t flag);
uint32_t HostPwmGetFlag(TIM_HandleTypeDef *timer, uint32_t flag);
#define __HAL_TIM_GET_AUTORELOAD(timer) HostPwmGetAutoreload(timer)
#define __HAL_TIM_GET_COMPARE(timer, channel) HostPwmGetCompare(timer, channel)
#define __HAL_TIM_SET_COMPARE(timer, channel, value) HostPwmSetCompare(timer, channel, value)
#define __HAL_TIM_CLEAR_FLAG(timer, flag) HostPwmClearFlag(timer, flag)
#define __HAL_TIM_GET_FLAG(timer, flag) HostPwmGetFlag(timer, flag)

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin);

#endif
