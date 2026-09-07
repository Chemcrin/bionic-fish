/** @file motor_control.h @brief TB6612 A/B 双桥驱动两相步进电机。 */
#ifndef MOTOR_CONTROL_H
#define MOTOR_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"

typedef struct {
    bool step_running;
    bool step_reverse;
    uint16_t step_target_rpm;
    uint16_t step_commanded_rpm;
    uint8_t step_phase;
    uint32_t next_commutation_us;
} MotorController;

void Motor_Init(MotorController *motor);
void Motor_ApplyCommand(MotorController *motor, const BfRemoteCommand *command, uint32_t now_us);
void Motor_ApplyFailsafe(MotorController *motor);
void Motor_Service(MotorController *motor, uint32_t now_us);

#endif /* MOTOR_CONTROL_H */
