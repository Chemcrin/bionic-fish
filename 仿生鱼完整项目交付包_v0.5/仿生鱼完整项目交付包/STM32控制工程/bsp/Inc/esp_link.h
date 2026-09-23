/** ESP-01S AT + single-controller TCP. All APIs run in the main loop.
 * GMR selects NONOS AT 1.7.4/1.7.5 (including 1 MB Nano AT) or ESP-AT 2.x.
 * UART must already be CFG_UART_BAUD, 8N1, no RTS/CTS on the board's pins.
 * Custom transparent ESP sketches are not supported.
 */
#ifndef ESP_LINK_H
#define ESP_LINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum {
    ESP_LINK_EVENT_RX_LOST = 1U << 0,
    ESP_LINK_EVENT_DISCONNECTED = 1U << 1,
    ESP_LINK_EVENT_TX_FAILED = 1U << 2
};
void EspLink_Init(void);
void EspLink_Service(void);
/* Consume before parsing commands; reset protocol/session and actuators on any
 * event. RX_LOST additionally means E_RX_OVERFLOW. Poll is gated until consumed. */
uint32_t EspLink_TakeEvents(void);
bool EspLink_Poll(uint8_t *byte);
/* FIFO for ACK/ERR, independent replaceable slot for status; complete LF frames.
 * No TCP client => false. Full event FIFO fails the session safely. */
bool EspLink_Send(const char *frame, size_t length);
bool EspLink_SendStatus(const char *frame, size_t length);
/* 发送任意字节块（HTTP 响应等非 LF 结尾内容）给当前客户端，走同一条 CIPSEND 事务。
 * 长度上限 1024 字节；超出或没有客户端时返回 false。 */
bool EspLink_SendRaw(const char *data, size_t length);
bool EspLink_IsServerReady(void);
bool EspLink_IsClientConnected(void);
/* 诊断用只读快照：当前 TCP 客户端槽位（-1 表示无），以及尚未被主循环消费的事件位。 */
int EspLink_ClientId(void);
uint32_t EspLink_PendingEvents(void);
/* STA 侧（CFG_ESP_WIFI_MODE 含 STA 位时）由 DHCP 拿到的地址文本，形如 "192.168.3.40"；
 * 未连接或仅 AP 模式时返回 "0.0.0.0"。同一网络内的浏览器/PC 用它访问 <ip>:9000。 */
const char *EspLink_StaIp(void);
/* 每接受一个新 TCP 客户端就自增。应用用它判断"这是不是一个新连接"——
 * 只比较 IsClientConnected() 或 client id 都不够：槽位会被复用，id 会重复，
 * 于是新客户端会继承上一个客户端的解析状态（已实测踩到）。 */
uint32_t EspLink_ClientGeneration(void);
/* 诊断用 CIPSEND 事务计数，分开统计 ACK/ERR（event）与周期 STA（status）两条下行：
 *   status_try 增长、status_ok 不增长 ⇒ ESP 拒收或该次发送失败（固件/ESP 侧）；
 *   status_try 与 status_ok 同步增长而手机仍收不到 ⇒ 丢失发生在 ESP 之后的链路上；
 *   status_try 根本不增长 ⇒ 固件从未尝试下发 STA（链路状态机问题）。 */
typedef struct {
    uint32_t event_try;
    uint32_t event_ok;
    uint32_t status_try;
    uint32_t status_ok;
    uint32_t send_fail;    /* ESP 明确回 ERROR / FAIL / SEND FAIL / link is not valid */
    uint32_t send_timeout; /* 时限内既无 '>' 提示也无终结响应 */
} EspLinkSendCounters;
void EspLink_GetSendCounters(EspLinkSendCounters *out);
/* Release an established but expired command session. Quarantine its bytes
 * immediately; finish any known send phase before issuing CIPCLOSE for its ID. */
void EspLink_CloseClient(void);
bool EspLink_ResetNeeded(void);
const char *EspLink_LastError(void);
#endif
