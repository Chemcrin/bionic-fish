#include "servo_control.h"

#include "app_config.h"
#include "bsp_board.h"

static int8_t ClampAngle(int8_t angle)
{
    if (angle < CFG_SERVO_SAFE_MIN_DEG) {
        return CFG_SERVO_SAFE_MIN_DEG;
    }
    if (angle > CFG_SERVO_SAFE_MAX_DEG) {
        return CFG_SERVO_SAFE_MAX_DEG;
    }
    return angle;
}

static uint16_t AngleToPulseUs(int8_t angle)
{
    int32_t pulse;
    angle = ClampAngle(angle);
    if (angle >= 0) {
        pulse = (int32_t)CFG_SERVO_CENTER_PULSE_US +
                ((int32_t)(CFG_SERVO_MAX_PULSE_US - CFG_SERVO_CENTER_PULSE_US) * angle) /
                    CFG_SERVO_SAFE_MAX_DEG;
    } else {
        pulse = (int32_t)CFG_SERVO_CENTER_PULSE_US +
                ((int32_t)(CFG_SERVO_CENTER_PULSE_US - CFG_SERVO_MIN_PULSE_US) * angle) /
                    (-CFG_SERVO_SAFE_MIN_DEG);
    }
    return (uint16_t)pulse;
}

void Servo_Init(ServoController *servo)
{
    if (servo == 0) {
        return;
    }
    servo->commanded_deg = 0;
    BSP_Servo_SetPulseUs(CFG_SERVO_CENTER_PULSE_US);
}

void Servo_SetAngle(ServoController *servo, int8_t requested_deg)
{
    if (servo == 0) {
        return;
    }
    servo->commanded_deg = ClampAngle(requested_deg);
    BSP_Servo_SetPulseUs(AngleToPulseUs(servo->commanded_deg));
}

void Servo_Center(ServoController *servo)
{
    Servo_SetAngle(servo, 0);
}
