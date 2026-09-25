#ifndef ESP_TEST_HAL_H
#define ESP_TEST_HAL_H
#include <stdint.h>
typedef struct { unsigned port; } UART_HandleTypeDef;
typedef enum { HAL_OK = 0, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void EspTest_ClearOre(UART_HandleTypeDef *uart);
#define __DMB() ((void)0)
#define __HAL_UART_CLEAR_OREFLAG(uart) EspTest_ClearOre(uart)
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
#endif
