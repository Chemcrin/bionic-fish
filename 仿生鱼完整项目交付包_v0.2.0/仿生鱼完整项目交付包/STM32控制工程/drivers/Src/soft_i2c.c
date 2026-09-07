#include "soft_i2c.h"

#include "app_config.h"
#include "bsp_time.h"

static void DriveLow(const SoftI2cBus *bus, GPIO_TypeDef *port, uint16_t pin)
{
    (void)bus;
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}

static void ReleaseLine(const SoftI2cBus *bus, GPIO_TypeDef *port, uint16_t pin)
{
    (void)bus;
    /* GPIO 已配置为开漏输出；写 1 是释放总线，绝不是向 5 V 推高。 */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}

static GPIO_PinState ReadLine(GPIO_TypeDef *port, uint16_t pin)
{
    return HAL_GPIO_ReadPin(port, pin);
}

static bool TimeExpired(uint32_t started_at_us, uint32_t timeout_us)
{
    return (uint32_t)(BSP_Micros() - started_at_us) >= timeout_us;
}

static SoftI2cStatus ClockHigh(SoftI2cBus *bus)
{
    uint32_t started_at;
    ReleaseLine(bus, bus->scl_port, bus->scl_pin);
    started_at = BSP_Micros();
    while (ReadLine(bus->scl_port, bus->scl_pin) == GPIO_PIN_RESET) {
        if (TimeExpired(started_at, CFG_SOFT_I2C_CLOCK_STRETCH_TIMEOUT_US)) {
            return SOFT_I2C_TIMEOUT;
        }
    }
    BSP_DelayUs(bus->half_period_us);
    return SOFT_I2C_OK;
}

static void ClockLow(SoftI2cBus *bus)
{
    DriveLow(bus, bus->scl_port, bus->scl_pin);
    BSP_DelayUs(bus->half_period_us);
}

static SoftI2cStatus WaitBusReleased(SoftI2cBus *bus)
{
    uint32_t started_at = BSP_Micros();
    ReleaseLine(bus, bus->scl_port, bus->scl_pin);
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    while ((ReadLine(bus->scl_port, bus->scl_pin) == GPIO_PIN_RESET) ||
           (ReadLine(bus->sda_port, bus->sda_pin) == GPIO_PIN_RESET)) {
        if (TimeExpired(started_at, CFG_SOFT_I2C_ACK_TIMEOUT_US)) {
            return SOFT_I2C_BUS_STUCK;
        }
    }
    return SOFT_I2C_OK;
}

static SoftI2cStatus Start(SoftI2cBus *bus, bool repeated)
{
    SoftI2cStatus status;
    if (!repeated) {
        status = WaitBusReleased(bus);
        if (status != SOFT_I2C_OK) {
            return status;
        }
    } else {
        ReleaseLine(bus, bus->sda_port, bus->sda_pin);
        BSP_DelayUs(bus->half_period_us);
        status = ClockHigh(bus);
        if (status != SOFT_I2C_OK) {
            return status;
        }
    }
    /* SDA 在 SCL 为高时由高变低，即 START / repeated START。 */
    DriveLow(bus, bus->sda_port, bus->sda_pin);
    BSP_DelayUs(bus->half_period_us);
    ClockLow(bus);
    return SOFT_I2C_OK;
}

static SoftI2cStatus Stop(SoftI2cBus *bus)
{
    SoftI2cStatus status;
    DriveLow(bus, bus->sda_port, bus->sda_pin);
    BSP_DelayUs(bus->half_period_us);
    status = ClockHigh(bus);
    if (status != SOFT_I2C_OK) {
        return status;
    }
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    BSP_DelayUs(bus->half_period_us);
    return SOFT_I2C_OK;
}

static SoftI2cStatus WriteByte(SoftI2cBus *bus, uint8_t value)
{
    uint8_t bit;
    SoftI2cStatus status;

    for (bit = 0U; bit < 8U; bit++) {
        if ((value & 0x80U) != 0U) {
            ReleaseLine(bus, bus->sda_port, bus->sda_pin);
        } else {
            DriveLow(bus, bus->sda_port, bus->sda_pin);
        }
        BSP_DelayUs(bus->half_period_us);
        status = ClockHigh(bus);
        if (status != SOFT_I2C_OK) {
            return status;
        }
        ClockLow(bus);
        value <<= 1U;
    }

    /* 第 9 个时钟由从机拉低 SDA 表示 ACK。 */
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    BSP_DelayUs(bus->half_period_us);
    status = ClockHigh(bus);
    if (status != SOFT_I2C_OK) {
        return status;
    }
    if (ReadLine(bus->sda_port, bus->sda_pin) != GPIO_PIN_RESET) {
        ClockLow(bus);
        return SOFT_I2C_NACK;
    }
    ClockLow(bus);
    return SOFT_I2C_OK;
}

static SoftI2cStatus ReadByte(SoftI2cBus *bus, uint8_t *value, bool acknowledge)
{
    uint8_t bit;
    uint8_t received = 0U;
    SoftI2cStatus status;

    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    for (bit = 0U; bit < 8U; bit++) {
        status = ClockHigh(bus);
        if (status != SOFT_I2C_OK) {
            return status;
        }
        received = (uint8_t)(received << 1U);
        if (ReadLine(bus->sda_port, bus->sda_pin) == GPIO_PIN_SET) {
            received |= 1U;
        }
        ClockLow(bus);
    }

    if (acknowledge) {
        DriveLow(bus, bus->sda_port, bus->sda_pin);
    } else {
        ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    }
    BSP_DelayUs(bus->half_period_us);
    status = ClockHigh(bus);
    if (status != SOFT_I2C_OK) {
        return status;
    }
    ClockLow(bus);
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    *value = received;
    return SOFT_I2C_OK;
}

static SoftI2cStatus FinishWithRecovery(SoftI2cBus *bus, SoftI2cStatus status)
{
    SoftI2cStatus stop_status = Stop(bus);
    if (status == SOFT_I2C_OK) {
        status = stop_status;
    }
    if (status != SOFT_I2C_OK) {
        bus->error_count++;
        (void)SoftI2c_BusRecover(bus);
    }
    return status;
}

void SoftI2c_Init(SoftI2cBus *bus, GPIO_TypeDef *scl_port, uint16_t scl_pin,
                  GPIO_TypeDef *sda_port, uint16_t sda_pin, uint16_t half_period_us)
{
    if (bus == 0) {
        return;
    }
    bus->scl_port = scl_port;
    bus->scl_pin = scl_pin;
    bus->sda_port = sda_port;
    bus->sda_pin = sda_pin;
    bus->half_period_us = (half_period_us == 0U) ? 1U : half_period_us;
    bus->error_count = 0U;
    if ((scl_port != 0) && (sda_port != 0)) {
        ReleaseLine(bus, scl_port, scl_pin);
        ReleaseLine(bus, sda_port, sda_pin);
    }
}

SoftI2cStatus SoftI2c_BusRecover(SoftI2cBus *bus)
{
    uint8_t pulse;
    SoftI2cStatus status;

    if ((bus == 0) || (bus->scl_port == 0) || (bus->sda_port == 0)) {
        return SOFT_I2C_ARGUMENT;
    }
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    for (pulse = 0U; pulse < 9U; pulse++) {
        ClockLow(bus);
        status = ClockHigh(bus);
        if (status != SOFT_I2C_OK) {
            return status;
        }
    }
    if (Stop(bus) != SOFT_I2C_OK) {
        return SOFT_I2C_TIMEOUT;
    }
    return WaitBusReleased(bus);
}

SoftI2cStatus SoftI2c_Probe(SoftI2cBus *bus, uint8_t address_7bit)
{
    SoftI2cStatus status;
    if ((bus == 0) || (address_7bit > 0x7FU)) {
        return SOFT_I2C_ARGUMENT;
    }
    status = Start(bus, false);
    if (status == SOFT_I2C_OK) {
        status = WriteByte(bus, (uint8_t)(address_7bit << 1U));
    }
    return FinishWithRecovery(bus, status);
}

SoftI2cStatus SoftI2c_Write(SoftI2cBus *bus, uint8_t address_7bit,
                             const uint8_t *data, uint16_t length)
{
    uint16_t index;
    SoftI2cStatus status;
    if ((bus == 0) || (data == 0) || (length == 0U) || (address_7bit > 0x7FU)) {
        return SOFT_I2C_ARGUMENT;
    }
    status = Start(bus, false);
    if (status == SOFT_I2C_OK) {
        status = WriteByte(bus, (uint8_t)(address_7bit << 1U));
    }
    for (index = 0U; (index < length) && (status == SOFT_I2C_OK); index++) {
        status = WriteByte(bus, data[index]);
    }
    return FinishWithRecovery(bus, status);
}

SoftI2cStatus SoftI2c_ReadRegister(SoftI2cBus *bus, uint8_t address_7bit, uint8_t reg,
                                    uint8_t *data, uint16_t length)
{
    uint16_t index;
    SoftI2cStatus status;
    if ((bus == 0) || (data == 0) || (length == 0U) || (address_7bit > 0x7FU)) {
        return SOFT_I2C_ARGUMENT;
    }
    status = Start(bus, false);
    if (status == SOFT_I2C_OK) {
        status = WriteByte(bus, (uint8_t)(address_7bit << 1U));
    }
    if (status == SOFT_I2C_OK) {
        status = WriteByte(bus, reg);
    }
    if (status == SOFT_I2C_OK) {
        status = Start(bus, true);
    }
    if (status == SOFT_I2C_OK) {
        status = WriteByte(bus, (uint8_t)((address_7bit << 1U) | 1U));
    }
    for (index = 0U; (index < length) && (status == SOFT_I2C_OK); index++) {
        status = ReadByte(bus, &data[index], (index + 1U) < length);
    }
    return FinishWithRecovery(bus, status);
}
