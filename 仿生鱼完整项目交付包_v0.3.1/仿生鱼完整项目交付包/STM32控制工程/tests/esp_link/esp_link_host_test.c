/* Exercise actual esp_link + bsp_uart + ring_buffer + ASCII parser sources.
 * The fake HAL transfers a bounded number of bytes per simulated millisecond;
 * the ESP peer enforces CIPSEND length and responds only to complete commands. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_link.h"
#include "bsp_uart.h"
#include "ascii_protocol.h"
#include "usart.h"

UART_HandleTypeDef huart2 = {2U}, huart3 = {3U};
static uint32_t now_ms, irq_mask, observed_events;
static uint8_t *rx_destination;
static bool rx_armed, fail_rearm, error_on_unlock;
static unsigned rx_aborts, tx_aborts;
static struct { const uint8_t *data; unsigned left, offset; } tx[4];
static char incoming[65536], command[256], commands[256][160];
static unsigned incoming_head, incoming_tail, command_length, command_count;
static char payloads[1024][200];
static unsigned payload_lengths[1024], payload_ids[1024], payload_count;
static unsigned body_left, body_length, body_id, body_offset;
static bool modern, auto_respond, drop_prompt, drop_result, reject_send;
static unsigned uart_flow;
static bool parse_app, consume_events;
static bool model_idle_timeout, connect_during_setup;
static uint32_t model_idle_ms, model_last_client_data;
static int model_client;
static unsigned parsed_commands, malformed_commands;
static ProtocolParser parser;

uint32_t BSP_Millis(void) { return now_ms; }
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask)
{
    irq_mask = mask;
    if (mask == 0U && error_on_unlock) {
        error_on_unlock = false;
        BSP_Uart_OnError(&huart2);
    }
}
void EspTest_ClearOre(UART_HandleTypeDef *uart) { assert(uart == &huart2); }
void Error_Handler(void) { abort(); }

HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    assert(uart == &huart2 && length == 1U);
    if (fail_rearm) { fail_rearm = false; return HAL_ERROR; }
    if (rx_armed) return HAL_BUSY;
    rx_armed = true;
    rx_destination = data;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *uart, uint8_t *data, uint16_t length)
{
    if (tx[uart->port].left != 0U) return HAL_BUSY;
    tx[uart->port].data = data;
    tx[uart->port].left = length;
    tx[uart->port].offset = 0U;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *uart)
{
    assert(uart == &huart2);
    rx_armed = false;
    rx_aborts++;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *uart)
{
    tx[uart->port].left = 0U;
    tx_aborts++;
    return HAL_OK;
}

static void Input(const char *bytes)
{
    size_t length = strlen(bytes);
    assert(incoming_head + length < sizeof(incoming));
    memcpy(incoming + incoming_head, bytes, length);
    incoming_head += (unsigned)length;
}
static void Deliver(uint8_t byte)
{
    assert(rx_armed && irq_mask == 0U);
    *rx_destination = byte;
    rx_armed = false; /* HAL completes a one-byte read before the callback. */
    BSP_Uart_OnRxComplete(&huart2);
}
static void Ipd(unsigned id, const char *payload)
{
    char header[32];
    (void)snprintf(header, sizeof(header), "+IPD,%u,%u:", id, (unsigned)strlen(payload));
    Input(header);
    Input(payload);
    if (model_client == (int)id) model_last_client_data = now_ms;
}
static void OnCommand(void)
{
    command[command_length] = 0;
    assert(command_count < 256U);
    (void)snprintf(commands[command_count++], sizeof(commands[0]), "%s", command);
    if (!auto_respond) return;
    if (strcmp(command, "AT+RST") == 0) { model_client = -1; Input("OK\r\nready\r\n"); }
    else if (strcmp(command, "AT+CIPSTO=3") == 0) { model_idle_ms = 3000U; Input("OK\r\n"); }
    else if (strncmp(command, "AT+CIPSERVER=1,", 15U) == 0 && connect_during_setup) {
        Input("OK\r\n3,CONNECT\r\n");
        Ipd(3U, "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n");
    }
    else if (strcmp(command, "AT+GMR") == 0)
        Input(modern ? "AT version:2.2.0.0(test)\r\nSDK version:test\r\nOK\r\n" :
                       "AT version:1.7.4.0(test)\r\nSDK version:3.0.4\r\nOK\r\n");
    else if (strcmp(command, "AT+UART_CUR?") == 0) {
        char response[48];
        (void)snprintf(response, sizeof(response), "+UART_CUR:115273,8,1,0,%u\r\nOK\r\n", uart_flow);
        Input(response);
    }
    else if (strcmp(command, "AT+CIPAP_CUR?") == 0)
        Input("+CIPAP_CUR:ip:\"" CFG_ESP_AP_IP "\"\r\n+CIPAP_CUR:gateway:\"" CFG_ESP_AP_IP "\"\r\n+CIPAP_CUR:netmask:\"" CFG_ESP_AP_NETMASK "\"\r\nOK\r\n");
    else if (strcmp(command, "AT+CIPAP?") == 0)
        Input("+CIPAP:ip:\"" CFG_ESP_AP_IP "\"\r\n+CIPAP:gateway:\"" CFG_ESP_AP_IP "\"\r\n+CIPAP:netmask:\"" CFG_ESP_AP_NETMASK "\"\r\nOK\r\n");
    else if (strncmp(command, "AT+CIPSEND=", 11U) == 0) {
        if (reject_send) { Input("ERROR\r\n"); return; }
        assert(sscanf(command + 11, "%u,%u", &body_id, &body_length) == 2);
        assert(body_length > 0U && body_length < sizeof(payloads[0]));
        body_left = body_length;
        body_offset = 0U;
        Input(drop_prompt ? "OK\r\n" : "OK\r\n> ");
    } else if (strncmp(command, "AT+CIPCLOSE=", 12U) == 0) {
        char response[32];
        (void)snprintf(response, sizeof(response), "%c,CLOSED\r\nOK\r\n", command[12]);
        Input(response);
    } else Input("OK\r\n");
}
static void OnTxByte(uint8_t byte)
{
    if (body_left != 0U) {
        assert(payload_count < 1024U);
        payloads[payload_count][body_offset++] = (char)byte;
        if (--body_left == 0U) {
            payloads[payload_count][body_offset] = 0;
            payload_lengths[payload_count] = body_length;
            payload_ids[payload_count] = body_id;
            payload_count++;
            if (!drop_result) Input("\r\nRecv bytes\r\nSEND OK\r\n");
        }
    } else if (byte == '\n') {
        if (command_length != 0U) OnCommand();
        command_length = 0U;
    } else if (byte != '\r') {
        assert(command_length < sizeof(command) - 1U);
        command[command_length++] = (char)byte;
    }
}
static void Tick(void)
{
    unsigned budget, port;
    uint8_t byte;
    now_ms++;
    if (model_idle_timeout && model_idle_ms != 0U && model_client >= 0 &&
        (uint32_t)(now_ms - model_last_client_data) >= model_idle_ms) {
        char response[24];
        (void)snprintf(response, sizeof(response), "%u,CLOSED\r\n", (unsigned)model_client);
        Input(response);
        model_client = -1;
    }
    for (port = 2U; port < 4U; port++) {
        budget = 11U; /* 115200 8N1, conservatively rounded down. */
        while (budget-- != 0U && tx[port].left != 0U) {
            uint8_t sent = tx[port].data[tx[port].offset++];
            tx[port].left--;
            if (port == 2U) OnTxByte(sent);
            if (tx[port].left == 0U) BSP_Uart_OnTxComplete(port == 2U ? &huart2 : &huart3);
        }
    }
    budget = 11U;
    while (budget-- != 0U && incoming_tail < incoming_head) Deliver((uint8_t)incoming[incoming_tail++]);
    if (incoming_tail == incoming_head) incoming_tail = incoming_head = 0U;
    EspLink_Service();
    BSP_Uart_Service();
    if (consume_events) {
        uint32_t events = EspLink_TakeEvents();
        observed_events |= events;
        if (events != 0U) Protocol_Reset(&parser);
    }
    if (!parse_app) return;
    budget = 64U;
    while (budget-- != 0U && EspLink_Poll(&byte)) {
        ProtocolEvent event = Protocol_Feed(&parser, byte);
        char ack[64];
        if (event.kind == PROTO_EVENT_COMMAND) {
            size_t length = Protocol_EncodeAck(ack, sizeof(ack), event.command.sequence, false);
            parsed_commands++;
            assert(EspLink_Send(ack, length));
        } else if (event.kind == PROTO_EVENT_ERROR) malformed_commands++;
    }
}
static void Run(unsigned millis) { while (millis-- != 0U) Tick(); }
static bool HasCommand(const char *value)
{
    unsigned i;
    for (i = 0; i < command_count; i++) if (strcmp(commands[i], value) == 0) return true;
    return false;
}
static unsigned AckCount(void)
{
    unsigned i, count = 0U;
    for (i = 0U; i < payload_count; ++i) {
        const char *p = payloads[i];
        while ((p = strstr(p, "<ACK,")) != NULL) { count++; p += 5; }
    }
    return count;
}
static void Reset(bool modern_profile)
{
    memset(tx, 0, sizeof(tx));
    now_ms = irq_mask = observed_events = 0U;
    incoming_head = incoming_tail = command_length = command_count = 0U;
    payload_count = body_left = body_offset = body_length = 0U;
    rx_armed = fail_rearm = error_on_unlock = false;
    rx_aborts = tx_aborts = 0U;
    drop_prompt = drop_result = reject_send = false;
    uart_flow = 0U;
    parsed_commands = malformed_commands = 0U;
    modern = modern_profile;
    model_idle_timeout = connect_during_setup = false;
    model_idle_ms = model_last_client_data = 0U;
    model_client = -1;
    auto_respond = consume_events = parse_app = true;
    BSP_Uart_Init();
    EspLink_Init();
    Protocol_Init(&parser);
}
static void Boot(bool modern_profile)
{
    Reset(modern_profile);
    Run(2000U);
    if (!EspLink_IsServerReady()) fprintf(stderr, "Boot failed: %s commands=%u last=%s\n", EspLink_LastError(), command_count, commands[command_count-1U]);
    assert(EspLink_IsServerReady() && !EspLink_IsClientConnected());
    assert(HasCommand("AT+RST") && HasCommand("AT+UART_CUR?") && HasCommand("AT+CIPRECVMODE=0"));
    assert(HasCommand(modern_profile ? "AT+CWDHCP=1,2" : "AT+CWDHCP_CUR=0,1"));
    assert(HasCommand(modern_profile ? "AT+CWDHCPS=0" : "AT+CWDHCPS_CUR=0"));
    assert(HasCommand("AT+SYSSTORE=0") == modern_profile);
    assert(HasCommand("AT+SYSMSG=0") == modern_profile);
    assert(HasCommand("AT+CIPSTO=3") && strcmp(commands[command_count - 1U], "AT+CIPSTO=3") == 0);
    assert(observed_events == 0U);
}
static void Connect(unsigned id)
{
    char response[24];
    (void)snprintf(response, sizeof(response), "%u,CONNECT\r\n", id);
    Input(response); Run(10U);
    assert(EspLink_IsClientConnected());
    model_client = (int)id;
    model_last_client_data = now_ms;
}

static void TestResponseOwnership(void)
{
    Reset(false);
    auto_respond = false;
    Input("OK\r\nOKAY\r\n"); Run(1000U);
    assert(command_count == 0U && !EspLink_IsServerReady());
    Run(300U); assert(command_count == 1U && HasCommand("AT"));
    Input("OK\r\n"); Run(20U);
    assert(HasCommand("AT+RST") && !EspLink_IsServerReady());
    Input("OK\r\n"); Run(20U); assert(command_count == 2U); /* reset needs ready */
    Input("ready\r\n"); Run(80U); assert(HasCommand("ATE0"));
    Input("OK\r\n"); Run(20U); assert(HasCommand("AT+GMR"));
    Input("ERROR\r\n"); Run(120U);
    assert(!EspLink_IsServerReady() && command_count == 5U && strcmp(commands[4], "AT+RST") == 0);
    puts("PASS response ownership and setup rejection recovery");
}
static void TestHandshakeAndRestart(void)
{
    Boot(false); Connect(3U);
    Ipd(3U, "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    assert(parsed_commands == 1U && payload_count == 1U && payload_ids[0] == 3U);
    assert(strcmp(payloads[0], "<ACK,seq=1,result=OK>\n") == 0);
    assert(command_length == 0U); /* no CRLF appended outside declared payload */
    Input("ready\r\n"); Run(400U);
    assert(EspLink_IsServerReady() && !EspLink_IsClientConnected());
    assert((observed_events & ESP_LINK_EVENT_DISCONNECTED) != 0U);
    Connect(1U); Ipd(1U, "<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    assert(payload_ids[payload_count - 1U] == 1U);
    puts("PASS nonzero link ID, LF-preserving ACK and ESP reboot");
}
static void TestProfilesAndUart(void)
{
    unsigned profile, flow;
    for (profile = 0U; profile < 2U; profile++) {
        bool modern_profile = profile != 0U;
        Boot(modern_profile); /* Actual divided baud 115273, 8N1, flow=0. */
        assert(!EspLink_ResetNeeded());
        for (flow = 1U; flow <= 3U; flow++) {
            unsigned before;
            Reset(modern_profile);
            uart_flow = flow;
            Run(2000U);
            assert(HasCommand("AT+UART_CUR?"));
            assert(EspLink_ResetNeeded() && !EspLink_IsServerReady() && !EspLink_IsClientConnected());
            assert(strstr(EspLink_LastError(), "UART") != NULL);

            /* Correcting the peer or receiving a late OK cannot clear the fault. */
            before = command_count;
            uart_flow = 0U;
            Input("+UART_CUR:115273,8,1,0,0\r\nOK\r\n");
            Run(100U);
            assert(EspLink_ResetNeeded() && !EspLink_IsServerReady());
            assert(command_count == before);

            Input("ready\r\n"); /* A real module reboot starts fresh verification. */
            Run(400U);
            assert(!EspLink_ResetNeeded() && EspLink_IsServerReady() && !EspLink_IsClientConnected());
            assert(strcmp(EspLink_LastError(), "OK") == 0);
            assert(HasCommand(modern_profile ? "AT+CWDHCP=1,2" : "AT+CWDHCP_CUR=0,1"));
            assert(HasCommand("AT+CIPSTO=3"));
        }
    }
    puts("PASS NONOS/ESP-AT UART 115273 flow 0..3 and ready-gated recovery");
}
static void TestPriorityAndLoad(void)
{
    unsigned i, start;
    Boot(false); Connect(2U); parse_app = false;
    assert(EspLink_SendStatus("<STA,seq=old>\n", 14U));
    assert(EspLink_SendStatus("<STA,seq=new>\n", 14U));
    assert(EspLink_Send("<ACK,seq=8,result=OK>\n", 22U));
    Run(100U);
    assert(payload_count == 2U && strncmp(payloads[0], "<ACK,", 5U) == 0);
    assert(strcmp(payloads[1], "<STA,seq=new>\n") == 0);
    parse_app = true;
    start = payload_count;
    for (i = 0U; i < 50U; i++) {
        Ipd(2U, "<CMD,seq=8,move=S,turn=C,step_speed=60,servo=0>\n");
        assert(EspLink_SendStatus("<STA,seq=new>\n", 14U));
        Run(200U);
    }
    assert(payload_count - start == 100U && parsed_commands == 50U && observed_events == 0U);
    puts("PASS ACK priority, latest STA coalescing, sustained 10 frames/s");
}
static void TestLargeIpdAndIsolation(void)
{
    char large[4096];
    unsigned i, before;
    Boot(false); Connect(4U);
    large[0] = 0;
    for (i = 0U; i < 30U; i++) strcat(large, "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n");
    Ipd(4U, large); Run(1000U);
    if (parsed_commands != 30U || AckCount() != 30U)
        fprintf(stderr, "burst: parsed=%u malformed=%u ACK=%u payloads=%u events=%lu error=%s\n",
                parsed_commands, malformed_commands, AckCount(), payload_count, (unsigned long)observed_events, EspLink_LastError());
    assert(parsed_commands == 30U && malformed_commands == 0U && AckCount() == 30U);
    assert(observed_events == 0U);
    Input("1,CONNECT\r\n"); Ipd(1U, "<CMD,seq=99,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    assert(HasCommand("AT+CIPCLOSE=1") && parsed_commands == 30U);
    before = parsed_commands;
    Input("4,CLOSED\r\n2,CONNECT\r\n");
    Ipd(2U, "<CMD,seq=2,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    /* The close event gates bytes until consumed; a fresh subsequent frame works. */
    Ipd(2U, "<CMD,seq=3,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    assert(parsed_commands > before && payload_ids[payload_count-1U] == 2U);
    puts("PASS multi-frame IPD, reply backpressure, second-client isolation");
}
static void TestMalformedAndLoss(void)
{
    uint8_t byte;
    unsigned i;
    const char *bad[] = { "+IPD,0,4294967296:", "+IPD,9,4:xxxx", "+IPD,0,-2:xx", "+IPD,0,12x:bad", "+IPD,,2:xx", "+IPD,0,00000000000:" };
    for (i = 0U; i < sizeof(bad)/sizeof(bad[0]); i++) {
        Boot(false); Connect(0U); Input(bad[i]); Run(50U);
        assert((observed_events & ESP_LINK_EVENT_RX_LOST) != 0U && parsed_commands == 0U);
        Run(500U); assert(EspLink_IsServerReady());
    }
    Boot(false); Connect(0U); Input("+IPD,0,10:abc"); Run(600U);
    assert((observed_events & ESP_LINK_EVENT_RX_LOST) != 0U);
    Boot(false); Connect(0U); consume_events = false;
    for (i = 0U; i < 300U; ++i) Deliver('x');
    assert(BSP_Uart_EspRxLost()); EspLink_Service();
    assert(!EspLink_Poll(&byte));
    assert((EspLink_TakeEvents() & ESP_LINK_EVENT_RX_LOST) != 0U);
    puts("PASS malformed IPD, length overflow, truncation timeout, raw ring loss gating");
}
static void TestUartRecoveryRace(void)
{
    Reset(false);
    BSP_Uart_OnError(&huart2); /* HAL still busy: main loop must abort then rearm. */
    assert(BSP_Uart_EspRxLost()); BSP_Uart_Service();
    assert(rx_aborts == 1U && rx_armed);
    error_on_unlock = true;
    assert(BSP_Uart_EspTakeRxLostAndDiscard());
    assert(BSP_Uart_EspRxLost()); /* ISR after clear was not erased. */
    BSP_Uart_Service(); (void)BSP_Uart_EspTakeRxLostAndDiscard();
    fail_rearm = true; Deliver('x');
    assert(BSP_Uart_EspRxLost() && !rx_armed);
    BSP_Uart_Service(); assert(rx_armed);
    puts("PASS HAL nonblocking error, failed rearm retry, atomic loss-clear race");
}
static void TestSendTimeout(void)
{
    unsigned count;
    Boot(false); Connect(0U); drop_prompt = true;
    assert(EspLink_Send("<ACK,seq=1,result=OK>\n", 22U)); Run(800U);
    assert(payload_count == 0U && body_left == 22U);
    assert((observed_events & ESP_LINK_EVENT_TX_FAILED) != 0U);
    count = command_count;
    Run(5100U);
    assert(EspLink_ResetNeeded() && command_count == count && payload_count == 0U);
    /* Real ESP reset is the only proof that the unknown payload phase ended. */
    body_left = 0U; drop_prompt = false; Input("ready\r\n"); Run(400U);
    assert(EspLink_IsServerReady() && !EspLink_ResetNeeded());
    Connect(0U); drop_prompt = true;
    assert(EspLink_Send("<ACK,seq=2,result=OK>\n", 22U)); Run(800U);
    Input("> "); Run(500U);
    assert(payload_count == 1U && payloads[0][0] == ' ' && payloads[0][21] == '\n');
    assert(EspLink_IsServerReady());
    Boot(false); Connect(0U); drop_result = true;
    assert(EspLink_Send("<ACK,seq=1,result=OK>\n", 22U)); Run(6000U);
    assert(payload_count == 1U && EspLink_ResetNeeded());
    puts("PASS missing prompt/result safety latch, late-prompt padding, real-reset recovery");
}

static void TestBoundaries(void)
{
    static char stream[8050];
    unsigned i;
    const char *cmd = "<CMD,seq=7,move=S,turn=C,step_speed=60,servo=0>\n";
    Boot(false); Connect(0U);
    memset(stream, 'x', 8000U);
    strcpy(stream + 8000U, cmd);
    Ipd(0U, stream); Run(1100U);
    assert(parsed_commands == 1U && observed_events == 0U);
    /* Total transfer > 500 ms is valid while UART bytes continue arriving. */
    Boot(false); Connect(0U);
    for (i = 0U; i < 36U; ++i) assert(EspLink_Send("<ACK,seq=1,result=OK>\n", 22U));
    assert(!EspLink_Send("<ACK,seq=1,result=OK>\n", 22U));
    assert((EspLink_TakeEvents() & ESP_LINK_EVENT_TX_FAILED) != 0U);
    assert(!EspLink_IsClientConnected());
    Run(500U); assert(EspLink_IsServerReady() && payload_count == 0U);
    Connect(0U); reject_send = true;
    assert(EspLink_Send("<ACK,seq=1,result=OK>\n", 22U)); Run(500U);
    assert(EspLink_IsServerReady() && !EspLink_IsClientConnected());
    assert(payload_count == 0U && (observed_events & ESP_LINK_EVENT_TX_FAILED) != 0U);
    Reset(false); now_ms = UINT32_MAX - 500U; EspLink_Init(); Run(2000U);
    assert(EspLink_IsServerReady());
    puts("PASS continuous long IPD, bounded reply saturation, terminal send error, tick wrap");
}

static void TestIdleClientRelease(void)
{
    unsigned i, before;
    char unfinished[401];
    const char *status = "<STA,seq=new>\n";
    Reset(false); connect_during_setup = true; Run(2000U);
    assert(EspLink_IsServerReady() && parsed_commands == 1U && AckCount() == 1U);
    Boot(false); Connect(2U); model_idle_timeout = true;
    for (i = 0U; i < 10U; ++i) {
        Ipd(2U, "<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n");
        assert(EspLink_SendStatus(status, strlen(status)));
        Run(500U);
        assert(EspLink_IsClientConnected());
    }
    for (i = 0U; i < 16U; ++i) {
        if (EspLink_IsClientConnected()) assert(EspLink_SendStatus(status, strlen(status)));
        Run(200U);
    }
    assert(!EspLink_IsClientConnected() && (observed_events & ESP_LINK_EVENT_DISCONNECTED) != 0U);
    Boot(false); Connect(2U);
    before = command_count;
    auto_respond = false;
    EspLink_CloseClient(); Run(20U);
    assert(!EspLink_IsClientConnected() && HasCommand("AT+CIPCLOSE=2"));
    /* Old CLOSED, new CONNECT (same reused ID), then old close command's OK. */
    Input("2,CLOSED\r\n2,CONNECT\r\nOK\r\n"); Run(20U);
    assert(EspLink_IsClientConnected() && command_count == before + 1U);
    auto_respond = true;
    Ipd(2U, "<CMD,seq=2,move=S,turn=C,step_speed=60,servo=0>\n"); Run(100U);
    assert(AckCount() == 1U && payload_ids[0] == 2U);
    Boot(false); Connect(4U); drop_prompt = true;
    assert(EspLink_Send("<ACK,seq=1,result=OK>\n", 22U)); Run(20U);
    EspLink_CloseClient(); Input("> "); Run(200U);
    assert(HasCommand("AT+CIPCLOSE=4") && EspLink_IsServerReady() && !EspLink_IsClientConnected());
    assert(payload_count == 1U && payloads[0][0] == ' ' && payloads[0][21] == '\n');
    Boot(false); Connect(1U);
    Input("+IPD,1,403:abc"); Run(10U);
    before = command_count;
    EspLink_CloseClient();
    memset(unfinished, 'x', 400U);
    memcpy(unfinished, "\r\nready\r\n", 9U);
    unfinished[400] = 0;
    Input(unfinished); Run(200U);
    assert(command_count == before + 1U && HasCommand("AT+CIPCLOSE=1"));
    assert(EspLink_IsServerReady() && !EspLink_IsClientConnected() && parsed_commands == 0U);
    puts("PASS fixed idle timeout, early handshake buffering, explicit close and old-reply isolation");
}

int main(void)
{
    TestResponseOwnership(); TestHandshakeAndRestart(); TestProfilesAndUart();
    TestPriorityAndLoad(); TestLargeIpdAndIsolation(); TestMalformedAndLoss();
    TestUartRecoveryRace(); TestSendTimeout(); TestBoundaries(); TestIdleClientRelease();
    puts("esp_link_host_test: all checks passed");
    return 0;
}
