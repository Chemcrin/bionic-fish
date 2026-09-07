#include "control_arbiter.h"

#include <string.h>

#include "app_config.h"

static bool CommandsEqual(const BfRemoteCommand *a, const BfRemoteCommand *b)
{
    return (a->sequence == b->sequence) && (a->move == b->move) && (a->turn == b->turn) &&
           (a->step_rpm == b->step_rpm) && (a->servo_deg == b->servo_deg);
}

/* `turn` 是方向语义，`servo` 是精确角度。二者必须一致，避免上位机把“右转”
 * 和负舵角同时发出却仍被静默执行。 */
static bool TurnMatchesServo(BfTurn turn, int8_t servo_deg)
{
    switch (turn) {
    case BF_TURN_LEFT:
        return servo_deg < 0;
    case BF_TURN_RIGHT:
        return servo_deg > 0;
    case BF_TURN_CENTER:
        return servo_deg == 0;
    default:
        return false;
    }
}

void ControlSession_Init(ControlSession *session)
{
    if (session != 0) {
        memset(session, 0, sizeof(*session));
    }
}

ProtocolError Control_ValidateCommand(const BfRemoteCommand *command)
{
    if (command == 0) {
        return PROTO_ERR_SEMANTIC;
    }
    if ((command->step_rpm != 60U) && (command->step_rpm != 100U)) {
        return PROTO_ERR_RANGE;
    }
    if ((command->servo_deg < -30) || (command->servo_deg > 30)) {
        return PROTO_ERR_RANGE;
    }
    if (!TurnMatchesServo(command->turn, command->servo_deg)) {
        return PROTO_ERR_SEMANTIC;
    }
    if ((command->move == BF_MOVE_REVERSE) && (CFG_STEPPER_UNIDIRECTIONAL != 0U)) {
        /* 项目当前约定只允许单方向推进；不因未来换相表而悄悄放开。 */
        return PROTO_ERR_STEP_REVERSE_UNSUPPORTED;
    }
    if (((command->move == BF_MOVE_FORWARD) || (command->move == BF_MOVE_REVERSE)) &&
        ((CFG_STEPPER_DRIVER_ENABLED == 0U) ||
         (CFG_STEPPER_PARAMETERS_CONFIRMED == 0U) ||
         (CFG_STEPPER_COMMUTATIONS_PER_OUTPUT_REV == 0UL) ||
         (CFG_STEPPER_WINDING_PWM_PERCENT == 0U))) {
        return PROTO_ERR_STEPPER_HW_UNCONFIRMED;
    }
    return PROTO_ERR_NONE;
}

ControlDecision Control_ClassifyCommand(const ControlSession *session,
                                        const BfRemoteCommand *command,
                                        ProtocolError *error)
{
    uint16_t delta;

    if ((session == 0) || (command == 0) || (error == 0)) {
        return CONTROL_REJECTED;
    }
    *error = PROTO_ERR_NONE;
    if (!session->sequence_valid) {
        return CONTROL_NEW_COMMAND;
    }
    if (command->sequence == session->last_sequence) {
        if (CommandsEqual(command, &session->last_command)) {
            return CONTROL_DUPLICATE_COMMAND;
        }
        *error = PROTO_ERR_SEQ_CONFLICT;
        return CONTROL_REJECTED;
    }

    /* uint16_t 回绕比较：1..32767 为新包，0x8000 无法判定，余下为旧包。 */
    delta = (uint16_t)(command->sequence - session->last_sequence);
    if (delta == 0x8000U) {
        *error = PROTO_ERR_SEQ_AMBIGUOUS;
        return CONTROL_REJECTED;
    }
    if (delta > 0x8000U) {
        *error = PROTO_ERR_SEQ_OLD;
        return CONTROL_REJECTED;
    }
    return CONTROL_NEW_COMMAND;
}

void Control_AcceptNew(ControlSession *session, const BfRemoteCommand *command, uint32_t now_ms)
{
    uint16_t delta;

    if ((session == 0) || (command == 0)) {
        return;
    }
    if (session->sequence_valid) {
        delta = (uint16_t)(command->sequence - session->last_sequence);
        if (delta > 1U) {
            session->sequence_gap_count++;
        }
    }
    session->sequence_valid = true;
    session->link_alive = true;
    session->last_sequence = command->sequence;
    session->last_command = *command;
    session->last_valid_command_ms = now_ms;
}

void Control_AcceptDuplicate(ControlSession *session, uint32_t now_ms)
{
    if (session != 0) {
        /* 相同 seq、相同载荷幂等；可以作为重传而刷新控制看门狗。 */
        session->link_alive = true;
        session->last_valid_command_ms = now_ms;
    }
}

void Control_ForceDisconnect(ControlSession *session)
{
    if (session != 0) {
        /* 接收缓冲溢出/串口错误后，无法判断后续字节是否仍属于原会话。
         * 保留最后 seq 仅供状态显示，但清除保活和序号窗口，等待完整新帧重建。 */
        session->link_alive = false;
        session->sequence_valid = false;
    }
}

bool Control_CheckTimeout(ControlSession *session, uint32_t now_ms)
{
    if ((session != 0) && session->link_alive &&
        ((uint32_t)(now_ms - session->last_valid_command_ms) >= CFG_LINK_TIMEOUT_MS)) {
        session->link_alive = false;
        /* ESP/安卓重启可能从 0 重新计数；失联后清序号窗口允许重新建链。 */
        session->sequence_valid = false;
        return true;
    }
    return false;
}
