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
} SoftI2cBus;

void SoftI2c_Init(SoftI2cBus *bus, GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin, uint16_t half_period_us);
SoftI2cStatus SoftI2c_Probe(SoftI2cBus *bus, uint8_t address_7bit);
SoftI2cStatus SoftI2c_Write(SoftI2cBus *bus, uint8_t address_7bit,
                             const uint8_t *data, uint16_t length);
SoftI2cStatus SoftI2c_ReadRegister(SoftI2cBus *bus, uint8_t address_7bit, uint8_t reg,
                                    uint8_t *data, uint16_t length);
SoftI2cStatus SoftI2c_BusRecover(SoftI2cBus *bus);

#endif /* SOFT_I2C_H */
