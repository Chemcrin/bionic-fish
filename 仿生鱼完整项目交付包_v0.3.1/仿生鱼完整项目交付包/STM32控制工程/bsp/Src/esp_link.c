#include "esp_link.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"

#define FRAME_BYTES       200U
#define EVENT_SLOTS       4U
#define REPLY_RESERVE     72U /* app's largest immediate ACK/ERR encoder buffer */
#define PHONE_BYTES       256U
#define LINE_BYTES        160U
/* Transport chunks can contain many ASCII frames. Stream them, never allocate
 * their length. 65535 is a framing guard, not a limit on the TCP byte stream. */
#define IPD_BYTES_MAX     65535UL
#define IPD_IDLE_MS       500UL
#define AT_TIMEOUT_MS     2000UL
#define SEND_TIMEOUT_MS   700UL
#define RESET_TIMEOUT_MS  5000UL
#define BOOT_DELAY_MS     1200UL

typedef struct { char data[FRAME_BYTES]; uint16_t len; } Frame;
typedef enum { RX_LINE, RX_ID, RX_LENGTH, RX_DATA } RxState;
typedef enum { AT_NONE, AT_SETUP, AT_RESET, AT_CLOSE, AT_PROMPT, AT_BODY, AT_RESULT, AT_FAULT } Transaction;
typedef enum {
    SET_PROBE, SET_REBOOT, SET_ECHO, SET_VERSION, SET_UART, SET_STORE,
    SET_MESSAGES, SET_MODE, SET_AP, SET_IP, SET_DHCP, SET_DHCP_POOL, SET_CHECK_IP,
    SET_NORMAL, SET_MUX, SET_IPD_INFO, SET_RX_MODE, SET_MAX_CLIENTS,
    SET_SERVER, SET_IDLE_TIMEOUT, SET_DONE
} SetupStep;

static struct {
    Frame events[EVENT_SLOTS], status, sending;
    uint8_t event_head, event_tail, event_count;
    bool status_pending, abandon_send;
    uint8_t phone[PHONE_BYTES];
    uint16_t phone_head, phone_tail;
    uint32_t events_pending;
    RxState rx;
    char line[LINE_BYTES];
    uint16_t line_len;
    bool line_discard, digits;
    uint32_t number, remaining, rx_last_ms;
    uint8_t ipd_id, digit_count;
    Transaction transaction;
    SetupStep setup;
    uint32_t deadline, next_command_ms;
    uint8_t setup_failures, reject_mask, close_id, ip_verified;
    bool legacy, version_seen, uart_verified, server_ready, restart_pending;
    int8_t client;
    const char *error;
} L;

static bool Due(uint32_t now, uint32_t when) { return (int32_t)(now - when) >= 0; }

static void ResetRx(void)
{
    L.rx = RX_LINE;
    L.line_len = 0U;
    L.line_discard = false;
    L.number = L.remaining = 0U;
    L.digits = false;
    L.digit_count = 0U;
    L.phone_head = L.phone_tail = 0U;
}

static void ClearFrames(void)
{
    L.event_head = L.event_tail = L.event_count = 0U;
    L.status_pending = false;
}

static void Invalidate(uint32_t events, const char *reason)
{
    L.events_pending |= events;
    if (L.client >= 0) L.events_pending |= ESP_LINK_EVENT_DISCONNECTED;
    L.client = -1;
    L.error = reason;
    ClearFrames();
    L.phone_head = L.phone_tail = 0U;
}

static void NeedReset(const char *reason);

/* Normal CIPSEND has no documented time-bounded escape. Never send AT+RST/+++
 * into a possibly incomplete payload. A late '>' permits harmless padding to
 * finish its exact byte count; a terminal result then permits AT+RST. Without
 * that proof, latch ResetNeeded until a real module 'ready' is observed. */
static void Recover(uint32_t events, const char *reason)
{
    Invalidate(events, reason);
    ResetRx();
    if (L.setup == SET_PROBE && (L.transaction == AT_NONE || L.transaction == AT_SETUP)) {
        /* MCU-only reset can leave ESP waiting for an old, unknown byte count.
         * Until AT answered, even a reset command could become payload. */
        NeedReset("AT state unknown; check UART and reset ESP power/EN");
        return;
    }
    L.server_ready = false;
    L.restart_pending = true;
    L.reject_mask = 0U;
    if (L.transaction == AT_PROMPT || L.transaction == AT_BODY || L.transaction == AT_RESULT) {
        L.abandon_send = true;
        L.deadline = BSP_Millis() + RESET_TIMEOUT_MS;
    } else if (L.transaction != AT_FAULT) {
        L.transaction = AT_NONE;
        L.next_command_ms = BSP_Millis() + 100U;
    }
}

static void NeedReset(const char *reason)
{
    Invalidate(ESP_LINK_EVENT_TX_FAILED, reason);
    ResetRx();
    L.server_ready = false;
    L.restart_pending = false;
    L.transaction = AT_FAULT;
}

static void ModuleReady(void)
{
    if (L.transaction == AT_FAULT) L.setup_failures = 0U;
    Invalidate(0U, "ESP restarted; configuring");
    ResetRx();
    BSP_Uart_EspAbortTx();
    L.server_ready = false;
    L.restart_pending = false;
    L.abandon_send = false;
    L.reject_mask = 0U;
    L.transaction = AT_NONE;
    L.setup = SET_ECHO;
    L.version_seen = L.uart_verified = false;
    L.ip_verified = 0U;
    L.next_command_ms = BSP_Millis() + 50U;
}

static bool QueueRaw(const char *data, size_t length)
{
    return BSP_Uart_EspTxFree() >= length && BSP_Uart_SendEsp(data, length);
}

static bool BeginCommand(const char *command, Transaction transaction, uint32_t timeout)
{
    if (!BSP_Uart_EspTxIdle() || !QueueRaw(command, strlen(command))) return false;
    L.transaction = transaction;
    L.deadline = BSP_Millis() + timeout;
    return true;
}

static bool ParseNumber(const char **text, uint32_t *value)
{
    const char *p = *text;
    uint32_t result = 0U;
    if (*p < '0' || *p > '9') return false;
    do {
        uint32_t digit = (uint32_t)(*p - '0');
        if (result > (UINT32_MAX - digit) / 10U) return false;
        result = result * 10U + digit;
        p++;
    } while (*p >= '0' && *p <= '9');
    *text = p;
    *value = result;
    return true;
}

static bool UartMatches(const char *text)
{
    uint32_t values[5];
    unsigned i;
    for (i = 0U; i < 5U; ++i) {
        if (!ParseNumber(&text, &values[i])) return false;
        if (i < 4U && *text++ != ',') return false;
    }
    /* Query reports actual divided baud, not necessarily the requested value. */
    return *text == '\0' && values[0] >= CFG_UART_BAUD * 98UL / 100UL &&
           values[0] <= CFG_UART_BAUD * 102UL / 100UL &&
           values[1] == 8U && values[2] == 1U && values[3] == 0U && values[4] == 0U;
}

static void SetupFailed(const char *reason)
{
    L.setup_failures++;
    if (L.setup_failures >= 3U) NeedReset(reason);
    else Recover(ESP_LINK_EVENT_TX_FAILED, reason);
}

static void OnLine(const char *line)
{
    size_t length = strlen(line);
    if (strcmp(line, "ready") == 0) { ModuleReady(); return; }
    if (L.transaction == AT_FAULT) return;
    if (length >= 3U && line[0] >= '0' && line[0] <= '4' && line[1] == ',') {
        uint8_t id = (uint8_t)(line[0] - '0');
        if (strcmp(line + 2, "CONNECT") == 0) {
            if (!L.restart_pending && !L.abandon_send && (L.server_ready ||
                L.setup == SET_SERVER || L.setup == SET_IDLE_TIMEOUT) && L.client < 0) {
                L.client = (int8_t)id;
                L.phone_head = L.phone_tail = 0U;
                ClearFrames();
            } else if (L.client != (int8_t)id) L.reject_mask |= (uint8_t)(1U << id);
            return;
        }
        if (strcmp(line + 2, "CLOSED") == 0) {
            L.reject_mask &= (uint8_t)~(1U << id);
            if (L.client == (int8_t)id) {
                if (L.transaction == AT_PROMPT || L.transaction == AT_BODY || L.transaction == AT_RESULT)
                    Recover(ESP_LINK_EVENT_DISCONNECTED, "TCP closed during send");
                else Invalidate(ESP_LINK_EVENT_DISCONNECTED, "TCP client disconnected");
            }
            return;
        }
    }
    if (L.transaction == AT_SETUP) {
        if (L.setup == SET_VERSION && strncmp(line, "AT version:", 11U) == 0) {
            const char *version = line + 11;
            L.legacy = strncmp(version, "1.7.4", 5U) == 0 || strncmp(version, "1.7.5", 5U) == 0;
            L.version_seen = L.legacy || strncmp(version, "2.", 2U) == 0;
        } else if (L.setup == SET_UART && strncmp(line, "+UART_CUR:", 10U) == 0) {
            L.uart_verified = UartMatches(line + 10);
        } else if (L.setup == SET_CHECK_IP) {
            const char *p = line;
            if (strncmp(p, "+CIPAP_CUR:", 11U) == 0) p += 11;
            else if (strncmp(p, "+CIPAP:", 7U) == 0) p += 7;
            else p = "";
            if (strcmp(p, "ip:\"" CFG_ESP_AP_IP "\"") == 0) L.ip_verified |= 1U;
            if (strcmp(p, "gateway:\"" CFG_ESP_AP_IP "\"") == 0) L.ip_verified |= 2U;
            if (strcmp(p, "netmask:\"" CFG_ESP_AP_NETMASK "\"") == 0) L.ip_verified |= 4U;
        }
        if (strcmp(line, "OK") == 0) {
            if (L.setup == SET_VERSION && !L.version_seen) { NeedReset("Unsupported AT version (need NONOS 1.7.4/5 or ESP-AT 2.x)"); return; }
            if (L.setup == SET_UART && !L.uart_verified) { NeedReset("AT UART must be configured baud/8N1/no flow control"); return; }
            if (L.setup == SET_CHECK_IP && L.ip_verified != 7U) { SetupFailed("AP IP/gateway/netmask verification failed"); return; }
            L.setup = (SetupStep)(L.setup + 1);
            L.transaction = AT_NONE;
            if (L.setup == SET_DONE) {
                L.server_ready = true;
                L.setup_failures = 0U;
                L.error = "OK";
            }
            return;
        }
    }
    if (strcmp(line, "SEND OK") == 0 && L.transaction == AT_RESULT) {
        L.transaction = AT_NONE;
        if (!L.abandon_send) L.error = "OK";
        L.abandon_send = false;
        return;
    }
    if (strcmp(line, "OK") == 0 && L.transaction == AT_CLOSE) {
        L.reject_mask &= (uint8_t)~(1U << L.close_id);
        L.transaction = AT_NONE;
        return;
    }
    if (strcmp(line, "ERROR") == 0 || strcmp(line, "FAIL") == 0 ||
        strcmp(line, "SEND FAIL") == 0 || strcmp(line, "link is not valid") == 0 ||
        strncmp(line, "busy ", 5U) == 0) {
        if (L.transaction == AT_SETUP) SetupFailed("AT setup rejected (check firmware profile)");
        else if (L.transaction == AT_CLOSE) {
            L.reject_mask &= (uint8_t)~(1U << L.close_id);
            L.transaction = AT_NONE;
        } else if (L.transaction == AT_PROMPT || L.transaction == AT_BODY || L.transaction == AT_RESULT) {
            /* busy alone does not prove that a previous data phase ended. */
            if (strncmp(line, "busy ", 5U) != 0) L.transaction = AT_NONE;
            Recover(ESP_LINK_EVENT_TX_FAILED, "AT send failed");
        }
    }
}

static void BadIpd(const char *reason)
{
    (void)BSP_Uart_EspTakeRxLostAndDiscard();
    Recover(ESP_LINK_EVENT_RX_LOST, reason);
}

static void RxByte(uint8_t byte)
{
    uint32_t now = BSP_Millis();
    if (L.rx == RX_DATA) {
        if (L.client == (int8_t)L.ipd_id && !L.restart_pending && L.events_pending == 0U) {
            uint16_t next = (uint16_t)((L.phone_head + 1U) % PHONE_BYTES);
            if (next == L.phone_tail) { BadIpd("Decoded RX overflow"); return; }
            L.phone[L.phone_head] = byte;
            L.phone_head = next;
        }
        L.remaining--;
        L.rx_last_ms = now;
        if (L.remaining == 0U) L.rx = RX_LINE;
        return;
    }
    if (L.rx == RX_ID || L.rx == RX_LENGTH) {
        L.rx_last_ms = now;
        if (byte >= '0' && byte <= '9') {
            uint32_t digit = (uint32_t)(byte - '0');
            if (++L.digit_count > (L.rx == RX_ID ? 1U : 10U)) { BadIpd("IPD header too long"); return; }
            if (L.number > (UINT32_MAX - digit) / 10U) { BadIpd("IPD integer overflow"); return; }
            L.number = L.number * 10U + digit;
            L.digits = true;
            if ((L.rx == RX_ID && L.number > 4U) || L.number > IPD_BYTES_MAX) BadIpd("IPD length/id out of range");
        } else if (L.rx == RX_ID && byte == ',' && L.digits) {
            L.ipd_id = (uint8_t)L.number;
            L.number = 0U;
            L.digits = false;
            L.digit_count = 0U;
            L.rx = RX_LENGTH;
        } else if (L.rx == RX_LENGTH && byte == ':' && L.digits) {
            L.remaining = L.number;
            L.rx = L.remaining == 0U ? RX_LINE : RX_DATA;
        } else BadIpd("Malformed IPD header");
        return;
    }
    if (byte == '>' && L.line_len == 0U && L.transaction == AT_PROMPT) {
        L.transaction = AT_BODY;
        L.deadline = now + SEND_TIMEOUT_MS;
        return;
    }
    if (byte == '\r' || byte == '\n') {
        if (L.line_len > 0U && !L.line_discard) {
            L.line[L.line_len] = '\0';
            L.line_len = 0U;
            OnLine(L.line);
        }
        L.line_len = 0U;
        L.line_discard = false;
    } else if (byte >= 0x20U && byte <= 0x7eU) {
        if (byte == ' ' && L.line_len == 0U) return; /* optional prompt space */
        if (!L.line_discard && L.line_len < LINE_BYTES - 1U) {
            L.line[L.line_len++] = (char)byte;
            if (L.line_len == 5U && memcmp(L.line, "+IPD,", 5U) == 0) {
                L.line_len = 0U;
                L.rx = RX_ID;
                L.number = 0U;
                L.digits = false;
                L.digit_count = 0U;
                L.rx_last_ms = now;
            }
        } else L.line_discard = true;
    } else {
        L.line_len = 0U;
        L.line_discard = true; /* ignore non-ASCII ROM boot noise */
    }
}

static bool CheckUartLoss(void)
{
    if (!BSP_Uart_EspRxLost()) return false;
    (void)BSP_Uart_EspTakeRxLostAndDiscard();
    Recover(ESP_LINK_EVENT_RX_LOST, "USART2 lost RX bytes");
    return true;
}

static void PumpRx(void)
{
    uint16_t budget = 256U;
    uint8_t byte;
    if (CheckUartLoss()) return;
    while (budget-- > 0U) {
        /* Stop reading instead of overflowing a second ring on valid large IPD.
         * Poll pumps again as the application consumes the decoded bytes. */
        if (L.rx == RX_DATA && L.client == (int8_t)L.ipd_id &&
            (uint16_t)((L.phone_head + 1U) % PHONE_BYTES) == L.phone_tail) break;
        if (!BSP_Uart_ReadEsp(&byte)) break;
        if (CheckUartLoss()) return;
        RxByte(byte);
    }
}

static const char *SetupCommand(char *buffer, size_t capacity)
{
    switch (L.setup) {
    case SET_PROBE: return "AT\r\n";
    case SET_REBOOT: return "AT+RST\r\n";
    case SET_ECHO: return "ATE0\r\n";
    case SET_VERSION: return "AT+GMR\r\n";
    case SET_UART: return "AT+UART_CUR?\r\n";
    case SET_STORE: return L.legacy ? NULL : "AT+SYSSTORE=0\r\n";
    case SET_MESSAGES: return L.legacy ? NULL : "AT+SYSMSG=0\r\n";
    case SET_MODE: return L.legacy ? "AT+CWMODE_CUR=2\r\n" : "AT+CWMODE=2\r\n";
    case SET_AP:
        (void)snprintf(buffer, capacity, "AT+CWSAP%s=\"%s\",\"%s\",1,3,1,0\r\n", L.legacy ? "_CUR" : "", CFG_ESP_AP_SSID, CFG_ESP_AP_PASSWORD);
        return buffer;
    case SET_IP:
        (void)snprintf(buffer, capacity, "AT+CIPAP%s=\"%s\",\"%s\",\"%s\"\r\n", L.legacy ? "_CUR" : "", CFG_ESP_AP_IP, CFG_ESP_AP_IP, CFG_ESP_AP_NETMASK);
        return buffer;
    case SET_DHCP: return L.legacy ? "AT+CWDHCP_CUR=0,1\r\n" : "AT+CWDHCP=1,2\r\n";
    case SET_DHCP_POOL: return L.legacy ? "AT+CWDHCPS_CUR=0\r\n" : "AT+CWDHCPS=0\r\n";
    case SET_CHECK_IP: return L.legacy ? "AT+CIPAP_CUR?\r\n" : "AT+CIPAP?\r\n";
    case SET_NORMAL: return "AT+CIPMODE=0\r\n";
    case SET_MUX: return "AT+CIPMUX=1\r\n";
    case SET_IPD_INFO: return "AT+CIPDINFO=0\r\n";
    case SET_RX_MODE: return "AT+CIPRECVMODE=0\r\n";
    case SET_MAX_CLIENTS: return "AT+CIPSERVERMAXCONN=1\r\n";
    case SET_SERVER:
        (void)snprintf(buffer, capacity, "AT+CIPSERVER=1,%u\r\n", (unsigned)CFG_ESP_TCP_PORT);
        return buffer;
    case SET_IDLE_TIMEOUT: return "AT+CIPSTO=3\r\n";
    default: return NULL;
    }
}

void EspLink_Init(void)
{
    memset(&L, 0, sizeof(L));
    L.client = -1;
    L.error = "Waiting for ESP AT";
    L.next_command_ms = BSP_Millis() + BOOT_DELAY_MS;
}

void EspLink_Service(void)
{
    char command[160];
    uint32_t now;
    bool sending_event;
    PumpRx();
    now = BSP_Millis();
    /* Idle timeout, not a limit on the total duration of a multi-frame IPD. */
    if (L.rx != RX_LINE && (uint32_t)(now - L.rx_last_ms) >= IPD_IDLE_MS)
        BadIpd("IPD inter-byte timeout");
    if (L.transaction != AT_NONE && L.transaction != AT_FAULT && Due(now, L.deadline)) {
        if (L.transaction == AT_PROMPT || L.transaction == AT_BODY || L.transaction == AT_RESULT) {
            if (L.abandon_send) NeedReset("CIPSEND state unknown; reset ESP power/EN");
            else Recover(ESP_LINK_EVENT_TX_FAILED, "CIPSEND timeout; awaiting terminal response");
        } else if (L.transaction == AT_RESET) NeedReset("ESP ready timeout; reset ESP power/EN");
        else if (L.transaction == AT_SETUP && L.setup == SET_PROBE)
            NeedReset("AT probe timeout; check UART and reset ESP power/EN");
        else SetupFailed("AT command timeout");
    }
    if (L.transaction == AT_FAULT) return;
    if (L.transaction == AT_BODY) {
        if (L.abandon_send) {
            memset(L.sending.data, ' ', L.sending.len);
            L.sending.data[L.sending.len - 1U] = '\n';
        }
        if (QueueRaw(L.sending.data, L.sending.len)) {
            L.transaction = AT_RESULT;
            L.deadline = now + (L.abandon_send ? RESET_TIMEOUT_MS : SEND_TIMEOUT_MS);
        }
        return;
    }
    if (L.transaction != AT_NONE || !Due(now, L.next_command_ms)) return;
    if (L.restart_pending) {
        if (BeginCommand("AT+RST\r\n", AT_RESET, RESET_TIMEOUT_MS)) L.restart_pending = false;
        return;
    }
    if (L.setup != SET_DONE) {
        const char *text = SetupCommand(command, sizeof(command));
        while (text == NULL && L.setup != SET_DONE) {
            L.setup = (SetupStep)(L.setup + 1);
            text = SetupCommand(command, sizeof(command));
        }
        if (text != NULL) (void)BeginCommand(text, L.setup == SET_REBOOT ? AT_RESET : AT_SETUP,
                                          L.setup == SET_REBOOT ? RESET_TIMEOUT_MS : AT_TIMEOUT_MS);
        return;
    }
    if (L.reject_mask != 0U) {
        for (L.close_id = 0U; L.close_id < 5U; ++L.close_id)
            if ((L.reject_mask & (1U << L.close_id)) != 0U) break;
        (void)snprintf(command, sizeof(command), "AT+CIPCLOSE=%u\r\n", (unsigned)L.close_id);
        (void)BeginCommand(command, AT_CLOSE, AT_TIMEOUT_MS);
        return;
    }
    if (!EspLink_IsClientConnected() || L.events_pending != 0U ||
        (L.event_count == 0U && !L.status_pending)) return;
    sending_event = L.event_count > 0U;
    /* Send already-packed slots; EspLink_Send performs the only coalescing. */
    L.sending = sending_event ? L.events[L.event_tail] : L.status;
    (void)snprintf(command, sizeof(command), "AT+CIPSEND=%u,%u\r\n", (unsigned)L.client, (unsigned)L.sending.len);
    if (BeginCommand(command, AT_PROMPT, SEND_TIMEOUT_MS)) {
        L.abandon_send = false;
        if (sending_event) {
            L.event_tail = (uint8_t)((L.event_tail + 1U) % EVENT_SLOTS);
            L.event_count--;
        } else L.status_pending = false;
    }
}

uint32_t EspLink_TakeEvents(void)
{
    uint32_t events = L.events_pending;
    L.events_pending = 0U;
    return events;
}

bool EspLink_Poll(uint8_t *byte)
{
    if (byte == NULL || CheckUartLoss() || L.events_pending != 0U || !EspLink_IsClientConnected()) return false;
    if (L.event_count == EVENT_SLOTS &&
        L.events[(L.event_head + EVENT_SLOTS - 1U) % EVENT_SLOTS].len + REPLY_RESERVE >= FRAME_BYTES)
        return false; /* bounded overload; reserve one maximum-size immediate reply */
    PumpRx();
    if (L.events_pending != 0U || L.phone_tail == L.phone_head) return false;
    *byte = L.phone[L.phone_tail];
    L.phone_tail = (uint16_t)((L.phone_tail + 1U) % PHONE_BYTES);
    return true;
}

static bool ValidFrame(const char *frame, size_t length)
{
    return frame != NULL && length > 0U && length < FRAME_BYTES && frame[length - 1U] == '\n';
}

bool EspLink_Send(const char *frame, size_t length)
{
    Frame *slot;
    if (!ValidFrame(frame, length) || !EspLink_IsClientConnected()) return false;
    /* Pack consecutive small responses into the existing fixed storage. This
     * also lets a full-sized incoming IPD finish before its queued AT prompt:
     * backpressure on every fourth CMD would otherwise deadlock behind IPD. */
    if (L.event_count > 0U) {
        slot = &L.events[(L.event_head + EVENT_SLOTS - 1U) % EVENT_SLOTS];
        if ((size_t)slot->len + length < FRAME_BYTES) {
            memcpy(slot->data + slot->len, frame, length);
            slot->len = (uint16_t)(slot->len + length);
            return true;
        }
    }
    if (L.event_count == EVENT_SLOTS) {
        Recover(ESP_LINK_EVENT_TX_FAILED, "ACK/ERR queue exhausted");
        return false;
    }
    slot = &L.events[L.event_head];
    memcpy(slot->data, frame, length);
    slot->len = (uint16_t)length;
    L.event_head = (uint8_t)((L.event_head + 1U) % EVENT_SLOTS);
    L.event_count++;
    return true;
}

bool EspLink_SendStatus(const char *frame, size_t length)
{
    if (!ValidFrame(frame, length) || !EspLink_IsClientConnected()) return false;
    memcpy(L.status.data, frame, length);
    L.status.len = (uint16_t)length;
    L.status_pending = true;
    return true;
}

bool EspLink_IsServerReady(void) { return L.server_ready; }
bool EspLink_IsClientConnected(void) { return L.server_ready && L.client >= 0 && !L.restart_pending && L.transaction != AT_FAULT; }
void EspLink_CloseClient(void)
{
    uint8_t id;
    if (L.client < 0) return;
    id = (uint8_t)L.client;
    /* Keep any known IPD/AT envelope until its end, discarding only application
     * bytes. Resetting mid-payload would reinterpret its text as AT responses. */
    Invalidate(ESP_LINK_EVENT_DISCONNECTED, "Command heartbeat expired; closing TCP");
    L.reject_mask |= (uint8_t)(1U << id);
    if (L.transaction == AT_PROMPT || L.transaction == AT_BODY || L.transaction == AT_RESULT) {
        L.abandon_send = true;
        L.deadline = BSP_Millis() + RESET_TIMEOUT_MS;
    }
}
bool EspLink_ResetNeeded(void) { return L.transaction == AT_FAULT; }
const char *EspLink_LastError(void) { return L.error; }
