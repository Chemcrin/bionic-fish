#include "motor_control.h"

#include <string.h>

#include "app_config.h"
#include "bsp_board.h"

/* TB6612 的 A、B 两个 H 桥分别驱动两相双极步进电机的一组线圈。
 * 这是四拍双相通电全步表，不存在也不虚构 STEP/DIR 引脚。线圈配对或极性错误
 * 会导致抖动/堵转，必须断电后用万用表确认并通过配置或改线修正。 */
typedef struct {
    BspBridgeState bridge_a;
    BspBridgeState bridge_b;
} StepperPhase;

static const StepperPhase kBipolarFullStepTable[4] = {
    {BSP_BRIDGE_FORWARD, BSP_BRIDGE_FORWARD},
    {BSP_BRIDGE_REVERSE, BSP_BRIDGE_FORWARD},
    {BSP_BRIDGE_REVERSE, BSP_BRIDGE_REVERSE},
    {BSP_BRIDGE_FORWARD, BSP_BRIDGE_REVERSE}
};

static void StopStepper(MotorController *motor)
{
    motor->step_running = false;
    motor->step_reverse = false;
    /* A/B 两相同时释放，避免停止后持续发热。是否需要保持力必须另行评估。 */
    BSP_BridgeA_Coast();
    BSP_BridgeB_Coast();
}

static void ApplyStepperPhase(const MotorController *motor)
{
    uint8_t phase = motor->step_phase & 0x03U;
    BSP_BridgeA_Set(kBipolarFullStepTable[phase].bridge_a,
                    CFG_STEPPER_WINDING_PWM_PERCENT);
    BSP_BridgeB_Set(kBipolarFullStepTable[phase].bridge_b,
                    CFG_STEPPER_WINDING_PWM_PERCENT);
}

void Motor_Init(MotorController *motor)
{
    if (motor == 0) {
        return;
    }
    memset(motor, 0, sizeof(*motor));
    StopStepper(motor);
}

void Motor_ApplyCommand(MotorController *motor, const BfRemoteCommand *command, uint32_t now_us)
{
    bool reverse;

    if ((motor == 0) || (command == 0)) {
        return;
    }
    if ((command->move != BF_MOVE_FORWARD) && (command->move != BF_MOVE_REVERSE)) {
        StopStepper(motor);
        return;
    }
    if ((CFG_STEPPER_DRIVER_ENABLED == 0U) ||
        ((command->move == BF_MOVE_REVERSE) && (CFG_STEPPER_UNIDIRECTIONAL != 0U))) {
        /* 内部调用也遵守显式禁用和单向配置。 */
        StopStepper(motor);
        return;
    }
    reverse = (command->move == BF_MOVE_REVERSE);
    /* 新 seq 只用于协议保活；同一运动目标以及单独的舵角变化不得提前换相。 */
    if (!motor->step_running || (motor->step_reverse != reverse)) {
        motor->next_commutation_us = now_us;
    }
    motor->step_running = true;
    motor->step_reverse = reverse;
}

void Motor_ApplyFailsafe(MotorController *motor)
{
    if (motor == 0) {
        return;
    }
    StopStepper(motor);
}

void Motor_Service(MotorController *motor, uint32_t now_us)
{
    bool reverse_phase_order;

    if ((motor == 0) || !motor->step_running || (CFG_STEPPER_DRIVER_ENABLED == 0U)) {
        return;
    }

    reverse_phase_order = motor->step_reverse ^ (CFG_STEPPER_PHASE_REVERSED != 0U);
    if ((int32_t)(now_us - motor->next_commutation_us) >= 0) {
        ApplyStepperPhase(motor);
        if (reverse_phase_order) {
            motor->step_phase = (uint8_t)((motor->step_phase - 1U) & 0x03U);
        } else {
            motor->step_phase = (uint8_t)((motor->step_phase + 1U) & 0x03U);
        }
        /* 主循环若偶发迟到，只执行一次具有完整驻留时间的换相。连续补四拍会在
         * 同一次循环内瞬间跨相，线圈来不及建立电流且更容易失步。 */
        motor->next_commutation_us = now_us + CFG_STEPPER_COMMUTATION_PERIOD_US;
    }
}
