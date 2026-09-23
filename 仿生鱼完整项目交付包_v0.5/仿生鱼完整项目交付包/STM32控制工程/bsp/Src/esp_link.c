#include "esp_link.h"
#include <stdio.h>
#include <string.h>
#include "app_config.h"
#include "bsp_time.h"
#include "bsp_uart.h"
/* 只为了取 HTTP_RESPONSE_BYTES 做编译期尺寸门禁（HTTP_RAW_BYTES >= 它）。
 * esp_link 不依赖 http_ui 的任何函数，只是共享这一个尺寸常量。 */
#include "http_ui.h"

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
/* HTTP 响应缓冲：整个响应留在 RAM 里，按 RAW_CHUNK_BYTES 分多次 CIPSEND 发出。
 *
 * ⚠️ 必须 >= protocol/Inc/http_ui.h 的 HTTP_RESPONSE_BYTES。
 * 2026-09-23 修复：本宏曾是 3584，而 HTTP_RESPONSE_BYTES 是 4096，且网页在 v0.5
 * 重做后涨到 3652 字节页面（响应总长 3779）。EspLink_SendRaw 里的
 * `length > sizeof(L.raw)` 判定因此成立并**静默 return false** ——
 * 一个字节都发不出去，浏览器只能一直超时，而串口上 error 仍是 "OK"、毫无提示。
 * 这类"缓冲区尺寸对不上"必须由编译期拦住，见下方 kHttpRawMustFitResponse。 */
#define HTTP_RAW_BYTES    4096U
/* 编译期门禁：L.raw 装不下完整 HTTP 响应就构建失败。
 * 与上面 HTTP_RESPONSE_BYTES 联动，避免再次出现"改了页面/改了响应缓冲、
 * 却忘了同步传输缓冲"的静默失败。 */
typedef char kHttpRawMustFitResponse[(HTTP_RAW_BYTES >= HTTP_RESPONSE_BYTES) ? 1 : -1];
/* 单次 CIPSEND 的载荷上限。**必须明显小于 CFG_UART_TX_RING_BYTES(512)**：
 * QueueRaw 要求 `TxFree() >= length`，一次性投递超过环形缓冲长度的载荷会永远入队失败，
 * 卡在 AT_BODY 直到 700ms 超时，进而锁存 ResetNeeded（实机已踩过这个坑）。 */
#define RAW_CHUNK_BYTES   384U

typedef struct { char data[FRAME_BYTES]; uint16_t len; } Frame;
typedef enum { RX_LINE, RX_ID, RX_LENGTH, RX_DATA } RxState;
typedef enum { AT_NONE, AT_SETUP, AT_RESET, AT_CLOSE, AT_PROMPT, AT_BODY, AT_RESULT, AT_STA_IP, AT_FAULT } Transaction;
typedef enum {
    SET_PROBE, SET_REBOOT, SET_ECHO, SET_VERSION, SET_UART, SET_STORE,
    SET_MESSAGES, SET_MODE, SET_AP, SET_IP, SET_DHCP, SET_DHCP_POOL, SET_CHECK_IP,
    SET_CHECK_STA_IP,
    SET_NORMAL, SET_MUX, SET_IPD_INFO, SET_RX_MODE, SET_MAX_CLIENTS,
    SET_SERVER, SET_IDLE_TIMEOUT, SET_DONE
} SetupStep;

/* STA 侧 IP 文本上限："255.255.255.255" + NUL。 */
#define STA_IP_TEXT_BYTES 16U

static struct {
    Frame events[EVENT_SLOTS], status, sending;
    uint8_t event_head, event_tail, event_count;
    bool status_pending, abandon_send;
    /* 诊断：在途 CIPSEND 是否承载周期 status 帧，以及分门别类的发送计数。 */
    bool cipsend_status;
    uint32_t event_send_try, event_send_ok, status_send_try, status_send_ok;
    uint32_t send_fail, send_timeout;
    /* HTTP 响应专用发送槽：事件槽只有 200 字节，装不下一个网页响应。 */
    char raw[HTTP_RAW_BYTES];
    uint16_t raw_len, raw_sent, raw_chunk;
    bool raw_pending, sending_raw;
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
    /* STA 侧拿到的 DHCP 地址文本（未取到时为空串）。 */
    char sta_ip[STA_IP_TEXT_BYTES];
    uint32_t next_sta_ip_ms;
    bool legacy, version_seen, uart_verified, server_ready, restart_pending;
    int8_t client;
    uint32_t client_generation;
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
    L.raw_pending = false;
    L.raw_sent = 0U;
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
    L.sta_ip[0] = '\0';
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

/* 形如 +CIPSTA_CUR:ip:"192.168.3.40"（legacy）或 +CIPSTA:ip:"..."（2.x）。
 * 只取 ip 行；只在拿到非 0.0.0.0 的地址时才写入，以免把已取到的地址又抹成空。 */
static void ParseStaAddressLine(const char *line)
{
    const char *p = line;
    const char *value;
    size_t length = 0U;

    if (strncmp(p, "+CIPSTA_CUR:", 12U) == 0) {
        p += 12;
    } else if (strncmp(p, "+CIPSTA:", 8U) == 0) {
        p += 8;
    } else {
        return;
    }
    if (strncmp(p, "ip:\"", 4U) != 0) {
        return;
    }
    value = p + 4;
    if (strncmp(value, "0.0.0.0", 7U) == 0) {
        return; /* 模块尚未拿到地址；保持空串，由 Service 继续补查 */
    }
    while ((value[length] != '\0') && (value[length] != '"') &&
           (length + 1U < STA_IP_TEXT_BYTES)) {
        L.sta_ip[length] = value[length];
        length++;
    }
    L.sta_ip[length] = '\0';
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
                L.client_generation++;
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
    if (L.transaction == AT_STA_IP) {
        ParseStaAddressLine(line);
        if ((strcmp(line, "OK") == 0) || (strcmp(line, "ERROR") == 0) ||
            (strcmp(line, "FAIL") == 0)) {
            L.transaction = AT_NONE;
        }
        return;
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
        } else if (L.setup == SET_CHECK_STA_IP) {
            /* DHCP 地址不可预知，因此不做等值校验，只记录下来供 OLED/串口显示。 */
            ParseStaAddressLine(line);
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
        if (L.sending_raw) {
            /* 分片推进：整段发完才清 raw_pending，否则下一个分片会被丢掉。 */
            L.raw_sent = (uint16_t)(L.raw_sent + L.raw_chunk);
            if (L.raw_sent >= L.raw_len) {
                L.raw_pending = false;
                L.raw_sent = 0U;
            }
        } else if (L.cipsend_status) {
            L.status_send_ok++;
        } else {
            L.event_send_ok++;
        }
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
            if (strncmp(line, "busy ", 5U) != 0) {
                L.send_fail++;
                L.transaction = AT_NONE;
            }
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
    case SET_MODE:
        /* 模式由配置决定：2=仅 AP（原行为），3=AP+STA。用 _CUR 变体只改本次运行的
         * 模式，不写 flash，避免与模块里已保存的 CWJAP 凭据打架。 */
        if (L.legacy) {
            (void)snprintf(buffer, capacity, "AT+CWMODE_CUR=%u\r\n", (unsigned)CFG_ESP_WIFI_MODE);
        } else {
            (void)snprintf(buffer, capacity, "AT+CWMODE=%u\r\n", (unsigned)CFG_ESP_WIFI_MODE);
        }
        return buffer;
    case SET_AP:
        (void)snprintf(buffer, capacity, "AT+CWSAP%s=\"%s\",\"%s\",1,3,1,0\r\n", L.legacy ? "_CUR" : "", CFG_ESP_AP_SSID, CFG_ESP_AP_PASSWORD);
        return buffer;
    case SET_IP:
        (void)snprintf(buffer, capacity, "AT+CIPAP%s=\"%s\",\"%s\",\"%s\"\r\n", L.legacy ? "_CUR" : "", CFG_ESP_AP_IP, CFG_ESP_AP_IP, CFG_ESP_AP_NETMASK);
        return buffer;
    case SET_DHCP: return L.legacy ? "AT+CWDHCP_CUR=0,1\r\n" : "AT+CWDHCP=1,2\r\n";
    case SET_DHCP_POOL: return L.legacy ? "AT+CWDHCPS_CUR=0\r\n" : "AT+CWDHCPS=0\r\n";
    case SET_CHECK_IP: return L.legacy ? "AT+CIPAP_CUR?\r\n" : "AT+CIPAP?\r\n";
    case SET_CHECK_STA_IP:
        /* 仅 AP 模式没有 STA 地址可查；返回 NULL 让状态机自动跳过这一步。 */
        if ((CFG_ESP_WIFI_MODE & 2U) == 0U) {
            return NULL;
        }
        return L.legacy ? "AT+CIPSTA_CUR?\r\n" : "AT+CIPSTA?\r\n";
    case SET_NORMAL: return "AT+CIPMODE=0\r\n";
    case SET_MUX: return "AT+CIPMUX=1\r\n";
    case SET_IPD_INFO: return "AT+CIPDINFO=0\r\n";
    case SET_RX_MODE: return "AT+CIPRECVMODE=0\r\n";
    case SET_MAX_CLIENTS:
        (void)snprintf(buffer, capacity, "AT+CIPSERVERMAXCONN=%u\r\n", (unsigned)CFG_ESP_MAX_CLIENTS);
        return buffer;
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
            else {
                L.send_timeout++;
                Recover(ESP_LINK_EVENT_TX_FAILED, "CIPSEND timeout; awaiting terminal response");
            }
        } else if (L.transaction == AT_STA_IP) {
            L.transaction = AT_NONE; /* 补查失败无所谓，下个周期再试 */
        } else if (L.transaction == AT_RESET) NeedReset("ESP ready timeout; reset ESP power/EN");
        else if (L.transaction == AT_SETUP && L.setup == SET_PROBE)
            NeedReset("AT probe timeout; check UART and reset ESP power/EN");
        else SetupFailed("AT command timeout");
    }
    if (L.transaction == AT_FAULT) return;
    if (L.transaction == AT_BODY) {
        char *payload = L.sending_raw ? (L.raw + L.raw_sent) : L.sending.data;
        uint16_t payload_len = L.sending_raw ? L.raw_chunk : L.sending.len;
        if (L.abandon_send) {
            memset(payload, ' ', payload_len);
            payload[payload_len - 1U] = '\n';
        }
        if (QueueRaw(payload, payload_len)) {
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
    /* 配置阶段查 STA 地址时模块多半还没打 WIFI GOT IP，会读到 0.0.0.0。
     * 这里在空闲时（无在途事务、无客户端、无待关闭连接）补查，直到拿到真实地址。 */
    if ((L.sta_ip[0] == '\0') && ((CFG_ESP_WIFI_MODE & 2U) != 0U) && (L.client < 0) &&
        (L.reject_mask == 0U) && Due(now, L.next_sta_ip_ms)) {
        L.next_sta_ip_ms = now + 5000UL;
        (void)BeginCommand(L.legacy ? "AT+CIPSTA_CUR?\r\n" : "AT+CIPSTA?\r\n", AT_STA_IP, AT_TIMEOUT_MS);
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
        (L.event_count == 0U && !L.status_pending && !L.raw_pending)) return;
    /* 优先级：ACK/ERR（一个帧一个语义，不能被拖）→ HTTP 响应（浏览器在等）→ 周期 STA。 */
    sending_event = L.event_count > 0U;
    if (sending_event) {
        L.sending_raw = false;
        /* Send already-packed slots; EspLink_Send performs the only coalescing. */
        L.sending = L.events[L.event_tail];
    } else if (L.raw_pending) {
        uint16_t remaining = (uint16_t)(L.raw_len - L.raw_sent);
        L.sending_raw = true;
        L.raw_chunk = (remaining > RAW_CHUNK_BYTES) ? (uint16_t)RAW_CHUNK_BYTES : remaining;
    } else {
        L.sending_raw = false;
        L.sending = L.status;
    }
    (void)snprintf(command, sizeof(command), "AT+CIPSEND=%u,%u\r\n", (unsigned)L.client,
                   (unsigned)(L.sending_raw ? L.raw_chunk : L.sending.len));
    if (BeginCommand(command, AT_PROMPT, SEND_TIMEOUT_MS)) {
        L.abandon_send = false;
        L.cipsend_status = !sending_event && !L.sending_raw;
        if (sending_event) {
            L.event_send_try++;
            L.event_tail = (uint8_t)((L.event_tail + 1U) % EVENT_SLOTS);
            L.event_count--;
        } else if (L.sending_raw) {
            L.event_send_try++;   /* HTTP 响应与 ACK/ERR 同属"事件性"下行 */
            /* raw_pending 在整段发完后才清除（见 SEND OK 处理），这里只推进分片。 */
        } else {
            L.status_send_try++;
            L.status_pending = false;
        }
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

bool EspLink_SendRaw(const char *data, size_t length)
{
    /* HTTP 响应：内容可以不是 LF 结尾的 ASCII 帧，因此不能走 ValidFrame 那条路。
     * 复用同一条 CIPSEND 事务（先发头、等 '>'、再发本缓冲），一次发完。 */
    if ((data == NULL) || (length == 0U) || (length > sizeof(L.raw)) ||
        !EspLink_IsClientConnected()) {
        return false;
    }
    memcpy(L.raw, data, length);
    L.raw_len = (uint16_t)length;
    L.raw_sent = 0U;
    L.raw_pending = true;
    return true;
}

bool EspLink_IsServerReady(void) { return L.server_ready; }
bool EspLink_IsClientConnected(void) { return L.server_ready && L.client >= 0 && !L.restart_pending && L.transaction != AT_FAULT; }
int EspLink_ClientId(void) { return L.client; }
uint32_t EspLink_PendingEvents(void) { return L.events_pending; }
const char *EspLink_StaIp(void) { return (L.sta_ip[0] == '\0') ? "0.0.0.0" : L.sta_ip; }
uint32_t EspLink_ClientGeneration(void) { return L.client_generation; }
void EspLink_GetSendCounters(EspLinkSendCounters *out)
{
    if (out == NULL) return;
    out->event_try = L.event_send_try;
    out->event_ok = L.event_send_ok;
    out->status_try = L.status_send_try;
    out->status_ok = L.status_send_ok;
    out->send_fail = L.send_fail;
    out->send_timeout = L.send_timeout;
}
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
