/* USER CODE BEGIN Header */
/**
 * @file main.c
 * @brief STM32F103C8T6 CubeMX/HAL 入口；业务逻辑不堆放在本文件。
 */
/* USER CODE END Header */
#include "main.h"

/* USER CODE BEGIN Includes */
#include "app_config.h"
#include "app.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "bsp_uart.h"
/* USER CODE END Includes */
#include "gpio.h"
#include "tim.h"
#include "usart.h"

static void SystemClock_Config(void);

/* USER CODE BEGIN PD */
/* app_config.h 中的数值在这里映射为 HAL 枚举，避免 PLL/APB 设置与配置表脱节。 */
#if (CFG_PLL_MULTIPLIER == 2UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL2
#elif (CFG_PLL_MULTIPLIER == 3UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL3
#elif (CFG_PLL_MULTIPLIER == 4UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL4
#elif (CFG_PLL_MULTIPLIER == 5UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL5
#elif (CFG_PLL_MULTIPLIER == 6UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL6
#elif (CFG_PLL_MULTIPLIER == 7UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL7
#elif (CFG_PLL_MULTIPLIER == 8UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL8
#elif (CFG_PLL_MULTIPLIER == 9UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL9
#elif (CFG_PLL_MULTIPLIER == 10UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL10
#elif (CFG_PLL_MULTIPLIER == 11UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL11
#elif (CFG_PLL_MULTIPLIER == 12UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL12
#elif (CFG_PLL_MULTIPLIER == 13UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL13
#elif (CFG_PLL_MULTIPLIER == 14UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL14
#elif (CFG_PLL_MULTIPLIER == 15UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL15
#elif (CFG_PLL_MULTIPLIER == 16UL)
#define APP_PLL_MULTIPLIER RCC_PLL_MUL16
#else
#error "CFG_PLL_MULTIPLIER 必须在 STM32F1 支持的 2..16 范围内。"
#endif

#if (CFG_APB1_DIVIDER == 1UL)
#define APP_APB1_DIVIDER RCC_HCLK_DIV1
#elif (CFG_APB1_DIVIDER == 2UL)
#define APP_APB1_DIVIDER RCC_HCLK_DIV2
#elif (CFG_APB1_DIVIDER == 4UL)
#define APP_APB1_DIVIDER RCC_HCLK_DIV4
#elif (CFG_APB1_DIVIDER == 8UL)
#define APP_APB1_DIVIDER RCC_HCLK_DIV8
#elif (CFG_APB1_DIVIDER == 16UL)
#define APP_APB1_DIVIDER RCC_HCLK_DIV16
#else
#error "CFG_APB1_DIVIDER 必须为 1/2/4/8/16。"
#endif

#if (CFG_APB2_DIVIDER == 1UL)
#define APP_APB2_DIVIDER RCC_HCLK_DIV1
#elif (CFG_APB2_DIVIDER == 2UL)
#define APP_APB2_DIVIDER RCC_HCLK_DIV2
#elif (CFG_APB2_DIVIDER == 4UL)
#define APP_APB2_DIVIDER RCC_HCLK_DIV4
#elif (CFG_APB2_DIVIDER == 8UL)
#define APP_APB2_DIVIDER RCC_HCLK_DIV8
#elif (CFG_APB2_DIVIDER == 16UL)
#define APP_APB2_DIVIDER RCC_HCLK_DIV16
#else
#error "CFG_APB2_DIVIDER 必须为 1/2/4/8/16。"
#endif

#if (CFG_SYSCLK_HZ <= 24000000UL)
#define APP_FLASH_LATENCY FLASH_LATENCY_0
#elif (CFG_SYSCLK_HZ <= 48000000UL)
#define APP_FLASH_LATENCY FLASH_LATENCY_1
#elif (CFG_SYSCLK_HZ <= 72000000UL)
#define APP_FLASH_LATENCY FLASH_LATENCY_2
#else
#error "CFG_SYSCLK_HZ 超过 STM32F103 的 72 MHz 上限。"
#endif
/* USER CODE END PD */

int main(void)
{
    HAL_Init();
    SystemClock_Config();

    MX_GPIO_Init();
    MX_TIM2_Init();
    MX_TIM3_Init();
    MX_TIM4_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();

    /* USER CODE BEGIN 2 */
    BSP_Time_Init();
    App_Init();
    /* USER CODE END 2 */

    while (1) {
        /* USER CODE BEGIN 3 */
        /* 解析、控制、UI 和超时检查都在这里运行；中断不做阻塞业务。 */
        App_Process();
    }
    /* USER CODE END 3 */
}

static void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
    RCC_OscInitStruct.HSIState = RCC_HSI_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLMUL = APP_PLL_MULTIPLIER;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                                  RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = APP_APB1_DIVIDER;
    RCC_ClkInitStruct.APB2CLKDivider = APP_APB2_DIVIDER;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, APP_FLASH_LATENCY) != HAL_OK) {
        Error_Handler();
    }
}

/* USER CODE BEGIN 4 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance == TIM4) {
        BSP_Time_OnTim4Overflow();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    BSP_Uart_OnRxComplete(huart);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    BSP_Uart_OnTxComplete(huart);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    BSP_Uart_OnError(huart);
}
/* USER CODE END 4 */

void Error_Handler(void)
{
    /* USER CODE BEGIN Error_Handler_Debug */
    /* 普通 HAL/初始化错误发生在 PWM 已成功启动后时，先尽力让两桥 coast；
     * HardFault、复位与掉电仍必须依靠经实测的硬件失效保护。 */
    BSP_Board_EmergencyCoast();
    __disable_irq();
    while (1) {
        /* 若需生产级硬故障诊断，请加独立看门狗和故障记录；不要在此驱动电机。 */
    }
    /* USER CODE END Error_Handler_Debug */
}
