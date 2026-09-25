/** @file bsp_board.h @brief 对所有已确认 GPIO/PWM 的小型板级抽象。 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BSP_BRIDGE_FORWARD = 0,
    BSP_BRIDGE_REVERSE,
    BSP_BRIDGE_COAST,
    BSP_BRIDGE_BRAKE
} BspBridgeState;

/* 运行时可选的两挡占空比。慢周期(2 s)PWM 下，它是"每周期导通多久"的比例：
 *   BSP_DUTY_FULL = 满速挡（默认 95%，见 app_config.h 的说明）
 *   BSP_DUTY_LOW  = 低速挡（60%）
 * 两挡都不改变方向，只改变平均电压。 */
typedef enum {
    BSP_DUTY_FULL = 0,
    BSP_DUTY_LOW
} BspDutyMode;

void BSP_Board_Init(void);
void BSP_Board_SetStartupSafeState(void);
void BSP_Board_EmergencyCoast(void);

/* 选择两路电机共用的占空比挡位。停止状态下的桥不受影响。 */
void BSP_Board_SetDutyMode(BspDutyMode mode);
BspDutyMode BSP_Board_GetDutyMode(void);
uint8_t BSP_DutyPercentFor(BspDutyMode mode);

void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty_percent);
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty_percent);
void BSP_BridgeA_Coast(void);
void BSP_BridgeB_Coast(void);
void BSP_Servo_SetPulseUs(uint16_t pulse_us);
bool BSP_ButtonPressed(uint8_t index);
void BSP_RunLed_Set(bool on);
void BSP_RunLed_Toggle(void);

#endif /* BSP_BOARD_H */
