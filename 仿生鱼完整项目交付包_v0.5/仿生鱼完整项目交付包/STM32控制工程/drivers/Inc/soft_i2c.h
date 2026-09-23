/**
 * @file soft_i2c.h
 * @brief PB6/PB7、PB8/PB9 共用的有 ACK/超时/恢复能力的软件 I2C。
 */
#ifndef SOFT_I2C_H
#define SOFT_I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "stm32f1xx_hal.h"

typedef enum {
    SOFT_I2C_OK = 0,
    SOFT_I2C_NACK,
    SOFT_I2C_TIMEOUT,
    SOFT_I2C_BUS_STUCK,
    SOFT_I2C_ARGUMENT
} SoftI2cStatus;

typedef struct {
    GPIO_TypeDef *scl_port;
    uint16_t scl_pin;
    GPIO_TypeDef *sda_port;
    uint16_t sda_pin;
    uint16_t half_period_us;
    uint32_t error_count;
    /* 关断时为标准开漏（释放 = 写 1 高阻，依赖外部上拉）；打开时为台架兼容模式
     * （释放 = 切成内部上拉输入，拉低 = 切回开漏并输出 0）。详见 .c 的说明。 */
    bool internal_pullup;
} SoftI2cBus;

void SoftI2c_Init(SoftI2cBus *bus, GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin, uint16_t half_period_us);
/* 台架兼容开关。打开后该总线在"释放"时把引脚切成内部约 40 kΩ 上拉输入，在"拉低"
 * 时切回开漏输出。只有确认这条总线没有外部上拉时才允许打开：内部上拉驱动弱、
 * 边沿慢、抗 EMI 差。默认关闭，`SoftI2c_Init` 会清零。 */
void SoftI2c_SetInternalPullup(SoftI2cBus *bus, bool enabled);
SoftI2cStatus SoftI2c_Probe(SoftI2cBus *bus, uint8_t address_7bit);
SoftI2cStatus SoftI2c_Write(SoftI2cBus *bus, uint8_t address_7bit,
                             const uint8_t *data, uint16_t length);
SoftI2cStatus SoftI2c_ReadRegister(SoftI2cBus *bus, uint8_t address_7bit, uint8_t reg,
                                    uint8_t *data, uint16_t length);
SoftI2cStatus SoftI2c_BusRecover(SoftI2cBus *bus);

/* 只读诊断：两条线都释放为高，才说明外部上拉真实存在。开漏输出模式下 STM32
 * 无法启用内部上拉，因此缺上拉/接错电压时两条线会一直读回低电平，而驱动的
 * 每一次事务都只会笼统地报 TIMEOUT/STUCK。该函数不做任何驱动动作。 */
bool SoftI2c_LinesReleased(const SoftI2cBus *bus);
/* 分别报告 SCL/SDA 当前电平：只报"忙"无法区分是主机把哪条线留在低电平，还是
 * 从机在事务中途拉住 SDA。该函数不做任何驱动动作。 */
void SoftI2c_LineLevels(const SoftI2cBus *bus, bool *scl_high, bool *sda_high);
/* 诊断专用：把两条线临时切成"带上拉的输入"（STM32 内部约 40 kΩ）再读回电平。
 * 内部上拉远弱于任何正常的外部上拉，所以结果只回答一个二选一问题——线路上有没有
 * 东西在拉低：
 *   读回高 —— 没有外器件拉低，线路处于高阻态（即外部上拉缺失）；
 *   读回低 —— 有外器件或短路在拉低，内部上拉抢不过它。
 * 它不会把任何线驱动为低，用完立即还原成开漏输出并释放。 */
void SoftI2c_IdleLevelsWithInternalPullup(SoftI2cBus *bus, bool *scl_high, bool *sda_high);
const char *SoftI2c_StatusName(SoftI2cStatus status);

#endif /* SOFT_I2C_H */
