#include "usart.h"

#include "app_config.h"

UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;

static void ConfigureUart(UART_HandleTypeDef *huart, USART_TypeDef *instance)
{
    huart->Instance = instance;
    huart->Init.BaudRate = CFG_UART_BAUD;
    huart->Init.WordLength = UART_WORDLENGTH_8B;
    huart->Init.StopBits = UART_STOPBITS_1;
    huart->Init.Parity = UART_PARITY_NONE;
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(huart) != HAL_OK) {
        Error_Handler();
    }
}

void MX_USART2_UART_Init(void)
{
    /* PA2 TX -> ESP RXD；PA3 RX <- ESP TXD，交叉连接。 */
    ConfigureUart(&huart2, USART2);
}

void MX_USART3_UART_Init(void)
{
    /* PB10 TX -> CH340 RX；PB11 RX <- CH340 TX。 */
    ConfigureUart(&huart3, USART3);
}
