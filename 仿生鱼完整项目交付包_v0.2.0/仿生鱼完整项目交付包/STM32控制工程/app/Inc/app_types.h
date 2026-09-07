/** @file app_types.h @brief 跨层共享的纯数据类型；不依赖 HAL。 */
#ifndef APP_TYPES_H
#define APP_TYPES_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BF_MOVE_FORWARD = 0,
    BF_MOVE_REVERSE,
    BF_MOVE_STOP
} BfMove;

typedef enum {
    BF_TURN_LEFT = 0,
    BF_TURN_RIGHT,
    BF_TURN_CENTER
} BfTurn;

typedef struct {
    uint16_t sequence;
    BfMove move;
    BfTurn turn;
    uint16_t step_rpm;
    int8_t servo_deg;
} BfRemoteCommand;

/* 单位为 0.1 度；避免 STM32F1 为 printf 浮点支持付出 Flash/RAM 代价。 */
typedef struct {
    int16_t roll_ddeg;
    int16_t pitch_ddeg;
    int16_t yaw_ddeg;
    bool valid;
    uint32_t last_update_ms;
} BfAttitude;

typedef struct {
    bool command_link_alive;       /* 仅代表最近收到有效 CMD，不代表 Wi-Fi/安卓已连接 */
    bool android_link_known;       /* 现有协议没有此信息，默认 false */
    bool android_link_alive;
    uint16_t last_sequence;
    uint16_t step_target_rpm;
    uint16_t step_commanded_rpm;   /* 无编码器：换相指令估算，绝非实测机械 RPM */
    bool step_running;
    int8_t servo_deg;
    BfAttitude attitude;
    uint32_t active_faults;
} BfSystemSnapshot;

#endif /* APP_TYPES_H */
