#include "motor_control.h"

#include <string.h>

#include "app_config.h"
#include "bsp_board.h"

/* 本文件只负责「一条命令  两个 H 桥的 IN1/IN2 与占空比」，没有任何换相节拍。
 * 两路的**占空比与使能互相独立**：CFG_MOTOR_A_* 只影响 M1，CFG_MOTOR_B_* 只影响 M2，
 * 因此可以单独禁用其中一路、或者给两路配不同占空比。
 *
 * 2026-09-23：占空比改为**运行时两挡**（满速 95% / 低速 60%），由 BSP 的
 * BSP_Board_SetDutyMode 统一持有。这里不再直接引用编译期常量。 */

/* 取当前挡位对应的占空比。挡位由上层（网页/按键）切换，这里是唯一读取点。 */
static uint8_t CurrentDutyPercent(void)
{
    return BSP_DutyPercentFor(BSP_Board_GetDutyMode());
}

/* 把一路方向命令落到一个 H 桥：F/R 给方向 + 当前占空比；
 * 其余（含 S、非法枚举值、该路被禁用）一律 coast。
 * coast 的桥组合是 IN1=IN2=低 + PWM 恒高，永远不会产生 IN1=IN2=高的 brake 组合。 */
static void ApplyBridgeA(MotorController *motor, BfMove direction)
{
    motor->motor_a = BF_MOVE_STOP;
    if (CFG_MOTOR_A_ENABLED == 0U) {
        BSP_BridgeA_Coast();
        return;
    }
    switch (direction) {
    case BF_MOVE_FORWARD:
        BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, CurrentDutyPercent());
        motor->motor_a = BF_MOVE_FORWARD;
        break;
    case BF_MOVE_REVERSE:
        BSP_BridgeA_Set(BSP_BRIDGE_REVERSE, CurrentDutyPercent());
        motor->motor_a = BF_MOVE_REVERSE;
        break;
    case BF_MOVE_STOP:
    default:
        BSP_BridgeA_Coast();
        break;
    }
}

static void ApplyBridgeB(MotorController *motor, BfMove direction)
{
    motor->motor_b = BF_MOVE_STOP;
    if (CFG_MOTOR_B_ENABLED == 0U) {
        BSP_BridgeB_Coast();
        return;
    }
    switch (direction) {
    case BF_MOVE_FORWARD:
        BSP_BridgeB_Set(BSP_BRIDGE_FORWARD, CurrentDutyPercent());
        motor->motor_b = BF_MOVE_FORWARD;
        break;
    case BF_MOVE_REVERSE:
        BSP_BridgeB_Set(BSP_BRIDGE_REVERSE, CurrentDutyPercent());
        motor->motor_b = BF_MOVE_REVERSE;
        break;
    case BF_MOVE_STOP:
    default:
        BSP_BridgeB_Coast();
        break;
    }
}

void Motor_Init(MotorController *motor)
{
    if (motor == 0) {
        return;
    }
    /* BF_MOVE_FORWARD 的枚举值就是 0，memset 之后必须显式写回 STOP，
     * 否则"刚初始化"会被当成"两路都在前进"。 */
    memset(motor, 0, sizeof(*motor));
    motor->motor_a = BF_MOVE_STOP;
    motor->motor_b = BF_MOVE_STOP;
    BSP_BridgeA_Coast();
    BSP_BridgeB_Coast();
}

void Motor_SetDirections(MotorController *motor, BfMove a, BfMove b)
{
    if (motor == 0) {
        return;
    }
    /* 顺序落地，互不覆盖：a 只写 motor_a/桥 A，b 只写 motor_b/桥 B。 */
    ApplyBridgeA(motor, a);
    ApplyBridgeB(motor, b);
}

void Motor_ApplyCommand(MotorController *motor, const BfRemoteCommand *command)
{
    if ((motor == 0) || (command == 0)) {
        return;
    }
    /* 一条命令同时落地两路：M1 取 move、M2 取 m2。 */
    Motor_SetDirections(motor, command->move, command->m2);
}

void Motor_ApplyFailsafe(MotorController *motor)
{
    Motor_SetDirections(motor, BF_MOVE_STOP, BF_MOVE_STOP);
}

void Motor_SetRunning(MotorController *motor, bool run)
{
    if (motor == 0) {
        return;
    }
    /* 只动 M1；M2 保持它当前的方向状态不变。 */
    ApplyBridgeA(motor, run ? BF_MOVE_FORWARD : BF_MOVE_STOP);
}