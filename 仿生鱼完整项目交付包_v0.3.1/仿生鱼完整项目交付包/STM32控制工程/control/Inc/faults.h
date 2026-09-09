/** @file faults.h @brief STA.err 使用的活动故障位图。 */
#ifndef FAULTS_H
#define FAULTS_H

#include <stdint.h>

typedef enum {
    BF_FAULT_NONE                     = 0U,
    BF_FAULT_LINK_TIMEOUT             = (1UL << 0),
    BF_FAULT_ESP_RX_LOST              = (1UL << 1),
    BF_FAULT_IMU_DATA_TIMEOUT         = (1UL << 2),
    BF_FAULT_IMU_I2C_RECOVERY_FAILED  = (1UL << 3),
    BF_FAULT_STEPPER_DISABLED         = (1UL << 4),
    BF_FAULT_UART_TX_DROPPED          = (1UL << 5),
    /* bit6 为协议兼容性预留位；未经协议版本升级不得复用。 */
    BF_FAULT_OLED_I2C                 = (1UL << 7),
    BF_FAULT_CONFIGURATION            = (1UL << 8)
} BfFault;

#endif /* FAULTS_H */
