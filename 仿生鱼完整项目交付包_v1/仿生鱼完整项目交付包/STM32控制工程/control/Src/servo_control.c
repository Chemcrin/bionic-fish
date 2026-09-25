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
    /* 2026-09-11：**舵机左右方向对调**（用户要求）。
     * 协议/UI 语义**不变**—左转仍是负角、右转仍是正角—只把"角度 → 脉宽"的极性
     * 反过来，于是上位机、网页、板载按键都不必改动，物理方向整体反转。
     *
     * 2026-09-23：网页端反馈左右仍然相反。经核对，**极性对调只应存在一处**：
     * 本函数已经做了 -angle。网页端此前把「左舵」按钮写成 sv=-SM、「右舵」写成
     * sv=+SM，与协议语义一致，因此网页端不再做二次取反（二次取反会把方向再翻回去）。
     * 若实机仍感觉相反，应先确认舵机安装朝向，而不是继续在软件里叠加取反。
     *
     * 2026-09-23：**行程由 ±20° 收窄为 ±15°**。限幅与换算基准已分离：
     * ClampAngle 用 CFG_SERVO_SAFE_*（现在 ±15°），下面的脉宽换算用
     * CFG_SERVO_FULL_SCALE_DEG（仍是 30）。因此 ±15° 只给出满量程 1/2 的脉宽偏移
     * （±250 µs → 1250 / 1750 µs），机械转角是真的变小了，
     * 而不是只把标签从 30 改成 15。 */
    angle = (int8_t)(-angle);
    if (angle >= 0) {
        pulse = (int32_t)CFG_SERVO_CENTER_PULSE_US +
                ((int32_t)(CFG_SERVO_MAX_PULSE_US - CFG_SERVO_CENTER_PULSE_US) * angle) /
                    CFG_SERVO_FULL_SCALE_DEG;
    } else {
        pulse = (int32_t)CFG_SERVO_CENTER_PULSE_US +
                ((int32_t)(CFG_SERVO_CENTER_PULSE_US - CFG_SERVO_MIN_PULSE_US) * angle) /
                    CFG_SERVO_FULL_SCALE_DEG;
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
