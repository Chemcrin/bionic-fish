/** @file bsp_uart.h @brief 两个 UART 的中断收字节与非阻塞发送队列。 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

void BSP_Uart_Init(void);
void BSP_Uart_Service(void);
void BSP_Uart_OnRxComplete(UART_HandleTypeDef *huart);
void BSP_Uart_OnTxComplete(UART_HandleTypeDef *huart);
void BSP_Uart_OnError(UART_HandleTypeDef *huart);

bool BSP_Uart_ReadEsp(uint8_t *byte);
bool BSP_Uart_EspRxLost(void);
/* Main-loop atomic exchange: discard raw RX and consume its current loss flag.
 * An ISR arriving after interrupts are restored sets a new, preserved flag. */
bool BSP_Uart_EspTakeRxLostAndDiscard(void);
bool BSP_Uart_EspTxIdle(void);
/* Only after ESP reset/ready proves its old payload transaction no longer exists. */
void BSP_Uart_EspAbortTx(void);

bool BSP_Uart_SendEsp(const char *data, size_t length);
bool BSP_Uart_SendDebug(const char *data, size_t length);
uint16_t BSP_Uart_EspTxFree(void);
uint16_t BSP_Uart_DebugTxFree(void);
uint32_t BSP_Uart_TxDropCount(void);

#endif /* BSP_UART_H */
