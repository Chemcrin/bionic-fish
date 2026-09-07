/* esp_link.h - ESP-01S 原厂 AT 固件 + TCP 服务器 模式的链路层
 *
 * 背景：STM32 通过 USART2 与 ESP-01S 通信。ESP 配置为「AP + TCP 服务器
 * (CIPMUX=1, CIPSERVER=1,9000)」。此模式下串口不是裸管道：下行要包
 * AT+CIPSEND=0,<len>，上行带 +IPD,0,<len>: 前缀和 AT 应答噪音。
 * 本模块把这一层透明化，让上层 app 继续按"纯 ASCII 帧"看待 ESP 通道。
 *
 * 依赖：bsp_uart 的裸收发 (BSP_Uart_ReadEsp/BSP_Uart_SendEsp) + bsp_time。
 */
#ifndef ESP_LINK_H
#define ESP_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* 初始化链路状态（在 App_Init 调用一次）。 */
void EspLink_Init(void);

/* 主循环周期调用：驱动 AT 上电配置、下行 CIPSEND 状态机、上行 +IPD 解码。 */
void EspLink_Service(void);

/* 把一条完整应用帧(ACK/ERR/STA，已含结尾 '\n')排队发给手机。满则返回 false。 */
bool EspLink_Send(const char *frame, size_t length);

/* 取一个已解码的手机上行字节；无则返回 false。供协议解析器轮询。 */
bool EspLink_Poll(uint8_t *byte);

/* 是否还能再入队至少一条应用帧（用于上层低优先级门控）。 */
bool EspLink_TxSpace(void);

/* 上报是否发生了上行字节丢失/溢出(需上层丢弃残帧并复位会话)。 */
bool EspLink_RxOverflow(void);

/* 清除 RxOverflow 标志。 */
void EspLink_ClearRxOverflow(void);

#endif /* ESP_LINK_H */
