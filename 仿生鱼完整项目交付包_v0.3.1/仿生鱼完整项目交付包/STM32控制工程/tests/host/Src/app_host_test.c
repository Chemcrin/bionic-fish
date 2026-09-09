#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Include the real application so its session/parser can supplement observable
 * protocol frames and actuator outputs. Do not also compile app.c separately. */
#include "../../../app/Src/app.c"

GPIO_TypeDef host_gpio_a, host_gpio_b, host_gpio_c;
TIM_HandleTypeDef htim2, htim3, htim4;
static uint32_t clock_ms, clock_us, time_reads;
static bool client_connected, server_ready, fail_ack_send;
static uint32_t pending_events;
static char phone_input[1024];
static size_t phone_length, phone_position;
static char esp_output[32768], debug_output[32768];
static size_t esp_length, debug_length;
static unsigned esp_send_attempts, esp_status_attempts, close_client_calls;
static uint16_t debug_free;
static bool bridge_a_coast, bridge_b_coast;
static unsigned phase_a_calls, phase_b_calls, coast_a_calls, coast_b_calls;
static uint16_t servo_pulse;
static bool oled_present, oled_write_failure;
static uint8_t oled_address;
static unsigned oled_init_calls, oled_probe_calls, i2c_calls_this_loop;

static void Append(char *out, size_t *used, size_t capacity, const char *data, size_t length)
{
    assert(*used + length < capacity);
    memcpy(out + *used, data, length);
    *used += length;
    out[*used] = '\0';
}

uint32_t BSP_Millis(void)
{
    /* Fixed fake time also detects accidental blocking initialization loops. */
    assert(++time_reads < 1000U);
    return clock_ms;
}
uint32_t BSP_Micros(void) { return clock_us; }
void BSP_Board_Init(void) {}
void BSP_Uart_Init(void) {}
void BSP_Uart_Service(void) {}
void BSP_RunLed_Toggle(void) {}
void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty)
{
    assert(state == BSP_BRIDGE_FORWARD || state == BSP_BRIDGE_REVERSE);
    assert(duty == CFG_STEPPER_WINDING_PWM_PERCENT && duty != 0U);
    bridge_a_coast = false;
    phase_a_calls++;
}
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty)
{
    assert(state == BSP_BRIDGE_FORWARD || state == BSP_BRIDGE_REVERSE);
    assert(duty == CFG_STEPPER_WINDING_PWM_PERCENT && duty != 0U);
    bridge_b_coast = false;
    phase_b_calls++;
}
void BSP_BridgeA_Coast(void) { bridge_a_coast = true; coast_a_calls++; }
void BSP_BridgeB_Coast(void) { bridge_b_coast = true; coast_b_calls++; }
void BSP_Servo_SetPulseUs(uint16_t pulse) { servo_pulse = pulse; }
uint16_t BSP_Uart_DebugTxFree(void) { return debug_free; }
bool BSP_Uart_SendDebug(const char *data, size_t length)
{
    if (length > debug_free) { return false; }
    Append(debug_output, &debug_length, sizeof(debug_output), data, length);
    return true;
}

void EspLink_Init(void)
{
    client_connected = server_ready = fail_ack_send = false;
    pending_events = 0U;
    phone_length = phone_position = 0U;
}
void EspLink_Service(void) {}
uint32_t EspLink_TakeEvents(void)
{
    uint32_t events = pending_events;
    pending_events = 0U;
    return events;
}
bool EspLink_Poll(uint8_t *byte)
{
    if (!client_connected || pending_events != 0U || phone_position == phone_length) { return false; }
    *byte = (uint8_t)phone_input[phone_position++];
    return true;
}
bool EspLink_IsClientConnected(void) { return client_connected; }
bool EspLink_IsServerReady(void) { return server_ready; }
void EspLink_CloseClient(void)
{
    close_client_calls++;
    if (client_connected) {
        client_connected = false;
        phone_length = phone_position = 0U;
        pending_events |= ESP_LINK_EVENT_DISCONNECTED;
    }
}
bool EspLink_ResetNeeded(void) { return false; }
const char *EspLink_LastError(void) { return "NONE"; }
bool EspLink_Send(const char *frame, size_t length)
{
    esp_send_attempts++;
    if (!client_connected) { return false; }
    if (fail_ack_send && length >= 4U && memcmp(frame, "<ACK", 4U) == 0) {
        pending_events |= ESP_LINK_EVENT_TX_FAILED;
        client_connected = false;
        phone_length = phone_position = 0U;
        return false;
    }
    Append(esp_output, &esp_length, sizeof(esp_output), frame, length);
    return true;
}
bool EspLink_SendStatus(const char *frame, size_t length)
{
    esp_status_attempts++;
    if (!client_connected) { return false; }
    Append(esp_output, &esp_length, sizeof(esp_output), frame, length);
    return true;
}

void SoftI2c_Init(SoftI2cBus *bus, GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin, uint16_t half_period_us)
{
    memset(bus, 0, sizeof(*bus));
    bus->scl_port = scl_port;
    bus->sda_port = sda_port;
    bus->scl_pin = scl_pin;
    bus->sda_pin = sda_pin;
    bus->half_period_us = half_period_us;
}
SoftI2cStatus SoftI2c_BusRecover(SoftI2cBus *bus)
{
    assert(bus != NULL);
    i2c_calls_this_loop++;
    return SOFT_I2C_OK;
}
SoftI2cStatus SoftI2c_Probe(SoftI2cBus *bus, uint8_t address)
{
    i2c_calls_this_loop++;
    if (bus->scl_pin == JY61P_SCL_Pin) {
        return address == CFG_JY61P_ADDR_7BIT ? SOFT_I2C_OK : SOFT_I2C_NACK;
    }
    assert(bus->scl_pin == OLED_SCL_Pin);
    oled_probe_calls++;
    return oled_present && address == oled_address ? SOFT_I2C_OK : SOFT_I2C_NACK;
}
SoftI2cStatus SoftI2c_ReadRegister(SoftI2cBus *bus, uint8_t address, uint8_t reg,
                                    uint8_t *data, uint16_t length)
{
    assert(bus->scl_pin == JY61P_SCL_Pin && address == CFG_JY61P_ADDR_7BIT);
    assert(reg == CFG_JY61P_ANGLE_START_REG && length == 6U);
    i2c_calls_this_loop++;
    memset(data, 0, length);
    return SOFT_I2C_OK;
}
SoftI2cStatus SoftI2c_Write(SoftI2cBus *bus, uint8_t address,
                             const uint8_t *data, uint16_t length)
{
    assert(bus->scl_pin == OLED_SCL_Pin && data != NULL && length != 0U);
    i2c_calls_this_loop++;
    if (!oled_present || address != oled_address || oled_write_failure) {
        return SOFT_I2C_NACK;
    }
    if (data[0] == 0x00U && length > 4U) {
        assert(data[1] == 0xAEU && data[length - 1U] == 0xAFU);
        oled_init_calls++;
    }
    return SOFT_I2C_OK;
}

static void ClearOutputs(void)
{
    esp_length = debug_length = 0U;
    esp_output[0] = debug_output[0] = '\0';
}
static void At(uint32_t now_ms)
{
    clock_ms = now_ms;
    clock_us = now_ms * 1000U;
    time_reads = i2c_calls_this_loop = 0U;
    App_Process();
    /* At most a sample, a bounded OLED attempt and one scan, not a whole scan. */
    assert(i2c_calls_this_loop <= 5U);
}
static void Boot(uint32_t now_ms)
{
    clock_ms = now_ms;
    clock_us = now_ms * 1000U;
    time_reads = i2c_calls_this_loop = 0U;
    ClearOutputs();
    esp_send_attempts = esp_status_attempts = close_client_calls = 0U;
    debug_free = 511U;
    phase_a_calls = phase_b_calls = coast_a_calls = coast_b_calls = 0U;
    oled_init_calls = oled_probe_calls = 0U;
    oled_present = oled_write_failure = false;
    oled_address = CFG_OLED_ADDR_7BIT;
    htim2.Init.Prescaler = CFG_MOTOR_TIM_PRESCALER;
    htim2.Init.Period = CFG_MOTOR_TIM_PERIOD;
    htim3.Init.Prescaler = CFG_SERVO_TIM_PRESCALER;
    htim3.Init.Period = CFG_SERVO_TIM_PERIOD;
    htim4.Init.Prescaler = CFG_TIMEBASE_TIM_PRESCALER;
    htim4.Init.Period = CFG_TIMEBASE_TIM_PERIOD;
    App_Init();
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(((s_faults & BF_FAULT_STEPPER_DISABLED) != 0U) == (CFG_STEPPER_DRIVER_ENABLED == 0U));
    assert(oled_probe_calls == 0U && oled_init_calls == 0U);
}
static void Connect(void)
{
    /* A new TCP connection has a fresh receive stream. */
    phone_length = phone_position = 0U;
    server_ready = client_connected = true;
}
static void QueueInput(const char *frame)
{
    assert(phone_position == phone_length);
    phone_length = strlen(frame);
    assert(phone_length < sizeof(phone_input));
    memcpy(phone_input, frame, phone_length);
    phone_position = 0U;
}
static void Input(const char *frame, uint32_t now_ms)
{
    QueueInput(frame);
    At(now_ms);
    assert(phone_position == phone_length);
}
static void ActiveCommand(uint32_t now_ms)
{
#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
    Input("<CMD,seq=100,move=F,turn=R,step_speed=100,servo=30>\n", now_ms);
    assert(!bridge_a_coast && !bridge_b_coast && phase_a_calls != 0U);
#else
    Input("<CMD,seq=100,move=S,turn=R,step_speed=100,servo=30>\n", now_ms);
#endif
    assert(strstr(esp_output, "<ACK,seq=100,result=OK>\n") != NULL);
    assert(servo_pulse == 2000U);
}

static void TestHandshakeAndRejectedCommand(void)
{
    Boot(0U);
    At(0U);
    At(200U);
    assert(esp_send_attempts == 0U && esp_status_attempts == 0U);
    assert((s_faults & BF_FAULT_UART_TX_DROPPED) == 0U);
    assert(strstr(debug_output, "<STA,seq=0,link=0,") != NULL);
    assert(strstr(debug_output, "ESP server=0 tcp=0") != NULL);
    assert(strstr(debug_output, "error=NONE") != NULL);
    Connect();
    Input("<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n", 201U);
    assert(strstr(esp_output, "<ACK,seq=0,result=OK>\n") != NULL);
    At(400U);
    assert(strstr(esp_output, "<STA,seq=0,link=1,step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,servo=0,") != NULL);
    Input("<CMD,seq=1,move=S,turn=R,step_speed=60,servo=15>\n", 401U);
    assert(servo_pulse == 1750U);
    ClearOutputs();
#if (CFG_STEPPER_DRIVER_ENABLED == 0U)
    Input("<CMD,seq=2,move=F,turn=L,step_speed=100,servo=-30>\n", 402U);
    assert(strstr(esp_output, "<ERR,seq=2,code=E_STEPPER_DISABLED>\n") != NULL);
#else
    Input("<CMD,seq=2,move=R,turn=L,step_speed=100,servo=-30>\n", 402U);
    assert(strstr(esp_output, "<ERR,seq=2,code=E_STEP_REVERSE_UNSUPPORTED>\n") != NULL);
#endif
    assert(servo_pulse == 1750U && bridge_a_coast && bridge_b_coast);
    assert(s_session.last_sequence == 1U && s_session.last_valid_command_ms == 401U);
    Input("<CMD,seq=1,move=S,turn=R,step_speed=60,servo=15>\n", 500U);
    assert(strstr(esp_output, "<ACK,seq=1,result=DUP>\n") != NULL);
}

static void TestLinkEvents(void)
{
    static const uint32_t event_cases[] = {
        ESP_LINK_EVENT_RX_LOST, ESP_LINK_EVENT_DISCONNECTED, ESP_LINK_EVENT_TX_FAILED
    };
    unsigned i;
    for (i = 0U; i < sizeof(event_cases) / sizeof(event_cases[0]); i++) {
        unsigned old_coast_a, old_coast_b;
        Boot(0U);
        Connect();
        ActiveCommand(1U);
        Input("<CMD,seq=101,", 2U);
        assert(s_parser.state != PROTO_WAIT_START);
        old_coast_a = coast_a_calls;
        old_coast_b = coast_b_calls;
        ClearOutputs();
        /* Real link resets its queues before publishing these events. */
        pending_events = event_cases[i];
        client_connected = false;
        At(3U);
        assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
        assert(coast_a_calls > old_coast_a && coast_b_calls > old_coast_b);
        assert(!s_session.sequence_valid && !s_session.link_alive);
        assert(s_parser.state == PROTO_WAIT_START);
        if (event_cases[i] == ESP_LINK_EVENT_RX_LOST) {
            assert(strstr(debug_output, "<ERR,seq=NA,code=E_RX_OVERFLOW>\n") != NULL);
            assert((s_faults & BF_FAULT_ESP_RX_LOST) != 0U);
        }
        assert(((s_faults & BF_FAULT_UART_TX_DROPPED) != 0U) ==
               (event_cases[i] == ESP_LINK_EVENT_TX_FAILED));
        At(201U);
        assert(strstr(debug_output, "link=0,step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,servo=0,") != NULL);
        Connect();
        Input("<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n", 202U);
        assert(strstr(esp_output, "<ACK,seq=0,result=OK>\n") != NULL);
        assert(s_session.sequence_valid && s_session.link_alive);
    }
}

static void TestTimeoutAndWrap(void)
{
    Boot(0U);
    Connect();
    ActiveCommand(100U);
    At(1099U);
    assert(servo_pulse == 2000U);
    assert(close_client_calls == 0U && client_connected);
    At(1100U);
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(!s_session.sequence_valid && !s_session.link_alive);
    assert(close_client_calls == 1U && !client_connected);
    At(1300U);
    /* TCP is now closed, so only the independent debug UART reports status. */
    assert(strstr(debug_output, "link=0,step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,servo=0,") != NULL);
    assert(close_client_calls == 1U);
    ClearOutputs();
    QueueInput("<CMD,seq=0,move=S,turn=R,step_speed=60,servo=30>\n");
    At(1301U); /* Expired socket bytes cannot resurrect its command session. */
    assert(phone_position == 0U && esp_length == 0U);
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(!s_session.sequence_valid && !s_session.link_alive);
    Connect();
    Input("<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n", 1302U);
    assert(strstr(esp_output, "<ACK,seq=0,result=OK>\n") != NULL);
    assert(s_session.sequence_valid && s_session.link_alive && servo_pulse == 1500U);

    Boot(UINT32_MAX - 499U);
    Connect();
    ActiveCommand(UINT32_MAX - 499U);
    At(499U);
    assert(servo_pulse == 2000U);
    assert(close_client_calls == 0U && client_connected);
    At(500U);
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(!s_session.sequence_valid);
    assert(close_client_calls == 1U && !client_connected);
}

static void TestFrameTimeout(void)
{
    Boot(0U);
    Connect();
    Input("<CMD,seq=1,", 10U);
    At(259U);
    assert(strstr(esp_output, "E_FRAME_TIMEOUT") == NULL);
    At(260U);
    assert(strstr(esp_output, "<ERR,seq=NA,code=E_FRAME_TIMEOUT>\n") != NULL);
    assert(s_parser.state == PROTO_WAIT_START);
}

static void TestOledLateArrivalAndPowerCycle(void)
{
    unsigned i, first_init;
    Boot(0U);
    Connect();
    Input("<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n", 1U);
    At(200U); /* Missing screen must not stall the link or scan all addresses. */
    assert(!s_display.initialized && oled_init_calls == 0U);
    Input("<CMD,seq=1,move=S,turn=R,step_speed=60,servo=15>\n", 201U);
    assert(strstr(esp_output, "<ACK,seq=1,result=OK>\n") != NULL);
    assert(servo_pulse == 1750U);
    oled_present = true;
    At(2200U); /* Other candidate address may be tried first. */
    At(4200U);
    assert(s_display.initialized && oled_init_calls == 1U);
    for (i = 0U; i < 150U; i++) { At(4200U); }
    assert(s_display.initialized && s_ui.display == &s_display);
    assert((s_faults & BF_FAULT_OLED_I2C) == 0U);
    first_init = oled_init_calls;
    oled_write_failure = true;
    At(4401U);
    assert(!s_display.initialized && (s_faults & BF_FAULT_OLED_I2C) != 0U);
    ClearOutputs();
    Connect(); /* The earlier idle command session expired while OLED retried. */
    Input("<CMD,seq=0,move=S,turn=C,step_speed=60,servo=0>\n", 4402U);
    assert(strstr(esp_output, "<ACK,seq=0,result=OK>\n") != NULL);
    oled_write_failure = false;
    At(6200U);
    At(8200U); /* Permit one failed retry while the screen was unavailable. */
    for (i = 0U; i < 150U; i++) { At(8200U); }
    assert(oled_init_calls > first_init && s_display.initialized);
    assert((s_faults & BF_FAULT_OLED_I2C) == 0U);
}

#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
static void TestFixedLowRunAndStop(void)
{
    unsigned millis, sequence = 1U, phases;
    char frame[96];
    Boot(0U);
    Connect();
    Input("<CMD,seq=1,move=F,turn=C,step_speed=60,servo=0>\n", 1U);
    for (millis = 2U; millis <= 2001U; millis++) {
        if (millis % 200U == 0U) {
            /* Old speed tokens and servo changes must not restart the phase clock. */
            (void)snprintf(frame, sizeof(frame),
                           "<CMD,seq=%u,move=F,turn=R,step_speed=%u,servo=15>\n",
                           ++sequence, (millis / 200U) & 1U ? 100U : 60U);
            Input(frame, millis);
        } else {
            At(millis);
        }
    }
    assert(s_motor.step_running && !bridge_a_coast && !bridge_b_coast);
    assert(phase_a_calls == 41U && phase_b_calls == 41U);
    assert(strstr(esp_output, "step_rpm=NA,step_est=NA,step_actual=NA,step_on=1,") != NULL);
    ClearOutputs();
    phases = phase_a_calls;
    (void)snprintf(frame, sizeof(frame),
                   "<CMD,seq=%u,move=S,turn=C,step_speed=60,servo=0>\n", ++sequence);
    Input(frame, 2002U);
    At(2202U);
    assert(!s_motor.step_running && bridge_a_coast && bridge_b_coast);
    assert(phase_a_calls == phases && phase_b_calls == phases);
    assert(strstr(esp_output, "step_rpm=NA,step_est=NA,step_actual=NA,step_on=0,") != NULL);
}

static void TestOledInitializationWaitsForStop(void)
{
    Boot(0U);
    oled_present = true;
    Connect();
    ActiveCommand(1U);
    At(200U);
    assert(oled_init_calls == 0U && oled_probe_calls == 0U);
    assert(!bridge_a_coast && !bridge_b_coast);
    Input("<CMD,seq=101,move=S,turn=C,step_speed=60,servo=0>\n", 201U);
    assert(strstr(esp_output, "<ACK,seq=101,result=OK>\n") != NULL);
    assert(bridge_a_coast && bridge_b_coast);
    assert(oled_init_calls == 1U && s_display.initialized);
}
#endif

static void TestAckFailureStopsInSameLoop(void)
{
    Boot(0U);
    Connect();
    fail_ack_send = true;
#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
    Input("<CMD,seq=1,move=F,turn=R,step_speed=100,servo=30>\n", 1U);
#else
    Input("<CMD,seq=1,move=S,turn=R,step_speed=100,servo=30>\n", 1U);
#endif
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(phase_a_calls == 0U && phase_b_calls == 0U);
    assert(!s_session.sequence_valid && !s_session.link_alive);
    assert((s_faults & BF_FAULT_UART_TX_DROPPED) != 0U);
}

int main(void)
{
    TestHandshakeAndRejectedCommand();
    TestLinkEvents();
    TestTimeoutAndWrap();
    TestFrameTimeout();
    TestOledLateArrivalAndPowerCycle();
#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
    TestFixedLowRunAndStop();
    TestOledInitializationWaitsForStop();
#endif
    TestAckFailureStopsInSameLoop();
    puts("app_host_test: PASS");
    return 0;
}
