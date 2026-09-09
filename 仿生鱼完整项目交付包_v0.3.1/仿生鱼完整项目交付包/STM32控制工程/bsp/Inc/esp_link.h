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
bool EspLink_IsServerReady(void);
bool EspLink_IsClientConnected(void);
/* Release an established but expired command session. Quarantine its bytes
 * immediately; finish any known send phase before issuing CIPCLOSE for its ID. */
void EspLink_CloseClient(void);
bool EspLink_ResetNeeded(void);
const char *EspLink_LastError(void);
#endif
