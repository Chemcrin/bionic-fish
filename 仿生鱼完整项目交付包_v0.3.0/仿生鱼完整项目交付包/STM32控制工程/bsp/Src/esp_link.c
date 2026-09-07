/* esp_link.c - 见 esp_link.h
 *
 * 设计要点：
 * 1) 下行：ESP 在 AT+CIPSEND=0,<len> 后需在数十秒内收到 <len> 字节。把命令头与
 *    帧体在同一串行突发里顺序入队(FIFO 保序)，无需解析 '>'。两次发送之间加冷却，
 *    避免上一个 CIPSEND 会话未结束就发下一个。发送失败(无客户端)则本帧丢弃，
 *    上层 STA 每 200ms 重发、下一条覆盖，故对单帧丢失不敏感。
 * 2) 上行：逐字节识别 "+IPD,<id>,<len>:" 并把其后 <len> 字节转发到解码环；其余
 *    AT 应答/事件行忽略。上电阶段用普通行的 "OK" 确认 server 已就绪。
 * 3) 上电：周期性发送 CIPMUX=1 + CIPSERVER=1,9000(并先 ATE0 关回显)直到就绪。
 */
#include "esp_link.h"

#include <stdio.h>
#include <string.h>

#include "bsp_time.h"
#include "bsp_uart.h"

#define ESP_TCP_PORT          9000U
#define ESP_MAX_FRAME         200U
#define ESP_TX_SLOTS          4U
#define ESP_PHONE_RX_BYTES    256U
#define ESP_FRAME_COOLDOWN_MS 150U
#define ESP_SETUP_PERIOD_MS   2000U
#define ESP_SETUP_FIRST_DELAY 1200U   /* 等 ESP 先上电 */
#define ESP_PLAIN_LINE_MAX    64U

typedef struct {
    char     data[ESP_MAX_FRAME];
    uint16_t len;
} EspFrame;

typedef enum {
    RX_IDLE,
    RX_IPD_LEN,    /* 已匹配 "+IPD,"，读 <linkid>,<len> */
    RX_IPD_DATA    /* 复制 <len> 个 payload 字节 */
} RxState;

static struct {
    EspFrame slots[ESP_TX_SLOTS];
    uint8_t  head, tail, count;
    uint32_t next_tx_ms;

    uint8_t  phone[ESP_PHONE_RX_BYTES];
    uint16_t phone_head, phone_tail;
    bool     rx_overflow;

    RxState  rx;
    uint8_t  ipd_match;         /* "+IPD," 已匹配到第几个字符(0..4) */
    bool     ipd_seen_comma;    /* 已越过 <linkid>, */
    uint32_t ipd_len;
    uint32_t ipd_got;

    bool     server_up;
    bool     echo_off;
    uint32_t next_setup_ms;

    uint8_t  plain[ESP_PLAIN_LINE_MAX];
    uint16_t plain_len;
} L;

static const char kIpdPrefix[] = "+IPD,";   /* 长度 5 */

static void RawCstr(const char *s)
{
    (void)BSP_Uart_SendEsp(s, strlen(s));
}

static bool PhonePush(uint8_t b)
{
    uint16_t next = (uint16_t)((L.phone_head + 1U) & (ESP_PHONE_RX_BYTES - 1U));
    if (next == L.phone_tail) {
        L.rx_overflow = true;
        return false;
    }
    L.phone[L.phone_head] = b;
    L.phone_head = next;
    return true;
}

static void OnPlainLine(const char *line, uint16_t len)
{
    /* 仅在 server 尚未就绪、且刚发完配置时，用 "OK" 确认已生效。 */
    if (!L.server_up && len >= 2U && strncmp(line, "OK", 2U) == 0) {
        L.server_up = true;
    }
}

static void PlainByte(uint8_t b)
{
    if (b == '\n' || b == '\r') {
        if (L.plain_len > 0U) {
            OnPlainLine((const char *)L.plain, L.plain_len);
        }
        L.plain_len = 0U;
    } else if (L.plain_len < ESP_PLAIN_LINE_MAX - 1U) {
        L.plain[L.plain_len++] = b;
    }
}

static void HandleRxByte(uint8_t b)
{
    switch (L.rx) {
    case RX_IDLE:
        /* 普通字节既进普通行缓冲(仅供上电 OK 判断)，又跑 "+IPD," 前缀匹配。 */
        PlainByte(b);
        {
            /* 前缀匹配：+ -> I -> P -> D -> , */
            if (L.ipd_match == 0U) {
                if (b == (uint8_t)'+') {
                    L.ipd_match = 1U;
                }
            } else {
                if (L.ipd_match < 5U && b == (uint8_t)kIpdPrefix[L.ipd_match]) {
                    L.ipd_match++;
                } else {
                    L.ipd_match = (b == (uint8_t)'+') ? 1U : 0U;
                }
            }
            if (L.ipd_match == 5U) {
                L.rx = RX_IPD_LEN;
                L.ipd_match = 0U;
                L.ipd_seen_comma = false;
                L.ipd_len = 0U;
            }
        }
        break;

    case RX_IPD_LEN:
        /* 格式：+IPD,<linkid>,<len>:  已消费 "+IPD,"，先跳过 linkid 到 ','，再读 len。 */
        if (!L.ipd_seen_comma) {
            if (b == (uint8_t)',') {
                L.ipd_seen_comma = true;
            }
        } else if (b >= (uint8_t)'0' && b <= (uint8_t)'9') {
            L.ipd_len = L.ipd_len * 10U + (uint32_t)(b - (uint8_t)'0');
        } else if (b == (uint8_t)':') {
            L.ipd_got = 0U;
            L.rx = (L.ipd_len > 0U) ? RX_IPD_DATA : RX_IDLE;
        }
        break;

    case RX_IPD_DATA:
        if (L.ipd_got < L.ipd_len) {
            (void)PhonePush(b);
            L.ipd_got++;
        }
        if (L.ipd_got >= L.ipd_len) {
            L.rx = RX_IDLE;
            L.ipd_len = 0U;
        }
        break;
    }
}

void EspLink_Init(void)
{
    memset(&L, 0, sizeof(L));
    L.next_setup_ms = BSP_Millis() + ESP_SETUP_FIRST_DELAY;
    L.next_tx_ms = BSP_Millis();
}

bool EspLink_TxSpace(void)
{
    return L.count < ESP_TX_SLOTS;
}

bool EspLink_Send(const char *frame, size_t length)
{
    if (frame == 0 || length == 0U || length >= ESP_MAX_FRAME || !EspLink_TxSpace()) {
        return false;
    }
    {
        EspFrame *f = &L.slots[L.head];
        memcpy(f->data, frame, length);
        f->len = (uint16_t)length;
        L.head = (uint8_t)((L.head + 1U) % ESP_TX_SLOTS);
        L.count++;
    }
    return true;
}

bool EspLink_Poll(uint8_t *byte)
{
    if (L.phone_tail == L.phone_head) {
        return false;
    }
    *byte = L.phone[L.phone_tail];
    L.phone_tail = (uint16_t)((L.phone_tail + 1U) & (ESP_PHONE_RX_BYTES - 1U));
    return true;
}

bool EspLink_RxOverflow(void)
{
    return L.rx_overflow;
}

void EspLink_ClearRxOverflow(void)
{
    L.rx_overflow = false;
}

static void SendQueuedFrame(void)
{
    EspFrame f = L.slots[L.tail];
    char header[32];
    (void)snprintf(header, sizeof(header), "AT+CIPSEND=0,%u\r\n", (unsigned int)f.len);
    RawCstr(header);
    (void)BSP_Uart_SendEsp(f.data, f.len);
    (void)BSP_Uart_SendEsp("\r\n", 2U);

    L.tail = (uint8_t)((L.tail + 1U) % ESP_TX_SLOTS);
    L.count--;
    L.next_tx_ms = BSP_Millis() + ESP_FRAME_COOLDOWN_MS;
}

void EspLink_Service(void)
{
    uint32_t now = BSP_Millis();
    uint8_t byte;

    /* 1) 上电：先 ATE0 关回显(避免把我们 TX 的内容回显成噪音)，再开多连接+服务器。 */
    if (!L.server_up) {
        if ((int32_t)(now - L.next_setup_ms) >= 0) {
            if (!L.echo_off) {
                RawCstr("ATE0\r\n");
                L.echo_off = true;
            }
            RawCstr("AT+CIPMUX=1\r\n");
            {
                char cmd[32];
                (void)snprintf(cmd, sizeof(cmd), "AT+CIPSERVER=1,%u\r\n", (unsigned int)ESP_TCP_PORT);
                RawCstr(cmd);
            }
            L.next_setup_ms = now + ESP_SETUP_PERIOD_MS;
        }
    }

    /* 2) 读回 ESP 串口解码。 */
    while (BSP_Uart_ReadEsp(&byte)) {
        HandleRxByte(byte);
    }

    /* 3) 下行：有帧待发且已过冷却，则发出一帧。 */
    if (L.count > 0U && (int32_t)(now - L.next_tx_ms) >= 0) {
        SendQueuedFrame();
    }
}
