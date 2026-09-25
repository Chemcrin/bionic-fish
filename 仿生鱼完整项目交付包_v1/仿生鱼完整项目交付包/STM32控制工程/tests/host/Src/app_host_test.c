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
static BspBridgeState bridge_a_state, bridge_b_state;
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
uint32_t BSP_Uart_EspRxLossCount(void) { return 0U; }
void BSP_RunLed_Toggle(void) {}

/* 2026-09-23：占空比改由 BSP 持有两挡状态。测试里照搬真实实现的语义，
 * 这样"挡位 → 实际 duty"这条链在宿主机上也能被断言到。 */
static BspDutyMode s_test_duty_mode = BSP_DUTY_FULL;
uint8_t BSP_DutyPercentFor(BspDutyMode mode)
{
    return (mode == BSP_DUTY_LOW) ? (uint8_t)CFG_MOTOR_LOW_DUTY_PERCENT
                                  : (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT;
}
void BSP_Board_SetDutyMode(BspDutyMode mode)
{
    s_test_duty_mode = (mode == BSP_DUTY_LOW) ? BSP_DUTY_LOW : BSP_DUTY_FULL;
}
BspDutyMode BSP_Board_GetDutyMode(void) { return s_test_duty_mode; }

void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty)
{
    assert(state == BSP_BRIDGE_FORWARD || state == BSP_BRIDGE_REVERSE);
    assert(duty == BSP_DutyPercentFor(s_test_duty_mode) && duty != 0U);
    bridge_a_state = state;
    bridge_a_coast = false;
    phase_a_calls++;
}
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty)
{
    assert(state == BSP_BRIDGE_FORWARD || state == BSP_BRIDGE_REVERSE);
    assert(duty == BSP_DutyPercentFor(s_test_duty_mode) && duty != 0U);
    bridge_b_state = state;
    bridge_b_coast = false;
    phase_b_calls++;
}
void BSP_BridgeA_Coast(void) { bridge_a_state = BSP_BRIDGE_COAST; bridge_a_coast = true; coast_a_calls++; }
void BSP_BridgeB_Coast(void) { bridge_b_state = BSP_BRIDGE_COAST; bridge_b_coast = true; coast_b_calls++; }
/* 2026-09-23：舵机行程由 +/-20 度收窄为 +/-15 度，但 1000-2000us 的电气满量程
 * 仍对应 30 度。所以 +/-15 度只给出满量程 1/2 的偏移：
 *   左(-15 度) = 1500 + 250 = 1750us ；右(+15 度) = 1500 - 250 = 1250us ；0 度 = 1500us。
 * 若这两个数字与 app_config.h 的 FULL_SCALE_DEG 不再自洽，本文件会直接断言失败。 */
#define PULSE_LEFT_15DEG   1750U
#define PULSE_RIGHT_15DEG  1250U

void BSP_Servo_SetPulseUs(uint16_t pulse) { servo_pulse = pulse; }
/* 调试按键：测试直接驱动这三个电平，绕开真实 GPIO。 */
static bool button_levels[3];
bool BSP_ButtonPressed(uint8_t index)
{
    assert(index >= 1U && index <= 3U);
    return button_levels[index - 1U];
}
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
int EspLink_ClientId(void) { return client_connected ? 0 : -1; }
uint32_t EspLink_PendingEvents(void) { return pending_events; }
const char *EspLink_StaIp(void) { return "0.0.0.0"; }
/* 每调用一次 Connect() 就换一代，模拟"新客户端"；app.c 靠它判断连接已更换。 */
static uint32_t client_generation;
uint32_t EspLink_ClientGeneration(void) { return client_generation; }
/* HTTP 响应走 SendRaw：这里原样收下来，供网页控制用例断言。 */
static char raw_output[4096];
static size_t raw_length;
bool EspLink_SendRaw(const char *data, size_t length)
{
    assert(data != NULL);
    assert(raw_length + length < sizeof(raw_output));
    memcpy(raw_output + raw_length, data, length);
    raw_length += length;
    raw_output[raw_length] = '\0';
    return true;
}
void EspLink_GetSendCounters(EspLinkSendCounters *out)
{
    assert(out != NULL);
    memset(out, 0, sizeof(*out));
}
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
void SoftI2c_SetInternalPullup(SoftI2cBus *bus, bool enabled)
{
    assert(bus != NULL);
    bus->internal_pullup = enabled;
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
/* 诊断用只读检查：主机测试不模拟电气上下拉，固定报告空闲为高，且不计入
 * i2c_calls_this_loop —— 它不发起任何总线事务。 */
bool SoftI2c_LinesReleased(const SoftI2cBus *bus)
{
    assert(bus != NULL);
    return true;
}
void SoftI2c_LineLevels(const SoftI2cBus *bus, bool *scl_high, bool *sda_high)
{
    assert(bus != NULL);
    if (scl_high != NULL) {
        *scl_high = true;
    }
    if (sda_high != NULL) {
        *sda_high = true;
    }
}
void SoftI2c_IdleLevelsWithInternalPullup(SoftI2cBus *bus, bool *scl_high, bool *sda_high)
{
    assert(bus != NULL);
    if (scl_high != NULL) {
        *scl_high = true;
    }
    if (sda_high != NULL) {
        *sda_high = true;
    }
}
const char *SoftI2c_StatusName(SoftI2cStatus status)
{
    switch (status) {
    case SOFT_I2C_OK: return "OK";
    case SOFT_I2C_NACK: return "NACK";
    case SOFT_I2C_TIMEOUT: return "TIMEOUT";
    case SOFT_I2C_BUS_STUCK: return "STUCK";
    case SOFT_I2C_ARGUMENT: return "ARG";
    default: return "UNKNOWN";
    }
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
    memset(button_levels, 0, sizeof(button_levels));
    htim2.Init.Prescaler = CFG_MOTOR_TIM_PRESCALER;
    htim2.Init.Period = CFG_MOTOR_TIM_PERIOD;
    htim3.Init.Prescaler = CFG_SERVO_TIM_PRESCALER;
    htim3.Init.Period = CFG_SERVO_TIM_PERIOD;
    htim4.Init.Prescaler = CFG_TIMEBASE_TIM_PRESCALER;
    htim4.Init.Period = CFG_TIMEBASE_TIM_PERIOD;
    App_Init();
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    assert(((s_faults & BF_FAULT_STEPPER_DISABLED) != 0U) == (CFG_MOTOR_A_ENABLED == 0U));
    assert(oled_probe_calls == 0U && oled_init_calls == 0U);
}
static void Connect(void)
{
    /* A new TCP connection has a fresh receive stream. */
    phone_length = phone_position = 0U;
    server_ready = client_connected = true;
    client_generation++;
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
#if (CFG_MOTOR_A_ENABLED != 0U)
    Input("<CMD,seq=100,move=F,turn=R,step_speed=100,servo=15>\n", now_ms);
    /* m2 缺省 = 停止，所以只有 A 桥被驱动，B 桥保持 coast。 */
    assert(!bridge_a_coast && phase_a_calls != 0U);
#else
    Input("<CMD,seq=100,move=S,turn=R,step_speed=100,servo=15>\n", now_ms);
#endif
    assert(strstr(esp_output, "<ACK,seq=100,result=OK>\n") != NULL);
    /* 2026-09-11 舵机左右对调：协议语义不变（右转/正角），但脉宽极性反转，
     * 所以 +15 度（右）落在低脉宽一侧：1500 - 250 = 1250us。 */
    assert(servo_pulse == PULSE_RIGHT_15DEG);
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
    /* 对调后：+15°（右）→ 1500 − 250 = 1250 µs */
    assert(servo_pulse == 1250U);
    ClearOutputs();
#if (CFG_MOTOR_A_ENABLED == 0U)
    Input("<CMD,seq=2,move=F,turn=L,step_speed=100,servo=-15>\n", 402U);
    assert(strstr(esp_output, "<ERR,seq=2,code=E_STEPPER_DISABLED>\n") != NULL);
#else
    /* 2026-09-12：move=R 已放开（直流电机可换向），改用真正越界的舵角
     * 来验证"被拒命令不得改动任何执行器状态"。2026-09-23 边界收窄到 ±15°，
     * 故用 -16°（刚越过新边界）而不是 -25°。 */
    Input("<CMD,seq=2,move=R,turn=L,step_speed=100,servo=-16>\n", 402U);
    assert(strstr(esp_output, "<ERR,seq=2,code=E_RANGE>\n") != NULL);
#endif
    assert(servo_pulse == 1250U && bridge_a_coast && bridge_b_coast);
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
    assert(servo_pulse == PULSE_RIGHT_15DEG); /* +15 度(右) 对调后 -> 低脉宽侧 1250us */
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
    QueueInput("<CMD,seq=0,move=S,turn=R,step_speed=60,servo=15>\n");
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
    assert(servo_pulse == PULSE_RIGHT_15DEG); /* +15 度(右) 对调后 -> 低脉宽侧 1250us */
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
    assert(servo_pulse == 1250U); /* +15°(右) 对调后 → 1250 µs */
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

#if (CFG_MOTOR_A_ENABLED != 0U)
/* 直流电机的"运行"语义与步进不同：没有换相节拍。只要方向命令不变，桥状态就必须
 * 一直保持；保活命令与舵角变化都不得把它打回 coast 或改变方向。
 * 原 TestFixedLowRunAndStop 是数换相次数的，对直流电机已无意义。 */
static void TestDualMotorHoldAndStop(void)
{
    unsigned millis, sequence = 1U, a_before, b_before;
    char frame[112];
    Boot(0U);
    Connect();
    Input("<CMD,seq=1,move=F,turn=C,step_speed=60,servo=0,m2=R>\n", 1U);
    assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_REVERSE);
    assert(!bridge_a_coast && !bridge_b_coast);
    assert(bridge_a_state == BSP_BRIDGE_FORWARD && bridge_b_state == BSP_BRIDGE_REVERSE);
    a_before = coast_a_calls;
    b_before = coast_b_calls;

    for (millis = 2U; millis <= 2001U; millis++) {
        if (millis % 200U == 0U) {
            (void)snprintf(frame, sizeof(frame),
                           "<CMD,seq=%u,move=F,turn=R,step_speed=60,servo=15,m2=R>\n",
                           ++sequence);
            Input(frame, millis);
            assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_REVERSE);
            assert(bridge_a_state == BSP_BRIDGE_FORWARD && bridge_b_state == BSP_BRIDGE_REVERSE);
            assert(!bridge_a_coast && !bridge_b_coast);
        } else {
            At(millis);
        }
    }
    /* 整整 2 秒、10 条保活 + 舵角变化，两路都不曾回到 coast。 */
    assert(coast_a_calls == a_before && coast_b_calls == b_before);
    assert(bridge_a_state == BSP_BRIDGE_FORWARD && bridge_b_state == BSP_BRIDGE_REVERSE);
    ClearOutputs();
    At(2200U);
    assert(strstr(esp_output, "step_on=1,servo=15,roll=") != NULL);
    assert(strstr(esp_output, "m1=F,m2=R,err=") != NULL);

    (void)snprintf(frame, sizeof(frame),
                   "<CMD,seq=%u,move=S,turn=C,step_speed=60,servo=0,m2=S>\n", ++sequence);
    Input(frame, 2210U);
    assert(s_motor.motor_a == BF_MOVE_STOP && s_motor.motor_b == BF_MOVE_STOP);
    assert(bridge_a_coast && bridge_b_coast);
}
static void TestOledInitializationWaitsForStop(void)
{
    Boot(0U);
    oled_present = true;
    Connect();
    ActiveCommand(1U);
    At(200U);
    assert(oled_init_calls == 0U && oled_probe_calls == 0U);
    /* ActiveCommand 不带 m2，所以只有 M1 在运行；OLED 初始化只等 M1 停。 */
    assert(!bridge_a_coast && bridge_b_coast);
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
#if (CFG_MOTOR_A_ENABLED != 0U)
    Input("<CMD,seq=1,move=F,turn=R,step_speed=100,servo=15>\n", 1U);
#else
    Input("<CMD,seq=1,move=S,turn=R,step_speed=100,servo=15>\n", 1U);
#endif
    assert(bridge_a_coast && bridge_b_coast && servo_pulse == 1500U);
    /* 直流电机是在命令落地时就写桥，所以 A 桥确实被驱动过一次；
     * 关键是同一轮里 ACK 失败已经把它打回 coast。 */
#if (CFG_MOTOR_A_ENABLED != 0U)
    assert(phase_a_calls == 1U && phase_b_calls == 0U);
#else
    /* M1 禁用时 move=F 在 RouteCommand 层就被拒，桥从未被驱动。 */
    assert(phase_a_calls == 0U && phase_b_calls == 0U);
#endif
    assert(!s_session.sequence_valid && !s_session.link_alive);
    assert((s_faults & BF_FAULT_UART_TX_DROPPED) != 0U);
}

#if (CFG_MOTOR_A_ENABLED != 0U)
static unsigned CountOccurrences(const char *haystack, const char *needle)
{
    unsigned count = 0U;
    const char *p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += strlen(needle);
    }
    return count;
}
#endif

/* 模拟一次"按下"：先在去抖窗口内抖两下，再保持稳定。只应在最后产生一次动作。
 * 注意时间必须从*t 连续推进：At() 只改全局时钟，不会回写 *t。 */
static void PressKey(uint8_t key, uint32_t *now_ms)
{
    uint32_t t = *now_ms;
    button_levels[key - 1U] = true;
    At(t + 5U);
    button_levels[key - 1U] = false;
    At(t + 10U);
    button_levels[key - 1U] = true;
    At(t + 15U);
    t += 15U + CFG_BUTTON_DEBOUNCE_MS + 5U;
    At(t);
    *now_ms = t;
}
static void ReleaseKey(uint8_t key, uint32_t *now_ms)
{
    uint32_t t = *now_ms;
    button_levels[key - 1U] = false;
    At(t + 5U);                             /* 原始采样跟到松开 */
    t += 5U + CFG_BUTTON_DEBOUNCE_MS + 5U;
    At(t);                                  /* 稳定状态跟到松开，为下一次按下沿做准备 */
    *now_ms = t;
}

/* 调试模式的核心契约：**没有上位机**时板载按键也能驱动执行器。 */
static void TestDebugButtons(void)
{
    uint32_t now = 100U;

    Boot(0U);
    assert(!client_connected && !s_session.link_alive);
    At(now);
    assert((s_motor.motor_a == BF_MOVE_STOP) && servo_pulse == CFG_SERVO_CENTER_PULSE_US);

    ClearOutputs();
    PressKey(1U, &now);
#if (CFG_MOTOR_A_ENABLED != 0U)
    assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_STOP);
    assert(strstr(debug_output, "DEBUG K1 M1 ON\r\n") != NULL);
    ReleaseKey(1U, &now);
    PressKey(1U, &now);
    assert((s_motor.motor_a == BF_MOVE_STOP) && bridge_a_coast && bridge_b_coast);
    assert(strstr(debug_output, "DEBUG K1 M1 OFF\r\n") != NULL);
    /* 去抖窗口内的抖动只能算一次按键，不能连续切换两次。 */
    assert(CountOccurrences(debug_output, "DEBUG K1 M1") == 2U);
#else
    /* 驱动被显式禁用时，调试按键同样不得启动电机。 */
    assert((s_motor.motor_a == BF_MOVE_STOP));
    assert(strstr(debug_output, "DEBUG K1 M1 ON") == NULL);
#endif
    ReleaseKey(1U, &now);

    /* K2/K3：舵机打左右极限。协议语义仍是"左=负角"，但 2026-09-11 起脉宽极性
     * 已对调，所以负角落在 MAX 脉宽上。 */
    PressKey(2U, &now);
    assert(servo_pulse == PULSE_LEFT_15DEG);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MIN_DEG);
    assert(strstr(debug_output, "DEBUG K2 servo LEFT\r\n") != NULL);
    ReleaseKey(2U, &now);

    PressKey(3U, &now);
    assert(servo_pulse == PULSE_RIGHT_15DEG);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MAX_DEG);
    assert(strstr(debug_output, "DEBUG K3 servo RIGHT\r\n") != NULL);
    ReleaseKey(3U, &now);
}

#if (CFG_MOTOR_A_ENABLED != 0U)
/* 上位机始终拥有最高优先级：远端命令必须立刻覆盖本地调试动作。 */
static void TestDebugMotionYieldsToHost(void)
{
    uint32_t now = 100U;

    Boot(0U);
    At(now);
    PressKey(1U, &now);
    assert((s_motor.motor_a != BF_MOVE_STOP));
    ReleaseKey(1U, &now);

    Connect();
    Input("<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n", now + 10U);
    assert((s_motor.motor_a == BF_MOVE_STOP) && bridge_a_coast && bridge_b_coast);

    /* 之后链路超时的失效保护不得把本地动作复活成运动状态。 */
    now += CFG_LINK_TIMEOUT_MS + 50U;
    At(now);
    assert((s_motor.motor_a == BF_MOVE_STOP) && bridge_a_coast && bridge_b_coast);
}
#endif

/* ---- 网页端：与 App 协议共用 9000 端口，按第一个字节分流 ---- */

/* 浏览器每请求一次都是一条新连接（Connection: close），这里照此模拟。 */
static char web_request[128];
static void WebGet(const char *target, uint32_t now_ms)
{
    client_connected = false;   /* 模拟上一个连接已关闭 */
    At(now_ms);
    Connect();
    raw_length = 0U;
    raw_output[0] = '\0';
    (void)snprintf(web_request, sizeof(web_request), "GET %s HTTP/1.1\r\nHost: fish\r\n\r\n", target);
    Input(web_request, now_ms + 2U);
}

static void TestHttpWebControl(void)
{
    uint32_t now = 100U;

    Boot(0U);
    At(now);

    WebGet("/", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 200 OK", 15U) == 0);
    assert(strstr(raw_output, "Content-Length:") != NULL);
    /* 控制页必须自带轮询脚本：固件 1 秒失联保护靠它维持，否则"前进"只能走一下。 */
    assert(strstr(raw_output, "fetch('/cmd?move=") != NULL);
    assert(strstr(raw_output, "setInterval(tx,400)") != NULL);
    assert(strstr(raw_output, "visibilitychange") != NULL);
    /* 2026-09-23 改版：两组电机各 3 个按键（启动/停止/反转）+ 低速挡开关。
     * 旧的「快/慢档」单电机控件已移除。 */
    assert(strstr(raw_output, "id=A1") != NULL && strstr(raw_output, "id=A0") != NULL &&
           strstr(raw_output, "id=AR") != NULL);
    assert(strstr(raw_output, "id=B1") != NULL && strstr(raw_output, "id=B0") != NULL &&
           strstr(raw_output, "id=BR") != NULL);
    assert(strstr(raw_output, "id=LOW") != NULL);
    /* 轮询 URL 必须带 m2（否则 M2 无法独立控制）与 speed（挡位）。 */
    assert(strstr(raw_output, "&m2=") != NULL);
    assert(strstr(raw_output, "&speed=") != NULL);
    /* 舵机行程已收窄到 ±15°。 */
    assert(strstr(raw_output, "SM=15") != NULL);

    /* keep-alive：同一连接上连续两个请求都要被服务（浏览器轮询依赖它）。
     * 主循环每轮最多消费 64 字节，所以两个请求要分两轮喂进去。 */
    raw_length = 0U;
    raw_output[0] = '\0';
    QueueInput("GET /api/status HTTP/1.1\r\nHost: fish\r\n\r\n"
               "GET /api/status HTTP/1.1\r\nHost: fish\r\n\r\n");
    At(now += 50U);
    At(now += 50U);
    assert(phone_position == phone_length);
    assert(strstr(raw_output, "step=") != NULL);
    assert(strstr(raw_output + 1U, "HTTP/1.1 200 OK") != NULL); /* 第二个响应也发出了 */

    WebGet("/api/status", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 200 OK", 15U) == 0);
    assert(strstr(raw_output, "step=") != NULL);
    assert(strstr(raw_output, "servo=") != NULL);

    WebGet("/favicon.ico", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 204", 12U) == 0);

    /* 前进与舵角可以合成一条命令：前进 + 左舵一次到位。 */
    WebGet("/cmd?move=F&servo=-30", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 200 OK", 15U) == 0);
#if (CFG_MOTOR_A_ENABLED != 0U)
    assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_STOP && !bridge_a_coast);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MIN_DEG);
    assert(strstr(debug_output, "DEBUG WEB forward") != NULL);
#endif
    /* STOP 在任何配置下都合法，用它来断言"网页动作确实建立了控制会话"。 */
    WebGet("/cmd?move=S", now += 100U);
    assert((s_motor.motor_a == BF_MOVE_STOP) && bridge_a_coast && bridge_b_coast);
    assert(s_session.link_alive && s_session.sequence_valid);

    /* 舵机左右极限。协议语义仍是"左=负角"，但 2026-09-11 起脉宽极性已对调。
     * 2026-09-23 边界收窄到 ±15°，故这里刻意请求 ±30° 来验证**夹紧**行为：
     * 网页端点走 ClampServo，超界静默夹到安全极限，而不是拒绝整条命令。 */
    WebGet("/servo?deg=-30", now += 100U);
    assert(servo_pulse == PULSE_LEFT_15DEG);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MIN_DEG);

    WebGet("/servo?deg=30", now += 100U);
    assert(servo_pulse == PULSE_RIGHT_15DEG);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MAX_DEG);

    /* 恰好等于新边界 ±15° 不应被夹（与上面 ±30 形成对照）。 */
    WebGet("/servo?deg=15", now += 100U);
    assert(servo_pulse == PULSE_RIGHT_15DEG);
    assert(s_servo.commanded_deg == (int8_t)CFG_SERVO_SAFE_MAX_DEG);

    /* 越界角度要被夹到安全范围，而不是拒绝整条命令。 */
    WebGet("/servo?deg=99", now += 100U);
    assert(servo_pulse == PULSE_RIGHT_15DEG);

#if (CFG_MOTOR_A_ENABLED != 0U)
    /* 后退：2026-09-12 起固件支持换向，move=R 必须真正驱动 A 桥反转。 */
    WebGet("/cmd?move=R", now += 100U);
    assert(s_motor.motor_a == BF_MOVE_REVERSE && !bridge_a_coast);
    assert(bridge_a_state == BSP_BRIDGE_REVERSE);
    assert(strstr(debug_output, "DEBUG WEB reverse") != NULL);
    WebGet("/cmd?move=S", now += 100U);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
#else
    /* M1 被编译期禁用时，move=R 必须在 RouteCommand 层就被拒，且不产生运动。 */
    WebGet("/cmd?move=R", now += 100U);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
    assert(strstr(debug_output, "DEBUG WEB rejected") != NULL);
#endif

    /* 非法请求：缺参数 / 未知路径。 */
    WebGet("/cmd", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 400", 12U) == 0);
    WebGet("/nope", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 404", 12U) == 0);

    /* 2026-09-23 新增：网页端双电机独立控制 + 低速挡。
     * 一条 /cmd 同时带 move、m2、speed，验证三者在**同一条命令**里各自生效。 */
#if (CFG_MOTOR_A_ENABLED != 0U) && (CFG_MOTOR_B_ENABLED != 0U)
    WebGet("/cmd?move=F&m2=R&speed=60", now += 100U);
    assert(strncmp(raw_output, "HTTP/1.1 200 OK", 15U) == 0);
    assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_REVERSE);
    assert(bridge_a_state == BSP_BRIDGE_FORWARD && bridge_b_state == BSP_BRIDGE_REVERSE);
    /* speed=60 必须真的把挡位切到低速挡。 */
    assert(BSP_Board_GetDutyMode() == BSP_DUTY_LOW);
    assert(strstr(raw_output, "m1=F") != NULL && strstr(raw_output, "m2=R") != NULL);

    /* 满速挡：speed=100 切回满速。 */
    WebGet("/cmd?move=F&m2=R&speed=100", now += 100U);
    assert(BSP_Board_GetDutyMode() == BSP_DUTY_FULL);

    /* 只停 M1，M2 必须保持反转不变。 */
    WebGet("/cmd?move=S&m2=R&speed=100", now += 100U);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
    assert(s_motor.motor_b == BF_MOVE_REVERSE && !bridge_b_coast);

    /* 两路同时停止。 */
    WebGet("/cmd?move=S&m2=S&speed=100", now += 100U);
    assert(bridge_a_coast && bridge_b_coast);
#endif

    /* 关键回归：HTTP 之后，协议客户端（首字节 '<'）必须仍然完全正常。
     * 注意序号必须落在窗口内——网页端点已经把 last_sequence 推进了，
     * 这里沿用 last_sequence+1，同时也验证了两种客户端共享同一个序号窗口。 */
    client_connected = false;
    At(now += 100U);
    Connect();
    ClearOutputs();
    (void)snprintf(web_request, sizeof(web_request),
                   "<CMD,seq=%u,move=S,turn=C,step_speed=60,servo=0>\n",
                   (unsigned)(uint16_t)(s_session.last_sequence + 1U));
    Input(web_request, now += 100U);
    assert(strstr(esp_output, "result=OK>\n") != NULL);
    assert(strstr(esp_output, "<ACK,seq=") != NULL);

    /* 反向验证：网页推进序号后，用旧序号发协议命令必须被序号窗口拒绝。 */
    ClearOutputs();
    Input("<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0>\n", now += 100U);
    assert(strstr(esp_output, "E_SEQ_OLD") != NULL);
}

#if (CFG_MOTOR_A_ENABLED != 0U)
/* 网页端落地的是同一套序号窗口：同 seq 同载荷必须是 DUP，不重复驱动。 */
static void TestHttpSequenceWindow(void)
{
    uint32_t now = 100U;

    Boot(0U);
    At(now);
    WebGet("/cmd?move=F", now += 100U);
    assert((s_motor.motor_a != BF_MOVE_STOP));
    assert(s_session.last_sequence == 1U);

    /* HTTP 每次都用 last_sequence+1，所以连续两次前进是两条新命令，而不是 DUP。 */
    WebGet("/cmd?move=F", now += 100U);
    assert((s_motor.motor_a != BF_MOVE_STOP));
    assert(s_session.last_sequence == 2U);
}
#endif

#if (CFG_MOTOR_A_ENABLED != 0U)
/* 新增：两路直流电机必须各自独立，一条命令同时落地两边。 */
static void TestDualMotorIndependent(void)
{
    uint32_t now = 100U;
    Boot(0U);
    Connect();

    Input("<CMD,seq=1,move=F,turn=C,step_speed=60,servo=0,m2=R>\n", now += 10U);
    assert(s_motor.motor_a == BF_MOVE_FORWARD && bridge_a_state == BSP_BRIDGE_FORWARD);
    assert(s_motor.motor_b == BF_MOVE_REVERSE && bridge_b_state == BSP_BRIDGE_REVERSE);
    assert(!bridge_a_coast && !bridge_b_coast);
    ClearOutputs();
    At(now + CFG_STATUS_PERIOD_MS);
    assert(strstr(esp_output, "m1=F,m2=R,err=") != NULL);

    /* 只停 M1：M2 必须保持后退不变。 */
    Input("<CMD,seq=2,move=S,turn=C,step_speed=60,servo=0,m2=R>\n", now += 20U);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
    assert(s_motor.motor_b == BF_MOVE_REVERSE && !bridge_b_coast);

    /* 只换 M2 方向：M1 保持停止。 */
    Input("<CMD,seq=3,move=S,turn=C,step_speed=60,servo=0,m2=F>\n", now += 20U);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
    assert(s_motor.motor_b == BF_MOVE_FORWARD && bridge_b_state == BSP_BRIDGE_FORWARD);

    /* 失联保护必须同时停两路。 */
    now += CFG_LINK_TIMEOUT_MS + 10U;
    At(now);
    assert(s_motor.motor_a == BF_MOVE_STOP && s_motor.motor_b == BF_MOVE_STOP);
    assert(bridge_a_coast && bridge_b_coast);
}

/* 新增：m2 是可选字段。旧 5 字段客户端必须仍被接受，且 M2 落到停止。 */
static void TestM2OptionalField(void)
{
    uint32_t now = 100U;
    Boot(0U);
    Connect();

    /* 先把 M2 转起来，确保后面观察到的"停止"不是因为本来就停着。 */
    Input("<CMD,seq=1,move=S,turn=C,step_speed=60,servo=0,m2=F>\n", now += 10U);
    assert(s_motor.motor_b == BF_MOVE_FORWARD && !bridge_b_coast);

    /* 旧版 5 字段帧（无 m2）：仍须被接受，且 M2 被显式落停止。 */
    Input("<CMD,seq=2,move=F,turn=C,step_speed=60,servo=0>\n", now += 10U);
    assert(strstr(esp_output, "<ACK,seq=2,result=OK>\n") != NULL);
    assert(s_motor.motor_a == BF_MOVE_FORWARD && !bridge_a_coast);
    assert(s_motor.motor_b == BF_MOVE_STOP && bridge_b_coast);
    ClearOutputs();
    At(now + CFG_STATUS_PERIOD_MS);
    assert(strstr(esp_output, "m1=F,m2=S,err=") != NULL);

    /* 非法 m2 值必须整条拒绝，且不改动任何执行器。 */
    Input("<CMD,seq=3,move=F,turn=C,step_speed=60,servo=0,m2=X>\n", now += 20U);
    assert(strstr(esp_output, "<ERR,seq=3,code=E_RANGE>\n") != NULL);
    assert(s_motor.motor_a == BF_MOVE_FORWARD && s_motor.motor_b == BF_MOVE_STOP);
}

#endif /* CFG_MOTOR_A_ENABLED != 0U */

#if (CFG_MOTOR_A_ENABLED == 0U)
/* M1 被编译期禁用时的专用契约：M1 运动命令必须被拒，但 M2 完全不受影响。 */
static void TestM1DisabledM2Available(void)
{
    uint32_t now = 100U;
    Boot(0U);
    Connect();

    Input("<CMD,seq=1,move=F,turn=C,step_speed=60,servo=0,m2=S>\n", now += 10U);
    assert(strstr(esp_output, "<ERR,seq=1,code=E_STEPPER_DISABLED>\n") != NULL);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);

    /* M2 不受 M1 禁用影响：同一条命令里 M2 仍然照常执行。 */
    Input("<CMD,seq=2,move=S,turn=C,step_speed=60,servo=0,m2=F>\n", now += 10U);
    assert(strstr(esp_output, "<ACK,seq=2,result=OK>\n") != NULL);
    assert(s_motor.motor_b == BF_MOVE_FORWARD && bridge_b_state == BSP_BRIDGE_FORWARD);
    assert(s_motor.motor_a == BF_MOVE_STOP && bridge_a_coast);
}
#endif /* CFG_MOTOR_A_ENABLED == 0U */

int main(void)
{
    TestHandshakeAndRejectedCommand();
    TestLinkEvents();
    TestTimeoutAndWrap();
    TestFrameTimeout();
    TestOledLateArrivalAndPowerCycle();
    TestDebugButtons();
    TestHttpWebControl();
#if (CFG_MOTOR_A_ENABLED != 0U)
    TestDualMotorHoldAndStop();
    TestDualMotorIndependent();
    TestM2OptionalField();
    TestOledInitializationWaitsForStop();
    TestDebugMotionYieldsToHost();
    TestHttpSequenceWindow();
#else
    TestM1DisabledM2Available();
#endif
    TestAckFailureStopsInSameLoop();
    puts("app_host_test: PASS");
    return 0;
}
