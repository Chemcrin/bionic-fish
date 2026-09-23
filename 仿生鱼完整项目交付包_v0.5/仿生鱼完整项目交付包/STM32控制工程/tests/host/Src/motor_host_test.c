#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "app_config.h"
#include "app_types.h"
#include "bsp_board.h"
#include "motor_control.h"

/* 2026-09-12：推进方案换成"两路直流减速电机"，本测试相应重写。
 * 覆盖：9 种方向组合矩阵、两路占空比各自独立、停止/非法/失联一律 coast、
 * 以及"从不产生 IN1=IN2=1 的 brake 组合"。 */

typedef struct {
    BspBridgeState state;
    uint8_t duty;
} BridgeCall;

static BridgeCall a_last, b_last;
static unsigned a_calls, b_calls, brake_calls;
static unsigned a_drive, b_drive;

/* 2026-09-23：占空比改为运行时两挡，由 BSP 持有。测试照搬真实语义。 */
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
    if (state == BSP_BRIDGE_BRAKE) brake_calls++;
    a_last.state = state;
    a_last.duty = duty;
    a_calls++;
    a_drive++;
}
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty)
{
    if (state == BSP_BRIDGE_BRAKE) brake_calls++;
    b_last.state = state;
    b_last.duty = duty;
    b_calls++;
    b_drive++;
}
void BSP_BridgeA_Coast(void)
{
    a_last.state = BSP_BRIDGE_COAST;
    a_last.duty = 0U;
    a_calls++;
}
void BSP_BridgeB_Coast(void)
{
    b_last.state = BSP_BRIDGE_COAST;
    b_last.duty = 0U;
    b_calls++;
}

/* 期望值必须把"该路被编译期禁用"算进去：禁用时无论命令什么都应 coast。 */
static BspBridgeState ExpectA(BfMove d)
{
#if (CFG_MOTOR_A_ENABLED == 0U)
    (void)d;
    return BSP_BRIDGE_COAST;
#else
    switch (d) {
    case BF_MOVE_FORWARD: return BSP_BRIDGE_FORWARD;
    case BF_MOVE_REVERSE: return BSP_BRIDGE_REVERSE;
    case BF_MOVE_STOP:
    default: return BSP_BRIDGE_COAST;
    }
#endif
}
static BspBridgeState ExpectB(BfMove d)
{
#if (CFG_MOTOR_B_ENABLED == 0U)
    (void)d;
    return BSP_BRIDGE_COAST;
#else
    switch (d) {
    case BF_MOVE_FORWARD: return BSP_BRIDGE_FORWARD;
    case BF_MOVE_REVERSE: return BSP_BRIDGE_REVERSE;
    case BF_MOVE_STOP:
    default: return BSP_BRIDGE_COAST;
    }
#endif
}
static BfMove Effective(BfMove d, unsigned enabled)
{
    return enabled ? d : BF_MOVE_STOP;
}

/* 1) 方向组合矩阵：两路必须互相独立、且不产生非法桥组合。 */
static void TestDirectionMatrix(void)
{
    MotorController motor;
    const BfMove combos[3] = { BF_MOVE_FORWARD, BF_MOVE_REVERSE, BF_MOVE_STOP };
    unsigned i, j, cells = 0U;

    Motor_Init(&motor);
    assert(motor.motor_a == BF_MOVE_STOP && motor.motor_b == BF_MOVE_STOP);

    for (i = 0U; i < 3U; i++) {
        for (j = 0U; j < 3U; j++) {
            unsigned a_before = a_calls, b_before = b_calls;
            Motor_SetDirections(&motor, combos[i], combos[j]);
            /* 一次调用即刻驱动两路，不需要任何周期性 service。 */
            assert(a_calls == a_before + 1U && b_calls == b_before + 1U);
            assert(a_last.state == ExpectA(combos[i]));
            assert(b_last.state == ExpectB(combos[j]));
            assert(motor.motor_a == Effective(combos[i], CFG_MOTOR_A_ENABLED));
            assert(motor.motor_b == Effective(combos[j], CFG_MOTOR_B_ENABLED));
            if (ExpectA(combos[i]) != BSP_BRIDGE_COAST) {
                assert(a_last.duty == BSP_DutyPercentFor(s_test_duty_mode));
            }
            if (ExpectB(combos[j]) != BSP_BRIDGE_COAST) {
                assert(b_last.duty == BSP_DutyPercentFor(s_test_duty_mode));
            }
            cells++;
        }
    }
    printf("   direction matrix: %u/9 cells OK, brake_calls=%u\n", cells, brake_calls);
    assert(cells == 9U);
    assert(brake_calls == 0U);
}

/* 2) 停止 / 非法枚举值 / 失联：对应路 coast。 */
static void TestSafeStates(void)
{
    MotorController motor;

    Motor_Init(&motor);
    assert(a_last.state == BSP_BRIDGE_COAST && b_last.state == BSP_BRIDGE_COAST);

    Motor_SetDirections(&motor, (BfMove)42, (BfMove)43);
    assert(motor.motor_a == BF_MOVE_STOP && motor.motor_b == BF_MOVE_STOP);
    assert(a_last.state == BSP_BRIDGE_COAST && b_last.state == BSP_BRIDGE_COAST);

#if (CFG_MOTOR_A_ENABLED != 0U)
    Motor_SetDirections(&motor, BF_MOVE_FORWARD, BF_MOVE_STOP);
    assert(a_last.state == BSP_BRIDGE_FORWARD);
#endif
#if (CFG_MOTOR_B_ENABLED != 0U)
    Motor_SetDirections(&motor, BF_MOVE_STOP, BF_MOVE_REVERSE);
    assert(b_last.state == BSP_BRIDGE_REVERSE);
#endif
    Motor_ApplyFailsafe(&motor);
    assert(motor.motor_a == BF_MOVE_STOP && motor.motor_b == BF_MOVE_STOP);
    assert(a_last.state == BSP_BRIDGE_COAST && b_last.state == BSP_BRIDGE_COAST);

    /* 空指针必须是安全 no-op，而不是崩溃。 */
    Motor_Init(NULL);
    Motor_SetDirections(NULL, BF_MOVE_FORWARD, BF_MOVE_FORWARD);
    Motor_ApplyFailsafe(NULL);
    Motor_SetRunning(NULL, true);
    printf("   safe states: illegal/stop/failsafe/NULL all -> coast\n");
    assert(brake_calls == 0U);
}

/* 3) 本地调试入口只动 M1，M2 保持原方向。 */
static void TestSetRunningOnlyTouchesM1(void)
{
    MotorController motor;
    unsigned b_before;

    Motor_Init(&motor);
    Motor_SetDirections(&motor, BF_MOVE_STOP, BF_MOVE_REVERSE);
    b_before = b_calls;

    Motor_SetRunning(&motor, true);
    assert(b_calls == b_before);
    assert(motor.motor_a == Effective(BF_MOVE_FORWARD, CFG_MOTOR_A_ENABLED));
    assert(motor.motor_b == Effective(BF_MOVE_REVERSE, CFG_MOTOR_B_ENABLED));

    Motor_SetRunning(&motor, false);
    assert(b_calls == b_before);
    assert(motor.motor_a == BF_MOVE_STOP);
    assert(motor.motor_b == Effective(BF_MOVE_REVERSE, CFG_MOTOR_B_ENABLED));
    printf("   SetRunning: M1 toggled, M2 untouched (b_calls stable at %u)\n", b_calls);
}

/* 4) 单路禁用：只有被禁用的那一路 coast，另一路照常。 */
static void TestPerMotorDisable(void)
{
    MotorController motor;
    Motor_Init(&motor);
    Motor_SetDirections(&motor, BF_MOVE_FORWARD, BF_MOVE_REVERSE);
#if (CFG_MOTOR_A_ENABLED == 0U)
    assert(motor.motor_a == BF_MOVE_STOP && a_last.state == BSP_BRIDGE_COAST);
    printf("   M1 disabled -> coast; M2 still %s\n",
           (motor.motor_b == BF_MOVE_REVERSE) ? "REVERSE" : "(disabled)");
#else
    assert(motor.motor_a == BF_MOVE_FORWARD && a_last.state == BSP_BRIDGE_FORWARD);
#endif
#if (CFG_MOTOR_B_ENABLED == 0U)
    assert(motor.motor_b == BF_MOVE_STOP && b_last.state == BSP_BRIDGE_COAST);
    printf("   M2 disabled -> coast\n");
#else
    assert(motor.motor_b == BF_MOVE_REVERSE && b_last.state == BSP_BRIDGE_REVERSE);
#endif
    assert(brake_calls == 0U);
}

int main(void)
{
    /* 速度/占空比口径：两路共用同一挡位，满速挡为 95%（见 app_config.h 说明，
     * 95% 而非 100% 是为了保证慢周期 PWM 仍持续产生开关边沿）。 */
    assert(CFG_MOTOR_A_DUTY_PERCENT == CFG_MOTOR_FULL_DUTY_PERCENT);
    assert(CFG_MOTOR_B_DUTY_PERCENT == CFG_MOTOR_FULL_DUTY_PERCENT);
    assert(CFG_MOTOR_FULL_DUTY_PERCENT == 95U);
    assert(CFG_MOTOR_LOW_DUTY_PERCENT == 60U);
    assert(CFG_MOTOR_A_ENABLED == 1U || CFG_MOTOR_A_ENABLED == 0U);
    assert(CFG_MOTOR_B_ENABLED == 1U || CFG_MOTOR_B_ENABLED == 0U);

    TestDirectionMatrix();
    TestSafeStates();
    TestSetRunningOnlyTouchesM1();
    TestPerMotorDisable();

    assert(brake_calls == 0U);
    printf("motor_host_test: PASS (a_calls=%u b_calls=%u a_drive=%u b_drive=%u brake=%u)\n",
           a_calls, b_calls, a_drive, b_drive, brake_calls);
    return 0;
}
