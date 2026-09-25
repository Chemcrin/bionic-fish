/**
 * @file stm32f1xx_hal_conf.h
 * @brief STM32CubeF1 HAL 模块开关，供本工程的独立 CMake/CubeIDE 构建使用。
 *
 * 若 CubeMX 重新生成此文件，请保留与本工程已用外设一致的模块和时钟常量。
 */
#ifndef __STM32F1xx_HAL_CONF_H
#define __STM32F1xx_HAL_CONF_H

#include "app_config.h"

#define HAL_MODULE_ENABLED
#define HAL_CORTEX_MODULE_ENABLED
#define HAL_DMA_MODULE_ENABLED
#define HAL_FLASH_MODULE_ENABLED
#define HAL_GPIO_MODULE_ENABLED
#define HAL_PWR_MODULE_ENABLED
#define HAL_RCC_MODULE_ENABLED
#define HAL_TIM_MODULE_ENABLED
#define HAL_UART_MODULE_ENABLED

/* 这里必须与 app_config.h / RCC 实际晶振保持一致；8 MHz 为待实测基线。 */
#ifndef HSE_VALUE
#define HSE_VALUE                 CFG_HSE_VALUE_HZ
#elif (HSE_VALUE != CFG_HSE_VALUE_HZ)
#error "HSE_VALUE 必须与 app_config.h 中的 CFG_HSE_VALUE_HZ 保持一致。"
#endif
#define HSE_STARTUP_TIMEOUT       100U
#define HSI_VALUE                 8000000U
#define LSI_VALUE                 40000U
#define LSE_VALUE                 32768U
#define LSE_STARTUP_TIMEOUT       5000U
#define EXTERNAL_CLOCK_VALUE      12288000U
#define VDD_VALUE                 3300U
#define TICK_INT_PRIORITY         0U
#define USE_RTOS                  0U
#define PREFETCH_ENABLE           1U
#define INSTRUCTION_CACHE_ENABLE  0U
#define DATA_CACHE_ENABLE         0U

/* 回调注册表会增加 RAM/代码；本工程只使用 HAL 的弱回调函数。 */
#define USE_HAL_ADC_REGISTER_CALLBACKS       0U
#define USE_HAL_CAN_REGISTER_CALLBACKS       0U
#define USE_HAL_CEC_REGISTER_CALLBACKS       0U
#define USE_HAL_DAC_REGISTER_CALLBACKS       0U
#define USE_HAL_I2C_REGISTER_CALLBACKS       0U
#define USE_HAL_I2S_REGISTER_CALLBACKS       0U
#define USE_HAL_IRDA_REGISTER_CALLBACKS      0U
#define USE_HAL_NAND_REGISTER_CALLBACKS      0U
#define USE_HAL_NOR_REGISTER_CALLBACKS       0U
#define USE_HAL_PCCARD_REGISTER_CALLBACKS    0U
#define USE_HAL_PCD_REGISTER_CALLBACKS       0U
#define USE_HAL_HCD_REGISTER_CALLBACKS       0U
#define USE_HAL_RTC_REGISTER_CALLBACKS       0U
#define USE_HAL_SD_REGISTER_CALLBACKS        0U
#define USE_HAL_SMARTCARD_REGISTER_CALLBACKS 0U
#define USE_HAL_SPI_REGISTER_CALLBACKS       0U
#define USE_HAL_TIM_REGISTER_CALLBACKS       0U
#define USE_HAL_UART_REGISTER_CALLBACKS      0U
#define USE_HAL_USART_REGISTER_CALLBACKS     0U
#define USE_HAL_WWDG_REGISTER_CALLBACKS      0U

#ifdef HAL_RCC_MODULE_ENABLED
#include "stm32f1xx_hal_rcc.h"
#endif
#ifdef HAL_GPIO_MODULE_ENABLED
#include "stm32f1xx_hal_gpio.h"
#include "stm32f1xx_hal_gpio_ex.h"
#endif
#ifdef HAL_DMA_MODULE_ENABLED
#include "stm32f1xx_hal_dma.h"
#endif
#ifdef HAL_CORTEX_MODULE_ENABLED
#include "stm32f1xx_hal_cortex.h"
#endif
#ifdef HAL_FLASH_MODULE_ENABLED
#include "stm32f1xx_hal_flash.h"
#endif
#ifdef HAL_PWR_MODULE_ENABLED
#include "stm32f1xx_hal_pwr.h"
#endif
#ifdef HAL_TIM_MODULE_ENABLED
#include "stm32f1xx_hal_tim.h"
#endif
#ifdef HAL_UART_MODULE_ENABLED
#include "stm32f1xx_hal_uart.h"
#endif

/* Standard STM32CubeF1 assertion contract; no generated forced-include file. */
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line);
#define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
#else
#define assert_param(expr) ((void)0U)
#endif

#endif /* __STM32F1xx_HAL_CONF_H */
