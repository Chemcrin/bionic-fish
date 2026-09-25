#ifndef __USART_H
#define __USART_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;

void MX_USART2_UART_Init(void);
void MX_USART3_UART_Init(void);

#ifdef __cplusplus
}
#endif
#endif /* __USART_H */
