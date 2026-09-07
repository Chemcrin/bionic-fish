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

void BSP_Board_Init(void);
void BSP_Board_SetStartupSafeState(void);
void BSP_Board_EmergencyCoast(void);

void BSP_BridgeA_Set(BspBridgeState state, uint8_t duty_percent);
void BSP_BridgeB_Set(BspBridgeState state, uint8_t duty_percent);
void BSP_BridgeA_Coast(void);
void BSP_BridgeB_Coast(void);
void BSP_Servo_SetPulseUs(uint16_t pulse_us);
bool BSP_ButtonPressed(uint8_t index);
void BSP_RunLed_Set(bool on);
void BSP_RunLed_Toggle(void);

#endif /* BSP_BOARD_H */
