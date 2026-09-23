#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "jy61p.h"
#include "ssd1306.h"

static SoftI2cStatus read_status;
static SoftI2cStatus recover_status;
static SoftI2cStatus probe_status;
static SoftI2cStatus write_status;
static uint8_t sample[6];
static unsigned recover_calls;
static unsigned probe_calls;
static unsigned read_calls;
static unsigned write_calls;
static uint8_t last_write[32];
static uint16_t last_write_length;

SoftI2cStatus SoftI2c_BusRecover(SoftI2cBus *bus)
{
    assert(bus != NULL);
    recover_calls++;
    return recover_status;
}

SoftI2cStatus SoftI2c_Probe(SoftI2cBus *bus, uint8_t address)
{
    assert(bus != NULL && address < 128U);
    probe_calls++;
    return probe_status;
}

SoftI2cStatus SoftI2c_ReadRegister(SoftI2cBus *bus, uint8_t address, uint8_t reg,
                                uint8_t *data, uint16_t length)
{
    assert(bus != NULL && address == CFG_JY61P_ADDR_7BIT);
    assert(reg == CFG_JY61P_ANGLE_START_REG && length == sizeof(sample));
    read_calls++;
    if (read_status == SOFT_I2C_OK) {
        memcpy(data, sample, length);
    }
    return read_status;
}

SoftI2cStatus SoftI2c_Write(SoftI2cBus *bus, uint8_t address,
                          const uint8_t *data, uint16_t length)
{
    assert(bus != NULL && address == CFG_OLED_ADDR_7BIT);
    assert(data != NULL && length <= sizeof(last_write));
    write_calls++;
    memcpy(last_write, data, length);
    last_write_length = length;
    return write_status;
}

static void ResetBus(void)
{
    read_status = recover_status = probe_status = write_status = SOFT_I2C_OK;
    recover_calls = probe_calls = read_calls = write_calls = 0U;
    memset(sample, 0, sizeof(sample));
}

static void SetRawSample(int16_t raw)
{
    unsigned axis;
    for (axis = 0U; axis < 3U; axis++) {
        sample[axis * 2U] = (uint8_t)(uint16_t)raw;
        sample[axis * 2U + 1U] = (uint8_t)((uint16_t)raw >> 8U);
    }
}

static void TestAttitudeAndSpike(void)
{
    SoftI2cBus bus = {0};
    Jy61p sensor;
    ResetBus();
    assert(Jy61p_Init(&sensor, &bus, 0U));
    SetRawSample(32700);
    assert(Jy61p_Service(&sensor, 0U) == JY61P_RESULT_SAMPLE_OK);
    assert(sensor.attitude.yaw_ddeg == 1796);
    SetRawSample(-32700);
    assert(Jy61p_Service(&sensor, 50U) == JY61P_RESULT_SAMPLE_OK);
    assert(sensor.attitude.yaw_ddeg == -1796); /* 跨 ±180 度仅变化 0.8 度。 */

    SetRawSample(0);
#if (CFG_JY61P_MAX_DELTA_DDEG != 0)
    assert(Jy61p_Service(&sensor, 100U) == JY61P_RESULT_SAMPLE_INVALID);
    assert(!sensor.attitude.valid && sensor.consecutive_failures == 1U);
#else
    assert(Jy61p_Service(&sensor, 100U) == JY61P_RESULT_SAMPLE_OK);
#endif
    /* 失效后下一次成功读建立新基准，不因旧值继续拒绝。 */
    assert(Jy61p_Service(&sensor, 150U) == JY61P_RESULT_SAMPLE_OK);
    assert(sensor.attitude.valid && sensor.attitude.yaw_ddeg == 0);
    assert(sensor.consecutive_failures == 0U);
    SetRawSample(30000);
    assert(Jy61p_Service(&sensor, 500U) == JY61P_RESULT_SAMPLE_OK);
    assert(sensor.attitude.yaw_ddeg == 1647); /* 久未采样，不以过期值拒绝恢复。 */
}

static void TestSensorRecovery(void)
{
    SoftI2cBus bus = {0};
    Jy61p sensor;
    uint32_t now = 0U;
    unsigned i;
    ResetBus();
    assert(Jy61p_Init(&sensor, &bus, now));
    assert(Jy61p_Service(&sensor, now) == JY61P_RESULT_SAMPLE_OK);
    read_status = SOFT_I2C_NACK;
    for (i = 0U; i < 2U; i++) {
        now += 50U;
        assert(Jy61p_Service(&sensor, now) == JY61P_RESULT_I2C_ERROR);
        assert(!sensor.attitude.valid && !sensor.probed);
    }
    recover_status = SOFT_I2C_BUS_STUCK;
    now += 50U;
    assert(Jy61p_Service(&sensor, now) == JY61P_RESULT_RECOVERY_FAILED);
    recover_status = SOFT_I2C_OK;
    probe_status = SOFT_I2C_NACK;
    for (i = 0U; i < 300U; i++) {
        now += 50U;
        assert(Jy61p_Service(&sensor, now) == JY61P_RESULT_RECOVERY_FAILED);
        assert(!sensor.probed);
    }
    assert(sensor.consecutive_failures == UINT8_MAX);
    assert(recover_calls == 302U); /* 初始化一次、卡总线一次、断线重探测300次。 */
    assert(probe_calls == 301U);
    read_status = SOFT_I2C_OK;
    probe_status = SOFT_I2C_OK;
    SetRawSample(30000);
    now += 50U;
    assert(Jy61p_Service(&sensor, now) == JY61P_RESULT_SAMPLE_OK);
    assert(sensor.probed && sensor.attitude.valid && sensor.consecutive_failures == 0U);
    assert(sensor.attitude.yaw_ddeg == 1647);
}

static void TestMillisecondWrap(void)
{
    SoftI2cBus bus = {0};
    Jy61p sensor;
    ResetBus();
    assert(Jy61p_Init(&sensor, &bus, UINT32_MAX - 15U));
    assert(Jy61p_Service(&sensor, UINT32_MAX - 15U) == JY61P_RESULT_SAMPLE_OK);
    assert(Jy61p_Service(&sensor, 33U) == JY61P_RESULT_NONE);
    assert(!Jy61p_IsStale(&sensor, 33U, 50U));
    assert(Jy61p_IsStale(&sensor, 34U, 50U));
    assert(Jy61p_Service(&sensor, 34U) == JY61P_RESULT_SAMPLE_OK);
    assert(read_calls == 2U);
}

static void FinishDisplay(Ssd1306 *display)
{
    unsigned budget = 200U;
    while (!Ssd1306_IsIdle(display) && budget-- != 0U) {
        Ssd1306_Service(display);
    }
    assert(Ssd1306_IsIdle(display));
}

static void TestOledRecovery(void)
{
    SoftI2cBus bus = {0};
    Ssd1306 display;
    ResetBus();
    probe_status = SOFT_I2C_NACK;
    assert(!Ssd1306_Init(&display, &bus, CFG_OLED_ADDR_7BIT));
    assert(!display.initialized && Ssd1306_I2cErrorActive(&display));
    probe_status = SOFT_I2C_OK;
    assert(Ssd1306_Init(&display, &bus, CFG_OLED_ADDR_7BIT));
    assert(last_write[0] == 0x00U && last_write[1] == 0xAEU);
    assert(last_write[last_write_length - 1U] == 0xAFU);
    FinishDisplay(&display);
    assert(!Ssd1306_I2cErrorActive(&display));

    Ssd1306_DrawString(&display, 0U, 0U, "STEP:OFF T:  0");
    assert(Ssd1306_BeginRefresh(&display));
    Ssd1306_Service(&display); /* 页地址成功、随后数据半途失败。 */
    write_status = SOFT_I2C_NACK;
    Ssd1306_Service(&display);
    assert(!display.initialized && Ssd1306_IsIdle(&display));
    assert(Ssd1306_I2cErrorActive(&display));
    assert(Ssd1306_I2cFailureCount(&display) == 1U);
    write_status = SOFT_I2C_OK;
    assert(!Ssd1306_BeginRefresh(&display)); /* 未重发初始化前禁止假恢复。 */
    assert(Ssd1306_Init(&display, &bus, CFG_OLED_ADDR_7BIT));
    FinishDisplay(&display);
    assert(display.initialized && !Ssd1306_I2cErrorActive(&display));

    assert(Ssd1306_BeginRefresh(&display));
    write_status = SOFT_I2C_TIMEOUT;
    Ssd1306_Service(&display); /* 页地址失败同样要求重新初始化。 */
    assert(!display.initialized && Ssd1306_IsIdle(&display));
    assert(Ssd1306_I2cErrorActive(&display));
    write_status = SOFT_I2C_OK;
    assert(Ssd1306_Init(&display, &bus, CFG_OLED_ADDR_7BIT));
    FinishDisplay(&display);
    assert(!Ssd1306_I2cErrorActive(&display));
}

int main(void)
{
    TestAttitudeAndSpike();
    TestSensorRecovery();
    TestMillisecondWrap();
    TestOledRecovery();
    puts("sensor_oled_host_test: PASS");
    return 0;
}
