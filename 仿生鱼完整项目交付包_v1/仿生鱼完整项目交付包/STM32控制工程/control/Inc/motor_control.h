/** @file motor_control.h @brief TB6612 A/B 双桥驱动两路独立直流减速电机。
 *
 * 2026-09-12：推进方案由「一颗两相双极步进 + 四拍软件换相」改为
 * 「M1 主推进 370 直流减速电机 + M2 N20 直流减速电机」，两路各自独立启停与换向。
 * TB6612 是双 H 桥直流驱动：PWM 引脚给占空比、IN1/IN2 给方向，不存在也不虚构
 * STEP/DIR；换相表、相位与换相周期均已删除。
 */
#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"

typedef struct {
    /* 两路各自**当前已下达**的方向；BF_MOVE_STOP 表示该桥处于 coast。 */
    BfMove motor_a; /* M1：主推进 370 直流减速电机 */
    BfMove motor_b; /* M2：N20 直流减速电机 */
} MotorController;

void Motor_Init(MotorController *motor);

/* 一条命令同时落地两路：M1 取 a、M2 取 b。
 * 取值不在 F/R/S 之内、或该路被编译期显式禁用时，只把**对应那一路**置为 coast，
 * 不影响另一路。同一时刻两路可以一正一反。 */
void Motor_SetDirections(MotorController *motor, BfMove a, BfMove b);

/* 一条命令同时落地两路：M1 取 command->move、M2 取 command->m2。
 * m2 是可选字段，其缺省值（BF_MOVE_STOP）由协议解析层负责填入。 */
void Motor_ApplyCommand(MotorController *motor, const BfRemoteCommand *command);

/* 失联/停止的统一失效保护：两路都 coast。 */
void Motor_ApplyFailsafe(MotorController *motor);

/* 本地调试入口（板载 K1）：只切换 M1 的运行/停止，**不改变 M2**。
 * run=true 时 M1 前进，run=false 时 M1 coast。不经过协议，不占用序号窗口。 */
void Motor_SetRunning(MotorController *motor, bool run);

#endif /* MOTOR_CONTROL_H */