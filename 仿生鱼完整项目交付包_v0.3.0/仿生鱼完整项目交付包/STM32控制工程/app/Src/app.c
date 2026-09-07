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
static bool s_oled_ok;

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
    bool esp_ok = EspLink_Send(message, length);
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
    s_snapshot.step_target_rpm = s_motor.step_target_rpm;
    s_snapshot.step_commanded_rpm = s_motor.step_commanded_rpm;
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

    if (EspLink_RxOverflow()) {
        /* 溢出后，残缺字节绝不可继续拼成控制帧。 */
        while (EspLink_Poll(&byte)) {
            /* discard */
        }
        Protocol_Reset(&s_parser);
        s_protocol_last_byte_ms = now_ms;
        EspLink_ClearRxOverflow();
        /* 字节已经丢失时，不能等到普通保活超时才停止步进；立即进入同一套
         * 失联安全状态，并允许下一完整 CMD 重新建会话。 */
        Control_ForceDisconnect(&s_session);
        Motor_ApplyFailsafe(&s_motor);
#if (CFG_SERVO_CENTER_ON_LINK_TIMEOUT != 0U)
        Servo_Center(&s_servo);
#endif
        SetFault(BF_FAULT_LINK_TIMEOUT);
        SetFault(BF_FAULT_ESP_RX_LOST);
        ReplyError(false, 0U, PROTO_ERR_RX_OVERFLOW);
        return;
    }

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
    if (EspLink_TxSpace()) {
        if (!EspLink_Send(frame, length)) {
            SetFault(BF_FAULT_UART_TX_DROPPED);
        }
    }
    if (BSP_Uart_DebugTxFree() >= (uint16_t)(length + 96U)) {
        if (!BSP_Uart_SendDebug(frame, length)) {
            SetFault(BF_FAULT_UART_TX_DROPPED);
        }
    }
}

/* SSD1306 上电后需数百 µs~数 ms 完成内部 POR；若 MCU 复位比屏就绪更快，首次
 * Probe 会 NACK。原实现只在 App_Init 初始化一次，失败便永不重试，屏会一直黑。
 * 此处多轮重试，并先从配置地址(0x3C)试起、失败再回退到相邻地址(0x3D)。 */
static bool OledBringUp(void)
{
    uint8_t addresses[2];
    uint8_t attempt;
    uint8_t index;
    uint32_t started_ms;
    bool ok = false;

    addresses[0] = CFG_OLED_ADDR_7BIT;
    addresses[1] = (uint8_t)(CFG_OLED_ADDR_7BIT ^ 0x01U); /* 0x3C<->0x3D */

    for (attempt = 0U; (attempt < 8U) && !ok; attempt++) {
        for (index = 0U; index < (uint8_t)(sizeof(addresses)); index++) {
            if (Ssd1306_Init(&s_display, &s_oled_bus, addresses[index])) {
                ok = true;
                break;
            }
        }
        if (!ok) {
            started_ms = BSP_Millis();
            while ((uint32_t)(BSP_Millis() - started_ms) < 100U) {
            }
        }
    }
    return ok;
}

/* 一次性把 OLED 所在软件 I2C(PB8/PB9)上 0x08..0x77 全部探测一遍，把有 ACK 的
 * 地址打到调试串口。若打印 I2CSCAN none，说明总线上没有设备响应 —— 指向接线/
 * 供电；若打印出 0x3C/0x3D 等，说明设备在但 init 另有原因。 */
static void I2cScanDebug(void)
{
    uint8_t addr;
    char line[24];
    bool any = false;

    for (addr = 0x08U; addr < 0x78U; addr++) {
        if (SoftI2c_Probe(&s_oled_bus, addr) == SOFT_I2C_OK) {
            int n = snprintf(line, sizeof(line), "I2CSCAN 0x%02X\r\n", (unsigned int)addr);
            if (n > 0) {
                (void)BSP_Uart_SendDebug(line, (size_t)n);
            }
            any = true;
        }
    }
    if (!any) {
        static const char kNone[] = "I2CSCAN none\r\n";
        (void)BSP_Uart_SendDebug(kNone, sizeof(kNone) - 1U);
    }
}

void App_Init(void)
{
    uint32_t now_ms = BSP_Millis();

    memset(&s_snapshot, 0, sizeof(s_snapshot));
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
    s_oled_ok = OledBringUp();
    if (!s_oled_ok) {
        SetFault(BF_FAULT_OLED_I2C);
    }
    OledUi_Init(&s_ui, &s_display, now_ms);

#if (CFG_STEPPER_DRIVER_ENABLED == 0U) || \
    (CFG_STEPPER_PARAMETERS_CONFIRMED == 0U) || \
    (CFG_STEPPER_COMMUTATIONS_PER_OUTPUT_REV == 0UL) || \
    (CFG_STEPPER_WINDING_PWM_PERCENT == 0U)
    SetFault(BF_FAULT_STEPPER_HW_UNCONFIRMED);
#endif
    s_next_status_ms = now_ms + CFG_STATUS_PERIOD_MS;
    s_next_led_ms = now_ms + 500UL;
    s_protocol_last_byte_ms = now_ms;
    RefreshSnapshot();
    I2cScanDebug();
}

void App_Process(void)
{
    uint32_t now_ms = BSP_Millis();

    EspLink_Service();
    BSP_Uart_Service();
    ServiceProtocol(now_ms);
    if (Control_CheckTimeout(&s_session, now_ms)) {
        Motor_ApplyFailsafe(&s_motor);
#if (CFG_SERVO_CENTER_ON_LINK_TIMEOUT != 0U)
        Servo_Center(&s_servo);
#endif
        SetFault(BF_FAULT_LINK_TIMEOUT);
    }

    Motor_Service(&s_motor, BSP_Micros());
    ServiceImu(now_ms);
    RefreshSnapshot();
    OledUi_Update(&s_ui, now_ms, &s_snapshot);
    OledUi_Service(&s_ui);
    if (Ssd1306_I2cErrorActive(&s_display)) {
        /* 运行期发送失败也必须进入 STA.err/UI，而不只在初始化阶段可见。 */
        SetFault(BF_FAULT_OLED_I2C);
    } else {
        ClearFault(BF_FAULT_OLED_I2C);
    }
    ServiceStatus(now_ms);

    if ((int32_t)(now_ms - s_next_led_ms) >= 0) {
        BSP_RunLed_Toggle();
        /* 调试判据：OLED 初始化失败(认不到屏)→快速闪烁；成功→正常慢闪。 */
        s_next_led_ms = now_ms + (s_oled_ok ? 500UL : 120UL);
    }
}
