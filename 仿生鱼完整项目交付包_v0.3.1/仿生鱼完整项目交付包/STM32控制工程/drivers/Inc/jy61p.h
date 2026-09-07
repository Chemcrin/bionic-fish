/** @file jy61p.h @brief JY61P I2C 寄存器模式的安全读取封装。 */
#ifndef JY61P_H
#define JY61P_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"
#include "soft_i2c.h"

typedef enum {
    JY61P_RESULT_NONE = 0,
    JY61P_RESULT_SAMPLE_OK,
    JY61P_RESULT_SAMPLE_INVALID,
    JY61P_RESULT_I2C_ERROR,
    JY61P_RESULT_RECOVERY_FAILED
} Jy61pResult;

typedef struct {
    SoftI2cBus *bus;
    BfAttitude attitude;
    uint32_t next_sample_ms;
    uint8_t consecutive_failures;
    bool probed;
} Jy61p;

bool Jy61p_Init(Jy61p *sensor, SoftI2cBus *bus, uint32_t now_ms);
Jy61pResult Jy61p_Service(Jy61p *sensor, uint32_t now_ms);
bool Jy61p_IsStale(const Jy61p *sensor, uint32_t now_ms, uint32_t stale_after_ms);
void Jy61p_Invalidate(Jy61p *sensor);

#endif /* JY61P_H */
