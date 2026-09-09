#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "app_config.h"
#include "bsp_board.h"
#include "control_arbiter.h"
#include "motor_control.h"

static unsigned phase_a_count, phase_b_count, coast_count;
static BspBridgeState last_a, last_b;

void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty)
{
    assert(duty == 10U && duty == CFG_STEPPER_WINDING_PWM_PERCENT);
    last_a = state;
    phase_a_count++;
}
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty)
{
    assert(duty == 10U && duty == CFG_STEPPER_WINDING_PWM_PERCENT);
    last_b = state;
    phase_b_count++;
}
void BSP_BridgeA_Coast(void) { coast_count++; }
void BSP_BridgeB_Coast(void) { coast_count++; }

static BfRemoteCommand Command(uint16_t sequence, uint16_t legacy_speed)
{
    BfRemoteCommand command = {0};
    command.sequence = sequence;
    command.move = BF_MOVE_FORWARD;
    command.turn = BF_TURN_CENTER;
    command.step_rpm = legacy_speed;
    return command;
}

#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
static void TestFixedLowMotion(void)
{
    MotorController motor;
    BfRemoteCommand command = Command(1U, 60U);
    unsigned i, before;

    Motor_Init(&motor);
    assert(!motor.step_running && coast_count == 2U);
    assert(Control_ValidateCommand(&command) == PROTO_ERR_NONE);
    Motor_ApplyCommand(&motor, &command, 10000U);
    Motor_Service(&motor, 10000U);
    assert(phase_a_count == 1U && phase_b_count == 1U);
    assert(last_a == BSP_BRIDGE_FORWARD && last_b == BSP_BRIDGE_FORWARD);
    assert(motor.next_commutation_us == 60000U);

    /* New sequence, legacy speed and servo changes all keep the same deadline. */
    command.sequence++;
    command.step_rpm = 100U;
    command.turn = BF_TURN_RIGHT;
    command.servo_deg = 10;
    assert(Control_ValidateCommand(&command) == PROTO_ERR_NONE);
    Motor_ApplyCommand(&motor, &command, 12000U);
    Motor_Service(&motor, 59999U);
    assert(phase_a_count == 1U && motor.next_commutation_us == 60000U);
    Motor_Service(&motor, 60000U);
    assert(phase_a_count == 2U && phase_b_count == 2U);
    assert(last_a == BSP_BRIDGE_REVERSE && last_b == BSP_BRIDGE_FORWARD);
    command.step_rpm = 60U;
    Motor_ApplyCommand(&motor, &command, 60001U);
    assert(motor.next_commutation_us == 110000U);
    Motor_Service(&motor, 110000U);
    assert(last_a == BSP_BRIDGE_REVERSE && last_b == BSP_BRIDGE_REVERSE);

    /* A late loop takes one phase, then preserves a full fixed interval. */
    Motor_Service(&motor, 300000U);
    assert(phase_a_count == 4U && phase_b_count == 4U);
    assert(last_a == BSP_BRIDGE_FORWARD && last_b == BSP_BRIDGE_REVERSE);
    assert(motor.next_commutation_us == 350000U);
    Motor_Service(&motor, 300000U);
    assert(phase_a_count == 4U);
    for (i = 0U; i < 20U; i++) {
        uint32_t due = motor.next_commutation_us;
        before = phase_a_count;
        command.sequence++;
        command.step_rpm = (i & 1U) ? 60U : 100U;
        Motor_ApplyCommand(&motor, &command, due - 1U);
        Motor_Service(&motor, due - 1U);
        assert(phase_a_count == before && motor.next_commutation_us == due);
        Motor_Service(&motor, due);
        assert(phase_a_count == before + 1U && phase_b_count == phase_a_count);
        assert(motor.next_commutation_us == due + 50000U);
    }
    assert(motor.step_running);

    before = phase_a_count;
    command.move = BF_MOVE_STOP;
    Motor_ApplyCommand(&motor, &command, 1400001U);
    Motor_Service(&motor, 1450001U);
    assert(!motor.step_running && phase_a_count == before && coast_count == 4U);
    command.move = BF_MOVE_FORWARD;
    Motor_ApplyCommand(&motor, &command, 1500000U);
    assert(motor.step_running && motor.next_commutation_us == 1500000U);
    Motor_ApplyFailsafe(&motor);
    Motor_Service(&motor, 1600000U);
    assert(!motor.step_running && phase_a_count == before && coast_count == 6U);

    command.move = BF_MOVE_REVERSE;
    assert(Control_ValidateCommand(&command) == PROTO_ERR_STEP_REVERSE_UNSUPPORTED);
    Motor_ApplyCommand(&motor, &command, 1700000U);
    Motor_Service(&motor, 1700000U);
    assert(!motor.step_running && phase_a_count == before);
}

static void TestMicrosecondWrap(void)
{
    MotorController motor;
    BfRemoteCommand command = Command(20U, 100U);
    unsigned before = phase_a_count;
    Motor_Init(&motor);
    Motor_ApplyCommand(&motor, &command, UINT32_MAX - 1000U);
    Motor_Service(&motor, UINT32_MAX - 1000U);
    assert(motor.next_commutation_us == 48999U);
    command.sequence++;
    command.step_rpm = 60U;
    Motor_ApplyCommand(&motor, &command, UINT32_MAX);
    Motor_Service(&motor, UINT32_MAX);
    Motor_Service(&motor, 48998U);
    assert(phase_a_count == before + 1U);
    Motor_Service(&motor, 48999U);
    assert(phase_a_count == before + 2U && motor.next_commutation_us == 98999U);
    Motor_ApplyFailsafe(&motor);
}
#else
static void TestExplicitDisable(void)
{
    MotorController motor;
    BfRemoteCommand command = Command(1U, 60U);
    Motor_Init(&motor);
    assert(Control_ValidateCommand(&command) == PROTO_ERR_STEPPER_DISABLED);
    Motor_ApplyCommand(&motor, &command, 0U);
    Motor_Service(&motor, 0U);
    command.step_rpm = 100U;
    assert(Control_ValidateCommand(&command) == PROTO_ERR_STEPPER_DISABLED);
    Motor_ApplyCommand(&motor, &command, 50000U);
    Motor_Service(&motor, 50000U);
    assert(!motor.step_running && phase_a_count == 0U && phase_b_count == 0U);
    assert(coast_count >= 6U);
    command.move = BF_MOVE_STOP;
    assert(Control_ValidateCommand(&command) == PROTO_ERR_NONE);
}
#endif

int main(void)
{
    BfRemoteCommand invalid = Command(0U, 0U);
    assert(CFG_STEPPER_COMMUTATION_PERIOD_US == 50000UL);
    assert(CFG_STEPPER_WINDING_PWM_PERCENT == 10U);
    assert(Control_ValidateCommand(&invalid) == PROTO_ERR_RANGE);
#if (CFG_STEPPER_DRIVER_ENABLED != 0U)
    TestFixedLowMotion();
    TestMicrosecondWrap();
#else
    TestExplicitDisable();
#endif
    puts("motor_host_test: PASS");
    return 0;
}
