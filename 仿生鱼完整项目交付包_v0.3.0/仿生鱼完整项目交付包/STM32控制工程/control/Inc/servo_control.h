/** @file servo_control.h @brief 舵机相对中位角限幅与脉宽映射。 */
#ifndef SERVO_CONTROL_H
#define SERVO_CONTROL_H

#include <stdint.h>

typedef struct {
    int8_t commanded_deg;
} ServoController;

void Servo_Init(ServoController *servo);
void Servo_SetAngle(ServoController *servo, int8_t requested_deg);
void Servo_Center(ServoController *servo);

#endif /* SERVO_CONTROL_H */
