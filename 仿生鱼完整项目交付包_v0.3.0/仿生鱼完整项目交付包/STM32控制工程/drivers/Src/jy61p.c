#include "jy61p.h"

#include <string.h>

#include "app_config.h"

static int16_t DecodeLittleEndianI16(const uint8_t *bytes)
{
    return (int16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8U));
}

static int16_t RawAngleToDdeg(int16_t raw)
{
    /* JY61P 常见寄存器模式定义：raw / 32768 * 180 度。待实测确认。 */
    return (int16_t)(((int32_t)raw * 1800L) / 32768L);
}

static int16_t WrappedDifferenceDdeg(int16_t a, int16_t b)
{
    int32_t difference = (int32_t)a - (int32_t)b;
    if (difference > 1800L) {
        difference -= 3600L;
    } else if (difference < -1800L) {
        difference += 3600L;
    }
    return (int16_t)difference;
}

static bool IsPlausible(const BfAttitude *old_value, const BfAttitude *new_value)
{
    int16_t roll_delta;
    int16_t pitch_delta;
    int16_t yaw_delta;
    if (!old_value->valid) {
        return true;
    }
    roll_delta = WrappedDifferenceDdeg(new_value->roll_ddeg, old_value->roll_ddeg);
    pitch_delta = WrappedDifferenceDdeg(new_value->pitch_ddeg, old_value->pitch_ddeg);
    yaw_delta = WrappedDifferenceDdeg(new_value->yaw_ddeg, old_value->yaw_ddeg);
    return (roll_delta >= -CFG_JY61P_MAX_DELTA_DDEG) &&
           (roll_delta <= CFG_JY61P_MAX_DELTA_DDEG) &&
           (pitch_delta >= -CFG_JY61P_MAX_DELTA_DDEG) &&
           (pitch_delta <= CFG_JY61P_MAX_DELTA_DDEG) &&
           (yaw_delta >= -CFG_JY61P_MAX_DELTA_DDEG) &&
           (yaw_delta <= CFG_JY61P_MAX_DELTA_DDEG);
}

bool Jy61p_Init(Jy61p *sensor, SoftI2cBus *bus, uint32_t now_ms)
{
    SoftI2cStatus status;
    if ((sensor == 0) || (bus == 0)) {
        return false;
    }
    memset(sensor, 0, sizeof(*sensor));
    sensor->bus = bus;
    sensor->next_sample_ms = now_ms;
    /* 不自动写“解锁/保存/归零”寄存器：它会永久改变未知固件的传感器设置。 */
    status = SoftI2c_BusRecover(bus);
    if (status != SOFT_I2C_OK) {
        return false;
    }
    status = SoftI2c_Probe(bus, CFG_JY61P_ADDR_7BIT);
    sensor->probed = (status == SOFT_I2C_OK);
    return sensor->probed;
}

Jy61pResult Jy61p_Service(Jy61p *sensor, uint32_t now_ms)
{
    uint8_t bytes[6];
    SoftI2cStatus status;
    BfAttitude candidate;

    if ((sensor == 0) || (sensor->bus == 0)) {
        return JY61P_RESULT_I2C_ERROR;
    }
    if ((int32_t)(now_ms - sensor->next_sample_ms) < 0) {
        return JY61P_RESULT_NONE;
    }
    sensor->next_sample_ms = now_ms + CFG_JY61P_SAMPLE_PERIOD_MS;

    status = SoftI2c_ReadRegister(sensor->bus, CFG_JY61P_ADDR_7BIT,
                                  CFG_JY61P_ANGLE_START_REG, bytes, sizeof(bytes));
    if (status != SOFT_I2C_OK) {
        sensor->consecutive_failures++;
        sensor->probed = false;
        /* ACK/总线错误后的旧姿态不能继续伪装成实时测量；下一次成功采样会重新
         * 置 valid。保留数值仅供调试，不供状态帧作为有效角度输出。 */
        sensor->attitude.valid = false;
        if (sensor->consecutive_failures >= CFG_JY61P_MAX_CONSECUTIVE_FAILURES) {
            if (SoftI2c_BusRecover(sensor->bus) != SOFT_I2C_OK) {
                return JY61P_RESULT_RECOVERY_FAILED;
            }
            (void)SoftI2c_Probe(sensor->bus, CFG_JY61P_ADDR_7BIT);
        }
        return JY61P_RESULT_I2C_ERROR;
    }

    memset(&candidate, 0, sizeof(candidate));
    candidate.roll_ddeg = RawAngleToDdeg(DecodeLittleEndianI16(&bytes[0]));
    candidate.pitch_ddeg = RawAngleToDdeg(DecodeLittleEndianI16(&bytes[2]));
    candidate.yaw_ddeg = RawAngleToDdeg(DecodeLittleEndianI16(&bytes[4]));
    candidate.valid = true;
    candidate.last_update_ms = now_ms;

    /* I2C 寄存器读没有串口 0x55 帧的 checksum；这里的校验是 ACK、范围和
     * 相邻样本合理性。若实测固件提供 CRC，必须在此处加入其官方算法。 */
    if (!IsPlausible(&sensor->attitude, &candidate)) {
        sensor->consecutive_failures++;
        sensor->attitude.valid = false;
        return JY61P_RESULT_SAMPLE_INVALID;
    }
    sensor->attitude = candidate;
    sensor->consecutive_failures = 0U;
    sensor->probed = true;
    return JY61P_RESULT_SAMPLE_OK;
}

bool Jy61p_IsStale(const Jy61p *sensor, uint32_t now_ms, uint32_t stale_after_ms)
{
    return (sensor == 0) || !sensor->attitude.valid ||
           ((uint32_t)(now_ms - sensor->attitude.last_update_ms) >= stale_after_ms);
}

void Jy61p_Invalidate(Jy61p *sensor)
{
    if (sensor != 0) {
        sensor->attitude.valid = false;
    }
}
