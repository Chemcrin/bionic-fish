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
    Motor_ApplyCommand(&s_motor, command, BSP_Micros());
    Servo_SetAngle(&s_servo, command->servo_deg);
    ClearFault(BF_FAULT_LINK_TIMEOUT);
    ClearFault(BF_FAULT_ESP_RX_LOST);
}

static void HandleProtocolEvent(ProtocolEvent event, uint32_t now_ms)
{
    ProtocolError error;
    ControlDecision decision;

    if (event.kind == PROTO_EVENT_ERROR) {
        ReplyError(event.sequence_present, event.sequence, event.error);
        return;
    }
    if (event.kind != PROTO_EVENT_COMMAND) {
        return;
    }

    error = Control_ValidateCommand(&event.command);
    if (error != PROTO_ERR_NONE) {
        ReplyError(true, event.command.sequence, error);
        return;
    }
    if ((s_faults & (uint32_t)BF_FAULT_CONFIGURATION) != 0U) {
        ReplyError(true, event.command.sequence, PROTO_ERR_CONFIGURATION);
        return;
    }
    decision = Control_ClassifyCommand(&s_session, &event.command, &error);
    if (decision == CONTROL_REJECTED) {
        ReplyError(true, event.command.sequence, error);
        return;
    }
    if (decision == CONTROL_DUPLICATE_COMMAND) {
        Control_AcceptDuplicate(&s_session, now_ms);
        ReplyAck(event.command.sequence, true);
        return;
    }

    Control_AcceptNew(&s_session, &event.command, now_ms);
    ApplyAcceptedCommand(&event.command);
    ReplyAck(event.command.sequence, false);
}

static void RefreshSnapshot(void)
{
    s_snapshot.command_link_alive = s_session.link_alive;
    s_snapshot.android_link_known = false;
    s_snapshot.android_link_alive = false;
    s_snapshot.last_sequence = s_session.last_sequence;
    s_snapshot.step_running = s_motor.step_running;
    s_snapshot.servo_deg = s_servo.commanded_deg;
    s_snapshot.attitude = s_jy61p.attitude;
    s_snapshot.active_faults = s_faults;
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

static void ServiceProtocol(uint32_t now_ms)
{
    uint8_t byte;
    uint8_t budget = 64U;

    /* 必须在消费新字节前判定超时：迟到的续传字符不能拼接到旧半帧。 */
    ServiceProtocolFrameTimeout(now_ms);

    while ((budget-- != 0U) && EspLink_Poll(&byte)) {
        ProtocolEvent event = Protocol_Feed(&s_parser, byte);
        if (s_parser.state != PROTO_WAIT_START) {
            s_protocol_last_byte_ms = now_ms;
        }
        HandleProtocolEvent(event, now_ms);
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

    /* 周期 STA 是低优先级：预留空间给 ACK/ERR，串口堵塞不能拖慢控制。 */
    if (EspLink_IsClientConnected()) {
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
        /* 初始化发送整组面板配置，留到步进停止时执行，避免延长正在运行的相位。 */
        if (s_motor.step_running || (int32_t)(now_ms - s_next_oled_retry_ms) < 0) {
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
    if (s_scan_address >= 0x78U || s_motor.step_running || !Ssd1306_IsIdle(&s_display) ||
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
    char line[128];
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
    n = snprintf(line, sizeof(line), "ESP server=%u tcp=%u reset=%u error=%s\r\n",
        ready ? 1U : 0U, connected ? 1U : 0U,
        EspLink_ResetNeeded() ? 1U : 0U, EspLink_LastError());
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
    SetFault(BF_FAULT_OLED_I2C);
    OledUi_Init(&s_ui, &s_display, now_ms);

#if (CFG_STEPPER_DRIVER_ENABLED == 0U)
    SetFault(BF_FAULT_STEPPER_DISABLED);
#endif
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
    /* ACK 入队失败也可能在解析过程中触发断链，必须在本轮换相前处理。 */
    ServiceLinkEvents(now_ms);
    if (Control_CheckTimeout(&s_session, now_ms)) {
        DisconnectControl(now_ms);
        EspLink_CloseClient();
    }

    Motor_Service(&s_motor, BSP_Micros());
    ServiceImu(now_ms);
    Motor_Service(&s_motor, BSP_Micros());
    RefreshSnapshot();
    ServiceOled(now_ms);
    Motor_Service(&s_motor, BSP_Micros());
    ServiceI2cScan();
    ServiceLinkDiagnostics(now_ms);
    ServiceStatus(now_ms);
    BSP_Uart_Service();

    if ((int32_t)(now_ms - s_next_led_ms) >= 0) {
        BSP_RunLed_Toggle();
        /* 调试判据：OLED 初始化失败(认不到屏)→快速闪烁；成功→正常慢闪。 */
        s_next_led_ms = now_ms + (s_display.initialized ? 500UL : 120UL);
    }
}
