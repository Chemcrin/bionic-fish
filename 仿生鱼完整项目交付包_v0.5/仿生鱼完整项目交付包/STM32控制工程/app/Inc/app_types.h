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
    BfMove move;                   /* M1：主推进 370 直流减速电机 */
    BfMove m2;                     /* M2：N20 直流减速电机；**可选**字段，缺省 STOP */
    BfTurn turn;
    uint16_t step_rpm;             /* V1 step_speed=60/100 compatibility field; not an RPM target. */
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
    bool step_running;            /* 驱动命令状态，不证明电机实际转动；等价于 motor_a != S。 */
    /* 两路电机**当前已下达**的方向（F/R/S）。STA 帧里分别编码为 m1= 与 m2=。 */
    BfMove motor_a;               /* M1：主推进 370 直流减速电机 */
    BfMove motor_b;               /* M2：N20 直流减速电机 */
    int8_t servo_deg;
    BfAttitude attitude;
    uint32_t active_faults;
    /* STA 侧地址文本（"0.0.0.0" = 未连接）。同网段的手机/PC 用它访问 <ip>:9000。 */
    char sta_ip[16];
} BfSystemSnapshot;

#endif /* APP_TYPES_H */
