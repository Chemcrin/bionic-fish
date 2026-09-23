#include "app.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "app_types.h"
#include "ascii_protocol.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "bsp_uart.h"
#include "control_arbiter.h"
#include "esp_link.h"
#include "faults.h"
#include "http_ui.h"
#include "jy61p.h"
#include "main.h"
#include "motor_control.h"
#include "oled_ui.h"
#include "servo_control.h"
#include "soft_i2c.h"
#include "ssd1306.h"
#include "tim.h"

static ControlSession s_session;
static ProtocolParser s_parser;
static MotorController s_motor;
static ServoController s_servo;
static SoftI2cBus s_jy61p_bus;
static SoftI2cBus s_oled_bus;
static Jy61p s_jy61p;
static Ssd1306 s_display;
static OledUi s_ui;
static BfSystemSnapshot s_snapshot;
static uint32_t s_faults;
static uint32_t s_next_status_ms;
static uint32_t s_next_led_ms;
static uint32_t s_protocol_last_byte_ms;
static uint32_t s_next_oled_retry_ms;
static uint8_t s_oled_address_index;
static uint8_t s_scan_address;
static bool s_scan_found;
static bool s_last_server_ready;
static bool s_last_client_connected;
static uint32_t s_next_link_diagnostic_ms;
static uint32_t s_accepted_commands;
static uint32_t s_next_imu_diag_ms;
static uint32_t s_next_imu_scan_ms;
static uint8_t s_imu_scan_address;
static bool s_imu_scan_found;
static bool s_imu_scan_done;

static void SetFault(BfFault fault)
{
    s_faults |= (uint32_t)fault;
}

static void ClearFault(BfFault fault)
{
    s_faults &= ~(uint32_t)fault;
}

/* 即使编译期配置已经做过算术约束，也要在应用启动时核对实际初始化进了同一组
 * PSC/ARR。若 CubeMX 重新生成时改坏了定时器，禁止后续控制帧重新使能电机。 */
static bool RuntimeTimerConfigurationMatches(void)
{
    return (htim2.Init.Prescaler == CFG_MOTOR_TIM_PRESCALER) &&
           (htim2.Init.Period == CFG_MOTOR_TIM_PERIOD) &&
           (htim3.Init.Prescaler == CFG_SERVO_TIM_PRESCALER) &&
           (htim3.Init.Period == CFG_SERVO_TIM_PERIOD) &&
           (htim4.Init.Prescaler == CFG_TIMEBASE_TIM_PRESCALER) &&
           (htim4.Init.Period == CFG_TIMEBASE_TIM_PERIOD);
}

static void Broadcast(const char *message, size_t length)
{
    /* 无 TCP 客户端时仍向调试口报告，不能把正常离线视为 TX 丢帧。 */
    bool esp_ok = !EspLink_IsClientConnected() || EspLink_Send(message, length);
    bool debug_ok = BSP_Uart_SendDebug(message, length);
    if (!esp_ok || !debug_ok) {
        SetFault(BF_FAULT_UART_TX_DROPPED);
    }
}

static void ReplyError(bool has_sequence, uint16_t sequence, ProtocolError error)
{
    char frame[72];
    size_t length = Protocol_EncodeError(frame, sizeof(frame), has_sequence, sequence, error);
    if (length != 0U) {
        Broadcast(frame, length);
    }
}

static void ReplyAck(uint16_t sequence, bool duplicate)
{
    char frame[48];
    size_t length = Protocol_EncodeAck(frame, sizeof(frame), sequence, duplicate);
    if (length != 0U) {
        Broadcast(frame, length);
    }
}

static void ApplyAcceptedCommand(const BfRemoteCommand *command)
{
    /* step_rpm 现在是**占空比挡位**的载体（历史字段名保留以免破坏协议词表）：
     *   100 = 满速挡（BSP_DUTY_FULL）
     *   60  = 低速挡（BSP_DUTY_LOW）
     * 在校验层它仍被限制为 60/100 两个合法值。挡位必须在落地方向之前设置，
     * 否则本帧的 BSP_BridgeX_Set 会用到上一帧的占空比。 */
    BSP_Board_SetDutyMode((command->step_rpm == 60U) ? BSP_DUTY_LOW : BSP_DUTY_FULL);
    Motor_ApplyCommand(&s_motor, command);
    Servo_SetAngle(&s_servo, command->servo_deg);
    ClearFault(BF_FAULT_LINK_TIMEOUT);
    ClearFault(BF_FAULT_ESP_RX_LOST);
}

/* 远端 CMD 与网页端点的**唯一**落地路径：校验 → 序号窗口 → 生效。
 * 返回 PROTO_ERR_NONE 表示已生效（含幂等重传，*duplicate 置位）；
 * 其余返回值是拒绝原因，由调用方决定怎么回报（协议回 ERR，网页回 4xx 文本）。 */
static ProtocolError RouteCommand(BfRemoteCommand *command, uint32_t now_ms, bool *duplicate)
{
    ProtocolError error;
    ControlDecision decision;

    *duplicate = false;
    error = Control_ValidateCommand(command);
    if (error != PROTO_ERR_NONE) {
        return error;
    }
    if ((s_faults & (uint32_t)BF_FAULT_CONFIGURATION) != 0U) {
        return PROTO_ERR_CONFIGURATION;
    }
    decision = Control_ClassifyCommand(&s_session, command, &error);
    if (decision == CONTROL_REJECTED) {
        return error;
    }
    if (decision == CONTROL_DUPLICATE_COMMAND) {
        Control_AcceptDuplicate(&s_session, now_ms);
        *duplicate = true;
        return PROTO_ERR_NONE;
    }
    Control_AcceptNew(&s_session, command, now_ms);
    s_accepted_commands++;
    ApplyAcceptedCommand(command);
    return PROTO_ERR_NONE;
}

static void HandleProtocolEvent(ProtocolEvent event, uint32_t now_ms)
{
    ProtocolError error;
    bool duplicate = false;

    if (event.kind == PROTO_EVENT_ERROR) {
        ReplyError(event.sequence_present, event.sequence, event.error);
        return;
    }
    if (event.kind != PROTO_EVENT_COMMAND) {
        return;
    }

    error = RouteCommand(&event.command, now_ms, &duplicate);
    if (error != PROTO_ERR_NONE) {
        ReplyError(true, event.command.sequence, error);
        return;
    }
    ReplyAck(event.command.sequence, duplicate);
}

static void RefreshSnapshot(void)
{
    s_snapshot.command_link_alive = s_session.link_alive;
    s_snapshot.android_link_known = false;
    s_snapshot.android_link_alive = false;
    s_snapshot.last_sequence = s_session.last_sequence;
    s_snapshot.motor_a = s_motor.motor_a;
    s_snapshot.motor_b = s_motor.motor_b;
    /* step_running 是 motor_a 的布尔折叠：两者必须同源，不能各填各的。 */
    s_snapshot.step_running = (s_motor.motor_a != BF_MOVE_STOP);
    s_snapshot.servo_deg = s_servo.commanded_deg;
    s_snapshot.attitude = s_jy61p.attitude;
    s_snapshot.active_faults = s_faults;
    (void)snprintf(s_snapshot.sta_ip, sizeof(s_snapshot.sta_ip), "%s", EspLink_StaIp());
}

static void ServiceImu(uint32_t now_ms)
{
    Jy61pResult result = Jy61p_Service(&s_jy61p, now_ms);
    if (result == JY61P_RESULT_SAMPLE_OK) {
        ClearFault(BF_FAULT_IMU_DATA_TIMEOUT);
        ClearFault(BF_FAULT_IMU_I2C_RECOVERY_FAILED);
    } else if (result == JY61P_RESULT_RECOVERY_FAILED) {
        SetFault(BF_FAULT_IMU_I2C_RECOVERY_FAILED);
    }
    if (Jy61p_IsStale(&s_jy61p, now_ms, CFG_JY61P_SAMPLE_PERIOD_MS *
                                           CFG_JY61P_MAX_CONSECUTIVE_FAILURES * 2UL)) {
        Jy61p_Invalidate(&s_jy61p);
        SetFault(BF_FAULT_IMU_DATA_TIMEOUT);
    }
}

static void AppendDdeg(char *out, size_t capacity, int16_t ddeg)
{
    long value = (long)ddeg;
    unsigned long magnitude = (value < 0L) ? (unsigned long)(-value) : (unsigned long)value;
    (void)snprintf(out, capacity, "%s%lu.%01lu", (value < 0L) ? "-" : "",
                   magnitude / 10UL, magnitude % 10UL);
}

static void FormatRawHex(const uint8_t *bytes, size_t count, char *out, size_t capacity)
{
    static const char kHex[] = "0123456789ABCDEF";
    size_t index;

    if ((out == 0) || (capacity == 0U) || (bytes == 0) || ((count * 2U) + 1U > capacity)) {
        if ((out != 0) && (capacity > 0U)) {
            out[0] = '\0';
        }
        return;
    }
    for (index = 0U; index < count; index++) {
        out[index * 2U] = kHex[bytes[index] >> 4U];
        out[(index * 2U) + 1U] = kHex[bytes[index] & 0x0FU];
    }
    out[count * 2U] = '\0';
}

/* UART3 周期诊断。只读已有状态，不额外发起 I2C 事务，避免干扰姿态采样节拍。
 * 关键判据是 idle：开漏输出模式下 STM32 无法启用内部上拉，若 PB6/PB7 没有外接
 * 上拉（或模块未供电/接线错），两条线会一直读回低电平，idle=0。 */
static void ServiceImuDiagnostics(uint32_t now_ms)
{
    char line[160];
    char raw[16];
    char roll[10];
    char pitch[10];
    char yaw[10];
    bool scl_high = false;
    bool sda_high = false;
    bool pullup_scl = false;
    bool pullup_sda = false;
    char pullup[4];
    const char *pullup_text = "--";
    int n;

    if (CFG_IMU_DIAG_PERIOD_MS == 0UL) {
        return;
    }
    if ((int32_t)(now_ms - s_next_imu_diag_ms) < 0) {
        return;
    }
    s_next_imu_diag_ms = now_ms + CFG_IMU_DIAG_PERIOD_MS;
    if (BSP_Uart_DebugTxFree() < sizeof(line)) {
        return;
    }

    if (s_jy61p.last_read_ok) {
        FormatRawHex(s_jy61p.last_raw, sizeof(s_jy61p.last_raw), raw, sizeof(raw));
    } else {
        (void)strcpy(raw, "--");
    }
    if (s_jy61p.attitude.valid) {
        AppendDdeg(roll, sizeof(roll), s_jy61p.attitude.roll_ddeg);
        AppendDdeg(pitch, sizeof(pitch), s_jy61p.attitude.pitch_ddeg);
        AppendDdeg(yaw, sizeof(yaw), s_jy61p.attitude.yaw_ddeg);
    } else {
        (void)strcpy(roll, "NA");
        (void)strcpy(pitch, "NA");
        (void)strcpy(yaw, "NA");
    }

    SoftI2c_LineLevels(&s_jy61p_bus, &scl_high, &sda_high);
    if (!scl_high || !sda_high) {
        /* 只在总线不空闲时才做内部上拉探测：正常工作的总线不会被改引脚模式。
         * pu=11 说明线路上没人拉低，是外部上拉缺失；pu 含 0 说明有器件/短路在拉低。 */
        SoftI2c_IdleLevelsWithInternalPullup(&s_jy61p_bus, &pullup_scl, &pullup_sda);
        (void)snprintf(pullup, sizeof(pullup), "%u%u",
                       pullup_scl ? 1U : 0U, pullup_sda ? 1U : 0U);
        pullup_text = pullup;
    }

    n = snprintf(line, sizeof(line),
                 "IMU scl=%u sda=%u pu=%s st=%s probe=%u fail=%u n=%lu e=%lu raw=%s R=%s P=%s Y=%s\r\n",
                 scl_high ? 1U : 0U, sda_high ? 1U : 0U, pullup_text,
                 SoftI2c_StatusName(s_jy61p.last_status),
                 s_jy61p.probed ? 1U : 0U,
                 (unsigned int)s_jy61p.consecutive_failures,
                 (unsigned long)s_jy61p.attempts,
                 (unsigned long)s_jy61p.errors,
                 raw, roll, pitch, yaw);
    if (n > 0) {
        size_t length = ((size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1U;
        (void)BSP_Uart_SendDebug(line, length);
    }
}

/* JY61P 总线（PB6/PB7）独立扫描：OLED 总线扫描不能证明这颗传感器是否应答。
 * 未找到时按 CFG_OLED_RETRY_MS 重来一轮，便于现场改线后无需重新上电。 */
static void ServiceImuScan(uint32_t now_ms)
{
    char line[24];
    int n;

    /* OLED 扫描仍在推进时让出这个槽位，保证每轮主循环最多只有一次 I2C 探测。 */
    if (s_scan_address < 0x78U) {
        return;
    }
    if (s_imu_scan_done || (s_motor.motor_a != BF_MOVE_STOP) ||
        (int32_t)(now_ms - s_next_imu_scan_ms) < 0 ||
        BSP_Uart_DebugTxFree() < sizeof(line)) {
        return;
    }
    if (s_imu_scan_address >= 0x78U) {
        if (s_imu_scan_found) {
            s_imu_scan_done = true;
        } else {
            static const char kNone[] = "IMUSCAN none\r\n";
            (void)BSP_Uart_SendDebug(kNone, sizeof(kNone) - 1U);
            s_imu_scan_address = 0x08U;
            s_next_imu_scan_ms = now_ms + CFG_OLED_RETRY_MS;
        }
        return;
    }
    if (SoftI2c_Probe(&s_jy61p_bus, s_imu_scan_address) == SOFT_I2C_OK) {
        n = snprintf(line, sizeof(line), "IMUSCAN 0x%02X\r\n", (unsigned int)s_imu_scan_address);
        if (n > 0) {
            (void)BSP_Uart_SendDebug(line, (size_t)n);
        }
        s_imu_scan_found = true;
    }
    s_imu_scan_address++;
    /* 逐步探测之间留间隔，避免一次扫描长时间占住主循环。 */
    s_next_imu_scan_ms = now_ms + 5UL;
}

/* 本地动作（调试按键 / 网页端点）写入调试口，便于事后核对"谁在什么时候动了执行器"。
 * 队列满时放弃，不能因为一条日志影响控制时序。此函数与 CFG_DEBUG_BUTTONS_ENABLED 无关，
 * 因为网页端点也要用它。 */
static void NoteDebugAction(const char *text)
{
    char line[48];
    int n = snprintf(line, sizeof(line), "DEBUG %s\r\n", text);
    if (n > 0) {
        (void)BSP_Uart_SendDebug(line, (size_t)n);
    }
}

#if (CFG_DEBUG_BUTTONS_ENABLED != 0U)

/* ---- 调试模式：板载按键 ------------------------------------------------------
 * K1(PB0) 切换 M1 启停，K2(PB1) 舵机左转 CFG_SERVO_SAFE_MIN_DEG（-15°），
 * K3(PA8) 舵机右转 CFG_SERVO_SAFE_MAX_DEG（+15°）。
 * 三个键低电平有效、内部上拉，所以必须按"按下沿"触发一次动作，不能按电平持续执行。
 * 这些动作不经过协议，因此不占用序号窗口、也不伪造远端命令；上位机任何被接受的
 * 命令仍会立即覆盖本地动作（见 ApplyAcceptedCommand）。 */

#define CFG_DEBUG_BUTTON_COUNT 3U

typedef struct {
    bool stable;        /* 去抖后的稳定状态：true = 按下 */
    bool sample;        /* 上一次原始采样 */
    uint32_t sample_ms; /* 原始采样最后一次变化的时间 */
} DebugButton;

static DebugButton s_buttons[CFG_DEBUG_BUTTON_COUNT];

/* 原始电平一变就重新计时，只有稳定超过 CFG_BUTTON_DEBOUNCE_MS 才更新稳定状态；
 * 仅在"未按下 → 按下"这一跳返回 true。超时用无符号减法判断，天然处理毫秒回绕。 */
static bool ButtonPressEdge(DebugButton *button, bool raw, uint32_t now_ms)
{
    if (raw != button->sample) {
        button->sample = raw;
        button->sample_ms = now_ms;
        return false;
    }
    if ((raw != button->stable) &&
        ((uint32_t)(now_ms - button->sample_ms) >= CFG_BUTTON_DEBOUNCE_MS)) {
        button->stable = raw;
        return raw;
    }
    return false;
}

static void DebugButtonsInit(uint32_t now_ms)
{
    uint8_t index;
    for (index = 0U; index < CFG_DEBUG_BUTTON_COUNT; index++) {
        /* 以真实电平作为初值：上电时就按住某个键不应被当成一次"按下"。 */
        s_buttons[index].sample = BSP_ButtonPressed((uint8_t)(index + 1U));
        s_buttons[index].stable = s_buttons[index].sample;
        s_buttons[index].sample_ms = now_ms;
    }
}

static void ServiceDebugButtons(uint32_t now_ms)
{
    if (ButtonPressEdge(&s_buttons[0], BSP_ButtonPressed(1U), now_ms)) {
        /* 日志必须记录**调用之后**的真实方向，而不是本次按键的意图：
         * M1 被编译期禁用时 Motor_SetRunning 只会 coast，此时不能打印 ON。 */
        bool was_running = (s_motor.motor_a != BF_MOVE_STOP);
        Motor_SetRunning(&s_motor, !was_running);
        NoteDebugAction((s_motor.motor_a != BF_MOVE_STOP) ? "K1 M1 ON" : "K1 M1 OFF");
    }
    if (ButtonPressEdge(&s_buttons[1], BSP_ButtonPressed(2U), now_ms)) {
        Servo_SetAngle(&s_servo, (int8_t)CFG_SERVO_SAFE_MIN_DEG);
        NoteDebugAction("K2 servo LEFT");
    }
    if (ButtonPressEdge(&s_buttons[2], BSP_ButtonPressed(3U), now_ms)) {
        Servo_SetAngle(&s_servo, (int8_t)CFG_SERVO_SAFE_MAX_DEG);
        NoteDebugAction("K3 servo RIGHT");
    }
}

#else /* CFG_DEBUG_BUTTONS_ENABLED == 0 */

static void DebugButtonsInit(uint32_t now_ms)
{
    (void)now_ms;
}

static void ServiceDebugButtons(uint32_t now_ms)
{
    (void)now_ms;
}

#endif /* CFG_DEBUG_BUTTONS_ENABLED */

static void ServiceProtocolFrameTimeout(uint32_t now_ms)
{
    bool was_discarding;

    if ((s_parser.state == PROTO_WAIT_START) ||
        ((uint32_t)(now_ms - s_protocol_last_byte_ms) < CFG_PROTOCOL_FRAME_TIMEOUT_MS)) {
        return;
    }
    /* 超长帧进入 DISCARD 时已经回应 E_TOO_LONG；静默超时只做复位，避免重复报错。 */
    was_discarding = (s_parser.state == PROTO_DISCARD);
    Protocol_Reset(&s_parser);
    s_protocol_last_byte_ms = now_ms;
    if (!was_discarding) {
        ReplyError(false, 0U, PROTO_ERR_FRAME_TIMEOUT);
    }
}

/* ---- 网页端（与 App 协议共用 9000 端口） ----------------------------------
 * 分流只看客户端的第一个字节：'G' → HTTP，其余 → 原协议。这样浏览器和 App 能
 * 同时使用 ESP 唯一那个 CIPSERVER 端口，而协议路径一行都不用改。
 * 网页解析出的动作**不直接驱动执行器**：合成一条协议命令后走 RouteCommand，
 * 与远端 CMD 共享校验、序号窗口与失效保护，因此"1 秒无新命令自动停机"对网页同样成立。 */
static HttpRequest s_http;
static bool s_http_mode;
static bool s_http_client_seen;
static uint32_t s_http_generation;

/* 只在"确实换了一个 TCP 客户端"时清解析状态。
 * 只比较 IsClientConnected() 不够：槽位会被复用，如果旧连接关闭与新连接建立
 * 发生在两次主循环之间，新客户端会继承旧的 HTTP/协议模式（已实测踩到）。 */
static bool HttpConnectionChanged(void)
{
    uint32_t generation = EspLink_ClientGeneration();
    if (!EspLink_IsClientConnected() || (generation != s_http_generation)) {
        s_http_generation = generation;
        return true;
    }
    return false;
}

static BfRemoteCommand BuildWebCommand(HttpAction action, const HttpRequest *request)
{
    BfRemoteCommand command;
    int servo_deg = request->servo_deg;
    memset(&command, 0, sizeof(command));
    /* 序号必须落在固件窗口内：last_sequence + 1 永远合法，无需本地计数器。 */
    command.sequence = (uint16_t)(s_session.last_sequence + 1U);
    command.move = BF_MOVE_STOP;
    command.m2 = BF_MOVE_STOP;
    /* step_rpm 现在承载「占空比挡位」：100=满速挡、60=低速挡。
     * 缺省（老页面/老脚本不发 speed=）等价于满速挡，与旧行为一致。 */
    command.step_rpm = (request->step_speed == 60U) ? 60U : 100U;
    /* 舵角对**所有**动作都生效，这样 /cmd?move=F&servo=-20 能一条命令表达"前进+左舵"。 */
    command.servo_deg = (int8_t)servo_deg;
    command.turn = (servo_deg < 0) ? BF_TURN_LEFT
                                   : ((servo_deg > 0) ? BF_TURN_RIGHT : BF_TURN_CENTER);
    switch (action) {
    case HTTP_ACTION_FORWARD:
        command.move = BF_MOVE_FORWARD;
        break;
    case HTTP_ACTION_REVERSE:
        command.move = BF_MOVE_REVERSE;
        break;
    case HTTP_ACTION_STOP:
    case HTTP_ACTION_SERVO:
    case HTTP_ACTION_NONE:
    default:
        break;
    }
    if (request->m2_present && (request->m2_dir > 0)) {
        command.m2 = BF_MOVE_FORWARD;
    } else if (request->m2_present && (request->m2_dir < 0)) {
        command.m2 = BF_MOVE_REVERSE;
    }
    return command;
}

/* 处理一个已收齐的 HTTP 请求：先落地动作，再回响应。 */
static void ServiceHttpRequest(uint32_t now_ms)
{
    static char response[HTTP_RESPONSE_BYTES];
    HttpAction action = HTTP_ACTION_NONE;
    BfRemoteCommand command;
    ProtocolError error;
    bool duplicate = false;
    size_t length;

    length = HttpUi_BuildResponse(&s_http, &s_snapshot, EspLink_StaIp(), CFG_ESP_AP_IP,
                                  response, sizeof(response), &action);
    if (length == 0U) {
        return; /* 缓冲不足就不发，浏览器会超时重试 */
    }
    if (action != HTTP_ACTION_NONE) {
        command = BuildWebCommand(action, &s_http);
        error = RouteCommand(&command, now_ms, &duplicate);
        if (error == PROTO_ERR_NONE) {
            NoteDebugAction((action == HTTP_ACTION_FORWARD) ? "WEB forward"
                            : (action == HTTP_ACTION_STOP) ? "WEB stop"
                            : (action == HTTP_ACTION_REVERSE) ? "WEB reverse"
                                                              : "WEB servo");
        } else {
            NoteDebugAction("WEB rejected");
        }
        /* 动作已落地，刷新快照后**重新渲染一次**，让这次响应就带上新状态。
         * 否则浏览器点完看到的还是上一轮的值，用户会以为没生效。
         * （第二次调用会重新解析请求并把 action/servo_deg 再算一遍，结果一致。） */
        RefreshSnapshot();
        length = HttpUi_BuildResponse(&s_http, &s_snapshot, EspLink_StaIp(), CFG_ESP_AP_IP,
                                      response, sizeof(response), &action);
        if (length == 0U) {
            return;
        }
    }
    (void)EspLink_SendRaw(response, length);
}

static void ServiceProtocol(uint32_t now_ms)
{
    uint8_t byte;
    uint8_t budget = 64U;

    /* 换了客户端就必须丢掉上一条连接的解析状态。 */
    if (HttpConnectionChanged()) {
        s_http_client_seen = false;
        s_http_mode = false;
        HttpRequest_Reset(&s_http);
    }

    /* 必须在消费新字节前判定超时：迟到的续传字符不能拼接到旧半帧。 */
    ServiceProtocolFrameTimeout(now_ms);

    while ((budget-- != 0U) && EspLink_Poll(&byte)) {
        if (!s_http_client_seen) {
            /* 第一个字节决定这个客户端走哪条路。 */
            s_http_client_seen = true;
            s_http_mode = (byte == (uint8_t)'G');
            HttpRequest_Reset(&s_http);
            if (s_http_mode) {
                Protocol_Reset(&s_parser); /* HTTP 客户端不参与组帧，清掉可能的半帧 */
            }
        }
        if (s_http_mode) {
            if (HttpRequest_Feed(&s_http, byte)) {
                ServiceHttpRequest(now_ms);
                /* keep-alive：同一连接上继续接收下一个请求（浏览器轮询就靠这个，
                 * 否则每 400 ms 重建一次 TCP 连接会和 ESP 的单客户端槽位打架）。 */
                HttpRequest_Reset(&s_http);
            }
            s_protocol_last_byte_ms = now_ms;
            continue;
        }
        {
            ProtocolEvent event = Protocol_Feed(&s_parser, byte);
            if (s_parser.state != PROTO_WAIT_START) {
                s_protocol_last_byte_ms = now_ms;
            }
            HandleProtocolEvent(event, now_ms);
        }
    }

    /* 若无线在本轮未再送来字节，半帧也不能永久占住状态机。 */
    ServiceProtocolFrameTimeout(now_ms);
}

static void ServiceStatus(uint32_t now_ms)
{
    char frame[160];
    size_t length;
    if ((int32_t)(now_ms - s_next_status_ms) < 0) {
        return;
    }
    s_next_status_ms = now_ms + CFG_STATUS_PERIOD_MS;
    RefreshSnapshot();
    length = Protocol_EncodeStatus(frame, sizeof(frame), &s_snapshot);
    if (length == 0U) {
        return;
    }

    /* 周期 STA 是低优先级：预留空间给 ACK/ERR，串口堵塞不能拖慢控制。
     * 只在"已确认是协议客户端"时才发：浏览器连接后先收到一串 <STA,…> 会让它
     * 在 HTTP 响应之前读到非法数据而解析失败。客户端在收到第一个字节后立刻分类
     * （'G' 或 '<'），所以协议客户端最多只损失连接后的头几十毫秒。 */
    if (EspLink_IsClientConnected() && s_http_client_seen && !s_http_mode) {
        if (!EspLink_SendStatus(frame, length)) {
            SetFault(BF_FAULT_UART_TX_DROPPED);
        }
    }
    if (BSP_Uart_DebugTxFree() >= (uint16_t)(length + 96U)) {
        if (!BSP_Uart_SendDebug(frame, length)) {
            SetFault(BF_FAULT_UART_TX_DROPPED);
        }
    }
}

/* 每次只探测一个地址。正常刷屏仍使用驱动的分块服务，不改变页面布局。 */
static void ServiceOled(uint32_t now_ms)
{
    if (!s_display.initialized) {
        SetFault(BF_FAULT_OLED_I2C);
        /* 整组面板配置留到 M1 停止时再发，避免在电机运行期间长时间占用主循环。 */
        if ((s_motor.motor_a != BF_MOVE_STOP) || (int32_t)(now_ms - s_next_oled_retry_ms) < 0) {
            return;
        }
        s_next_oled_retry_ms = now_ms + CFG_OLED_RETRY_MS;
        if (SoftI2c_BusRecover(&s_oled_bus) == SOFT_I2C_OK &&
            Ssd1306_Init(&s_display, &s_oled_bus,
                (uint8_t)(CFG_OLED_ADDR_7BIT ^ s_oled_address_index))) {
            OledUi_Init(&s_ui, &s_display, now_ms);
        } else {
            s_oled_address_index ^= 1U;
        }
        return;
    }
    OledUi_Update(&s_ui, now_ms, &s_snapshot);
    OledUi_Service(&s_ui);
    if (Ssd1306_I2cErrorActive(&s_display)) {
        SetFault(BF_FAULT_OLED_I2C);
    } else {
        ClearFault(BF_FAULT_OLED_I2C);
    }
}

/* 启动扫描每轮只探测一个地址，且只在步进停止、OLED无在途刷新时执行。 */
static void ServiceI2cScan(void)
{
    char line[24];
    if (s_scan_address >= 0x78U || (s_motor.motor_a != BF_MOVE_STOP) ||
        !Ssd1306_IsIdle(&s_display) ||
        BSP_Uart_DebugTxFree() < sizeof(line)) {
        return;
    }
    if (SoftI2c_Probe(&s_oled_bus, s_scan_address) == SOFT_I2C_OK) {
        int n = snprintf(line, sizeof(line), "I2CSCAN 0x%02X\r\n", (unsigned int)s_scan_address);
        if (n > 0) {
            (void)BSP_Uart_SendDebug(line, (size_t)n);
        }
        s_scan_found = true;
    }
    if (++s_scan_address >= 0x78U) {
        if (!s_scan_found) {
            static const char kNone[] = "I2CSCAN none\r\n";
            (void)BSP_Uart_SendDebug(kNone, sizeof(kNone) - 1U);
        }
    }
}

static void DisconnectControl(uint32_t now_ms)
{
    Protocol_Reset(&s_parser);
    s_protocol_last_byte_ms = now_ms;
    Control_ForceDisconnect(&s_session);
    Motor_ApplyFailsafe(&s_motor);
#if (CFG_SERVO_CENTER_ON_LINK_TIMEOUT != 0U)
    Servo_Center(&s_servo);
#endif
    SetFault(BF_FAULT_LINK_TIMEOUT);
}

static void ServiceLinkEvents(uint32_t now_ms)
{
    uint32_t events = EspLink_TakeEvents();
    if (events == 0U) {
        return;
    }
    /* 链路层已清原始UART、IPD和待发队列；应用必须先撤销动作和序号窗口。 */
    DisconnectControl(now_ms);
    if ((events & ESP_LINK_EVENT_TX_FAILED) != 0U) {
        SetFault(BF_FAULT_UART_TX_DROPPED);
    }
    if ((events & ESP_LINK_EVENT_RX_LOST) != 0U) {
        SetFault(BF_FAULT_ESP_RX_LOST);
        ReplyError(false, 0U, PROTO_ERR_RX_OVERFLOW);
    }
}

static void ServiceLinkDiagnostics(uint32_t now_ms)
{
    bool ready = EspLink_IsServerReady();
    bool connected = EspLink_IsClientConnected();
    EspLinkSendCounters send;
    char line[256];
    int n;
    if (ready == s_last_server_ready && connected == s_last_client_connected &&
        (int32_t)(now_ms - s_next_link_diagnostic_ms) < 0) {
        return;
    }
    if (BSP_Uart_DebugTxFree() < sizeof(line)) {
        return;
    }
    s_last_server_ready = ready;
    s_last_client_connected = connected;
    s_next_link_diagnostic_ms = now_ms + 5000UL;
    /* id/uloss/ev/cmd 是握手失败的定位关键：uloss 增长说明 USART2 真的丢了字节，
     * 而丢字节会立刻断链并清掉 tcp，ACK 就永远回不到手机；cmd 增长则说明命令已经
     * 被接受，问题在下行 ACK 而不是上行解析。
     * etx/stx 是 CIPSEND 计数器（ok/try，分开统计 ACK/ERR 与周期 STA）：它们回答
     * "ESP 到底有没有接受并发出那条 STA"，把固件/ESP 侧与 ESP 之后的链路分开。 */
    EspLink_GetSendCounters(&send);
    n = snprintf(line, sizeof(line),
        "ESP server=%u tcp=%u id=%d sta=%s reset=%u ev=%lu uloss=%lu cmd=%lu "
        "etx=%lu/%lu stx=%lu/%lu sf=%lu st=%lu error=%s\r\n",
        ready ? 1U : 0U, connected ? 1U : 0U,
        (int)EspLink_ClientId(),
        EspLink_StaIp(),
        EspLink_ResetNeeded() ? 1U : 0U,
        (unsigned long)EspLink_PendingEvents(),
        (unsigned long)BSP_Uart_EspRxLossCount(),
        (unsigned long)s_accepted_commands,
        (unsigned long)send.event_ok, (unsigned long)send.event_try,
        (unsigned long)send.status_ok, (unsigned long)send.status_try,
        (unsigned long)send.send_fail, (unsigned long)send.send_timeout,
        EspLink_LastError());
    if (n > 0) {
        size_t length = ((size_t)n < sizeof(line)) ? (size_t)n : sizeof(line) - 1U;
        (void)BSP_Uart_SendDebug(line, length);
    }
}

void App_Init(void)
{
    uint32_t now_ms = BSP_Millis();

    memset(&s_snapshot, 0, sizeof(s_snapshot));
    memset(&s_display, 0, sizeof(s_display));
    s_faults = 0U;
    ControlSession_Init(&s_session);
    Protocol_Init(&s_parser);
    BSP_Board_Init();
    Motor_Init(&s_motor);
    Servo_Init(&s_servo);
    if (!RuntimeTimerConfigurationMatches()) {
        SetFault(BF_FAULT_CONFIGURATION);
        Motor_ApplyFailsafe(&s_motor);
        Servo_Center(&s_servo);
    }
    BSP_Uart_Init();
    EspLink_Init();

    SoftI2c_Init(&s_jy61p_bus, JY61P_SCL_GPIO_Port, JY61P_SCL_Pin,
                 JY61P_SDA_GPIO_Port, JY61P_SDA_Pin, CFG_SOFT_I2C_HALF_PERIOD_US);
    SoftI2c_Init(&s_oled_bus, OLED_SCL_GPIO_Port, OLED_SCL_Pin,
                 OLED_SDA_GPIO_Port, OLED_SDA_Pin, CFG_SOFT_I2C_HALF_PERIOD_US);
    /* 只在 JY61P 总线上启用台架兼容；OLED 总线有板载上拉，保持标准开漏时序。 */
    SoftI2c_SetInternalPullup(&s_jy61p_bus, CFG_JY61P_INTERNAL_PULLUP != 0U);

    /* 上电空闲电平：两条软件 I2C 都必须在无人驱动时读回高。任一为 0 就说明该
     * 总线缺上拉、被拉死或引脚配置异常，后续所有事务都必然失败——先于任何
     * 事务把它报出来，省去把“线没上拉”误判成“器件地址不对”。 */
    {
        char line[80];
        bool oled_scl = false;
        bool oled_sda = false;
        bool imu_scl = false;
        bool imu_sda = false;
        int n;

        SoftI2c_LineLevels(&s_oled_bus, &oled_scl, &oled_sda);
        SoftI2c_LineLevels(&s_jy61p_bus, &imu_scl, &imu_sda);
        n = snprintf(line, sizeof(line),
                     "I2CBUS oled scl=%u sda=%u imu scl=%u sda=%u ipu=%u\r\n",
                     oled_scl ? 1U : 0U, oled_sda ? 1U : 0U,
                     imu_scl ? 1U : 0U, imu_sda ? 1U : 0U,
                     CFG_JY61P_INTERNAL_PULLUP != 0U ? 1U : 0U);
        if (n > 0) {
            (void)BSP_Uart_SendDebug(line, (size_t)n);
        }
    }

    if (!Jy61p_Init(&s_jy61p, &s_jy61p_bus, now_ms)) {
        SetFault(BF_FAULT_IMU_DATA_TIMEOUT);
    }
    s_oled_address_index = 0U;
    s_next_oled_retry_ms = now_ms + CFG_OLED_BOOT_DELAY_MS;
    s_scan_address = 0x08U;
    s_scan_found = false;
    s_last_server_ready = false;
    s_last_client_connected = false;
    s_next_link_diagnostic_ms = now_ms;
    s_imu_scan_address = 0x08U;
    s_imu_scan_found = false;
    s_imu_scan_done = false;
    s_next_imu_scan_ms = now_ms;
    s_next_imu_diag_ms = now_ms + CFG_IMU_DIAG_PERIOD_MS;
    SetFault(BF_FAULT_OLED_I2C);
    OledUi_Init(&s_ui, &s_display, now_ms);

#if (CFG_MOTOR_A_ENABLED == 0U)
    SetFault(BF_FAULT_STEPPER_DISABLED);
#endif
    DebugButtonsInit(now_ms);
    /* 防御性复位：这些是跨连接状态，不能靠"上一次连接结束时刚好清过"。 */
    s_http_client_seen = false;
    s_http_mode = false;
    s_http_generation = EspLink_ClientGeneration();
    HttpRequest_Reset(&s_http);
    s_next_status_ms = now_ms + CFG_STATUS_PERIOD_MS;
    s_next_led_ms = now_ms + 500UL;
    s_protocol_last_byte_ms = now_ms;
    RefreshSnapshot();
}

void App_Process(void)
{
    uint32_t now_ms = BSP_Millis();

    EspLink_Service();
    BSP_Uart_Service();
    ServiceLinkEvents(now_ms);
    ServiceProtocol(now_ms);
    /* ACK 入队失败也可能在解析过程中触发断链，必须先于本轮任何执行器动作处理。 */
    ServiceLinkEvents(now_ms);
    if (Control_CheckTimeout(&s_session, now_ms)) {
        DisconnectControl(now_ms);
        EspLink_CloseClient();
    }
    /* 调试按键放在链路超时处理之后：本轮的按键动作不会被同一轮的失效保护立刻撤销
     * （否则"上位机刚失联时按键无效"会难以理解）。 */
    ServiceDebugButtons(now_ms);

    ServiceImu(now_ms);
    ServiceImuDiagnostics(now_ms);
    RefreshSnapshot();
    ServiceOled(now_ms);
    ServiceI2cScan();
    ServiceImuScan(now_ms);
    ServiceLinkDiagnostics(now_ms);
    ServiceStatus(now_ms);
    BSP_Uart_Service();

    if ((int32_t)(now_ms - s_next_led_ms) >= 0) {
        BSP_RunLed_Toggle();
        /* 调试判据：OLED 初始化失败(认不到屏)→快速闪烁；成功→正常慢闪。 */
        s_next_led_ms = now_ms + (s_display.initialized ? 500UL : 120UL);
    }
}
