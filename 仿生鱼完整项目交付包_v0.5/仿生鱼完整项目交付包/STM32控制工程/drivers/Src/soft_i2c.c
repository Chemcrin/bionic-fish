#include "soft_i2c.h"

#include "app_config.h"
#include "bsp_time.h"

/* 台架兼容模式说明
 *
 * STM32F1 的开漏输出模式忽略 PU/PD 位，所以"开漏 + 内部上拉"不可兼得：只要引脚
 * 是 GPIO_MODE_OUTPUT_OD，就只剩高阻，完全依赖外部上拉。缺外部上拉时，主机把线
 * 驱动为低之后再写 1，线只会停在低电平（引脚电容没有充电通路），此后每次事务都
 * 必然失败——实测现象正是 scl=0 sda=0 且内部上拉一挂上就恢复为高。
 *
 * 因此兼容模式在两种引脚状态之间切换，而不是靠 ODR：
 *   释放（线为高）→ GPIO_MODE_INPUT + GPIO_PULLUP，由内部约 40 kΩ 充电；
 *   拉低         → GPIO_MODE_OUTPUT_OD + 写 0。
 * 代价是每个边沿多一次引脚模式写入，位周期变长；ClockHigh 本来就是"等到线真的变
 * 高再延时"，因此只是把总线跑慢，不会破坏时序正确性。 */
static void ApplyPinMode(GPIO_TypeDef *port, uint16_t pin, uint32_t mode, uint32_t pull)
{
    GPIO_InitTypeDef init = {0};
    if (port == 0) {
        return;
    }
    init.Pin = pin;
    init.Mode = mode;
    init.Pull = pull;
    init.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(port, &init);
}

static bool BusUsesInternalPullup(const SoftI2cBus *bus)
{
    return (bus != 0) && bus->internal_pullup;
}

static void ReleaseLine(const SoftI2cBus *bus, GPIO_TypeDef *port, uint16_t pin)
{
    if (BusUsesInternalPullup(bus)) {
        ApplyPinMode(port, pin, GPIO_MODE_INPUT, GPIO_PULLUP);
        return;
    }
    /* GPIO 已配置为开漏输出；写 1 是释放总线，绝不是向 5 V 推高。 */
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}

static void DriveLow(const SoftI2cBus *bus, GPIO_TypeDef *port, uint16_t pin)
{
    if (BusUsesInternalPullup(bus)) {
        /* 必须先切回开漏输出，输入模式下的写 0 只是选择下拉，不驱动总线。 */
        ApplyPinMode(port, pin, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL);
    }
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}

/* 任何提前返回都必须先把两条线释放回高。否则一次瞬时超时会把 SDA 或 SCL 钉在
 * 低电平：主机自己拉住的低电平同时骗过总线的空闲判定，让后续每次探测都立刻
 * 失败，把一次本可恢复的毛刺放大成永久死总线。 */
static void ReleaseBothLines(SoftI2cBus *bus)
{
    ReleaseLine(bus, bus->sda_port, bus->sda_pin);
    ReleaseLine(bus, bus->scl_port, bus->scl_pin);
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
        ReleaseBothLines(bus);
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
            ReleaseBothLines(bus);
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
        ReleaseBothLines(bus);
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
            ReleaseBothLines(bus);
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
        /* acknowledge 分支此刻正把 SDA 拉低；不释放就会把主机自己变成"从机"，
         * 让总线永远读不回空闲。 */
        ReleaseBothLines(bus);
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
        /* 兜底：本次事务不能把任何一条线留给主机自己拉着。 */
        ReleaseBothLines(bus);
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
    bus->internal_pullup = false;
    if ((scl_port != 0) && (sda_port != 0)) {
        ReleaseLine(bus, scl_port, scl_pin);
        ReleaseLine(bus, sda_port, sda_pin);
    }
}

void SoftI2c_SetInternalPullup(SoftI2cBus *bus, bool enabled)
{
    if (bus == 0) {
        return;
    }
    bus->internal_pullup = enabled;
    /* 立刻按新策略释放一次，否则引脚会停在切换前的模式上。 */
    ReleaseBothLines(bus);
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
            ReleaseBothLines(bus);
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

/* 空闲检查：OD 输出写 1 后，只有当外部上拉把两条线真正拉高时才读回高电平。
 * 缺上拉、上拉接到错误电压、从机把线拉死、或引脚没配成 OD，都会读回低。 */
bool SoftI2c_LinesReleased(const SoftI2cBus *bus)
{
    if ((bus == 0) || (bus->scl_port == 0) || (bus->sda_port == 0)) {
        return false;
    }
    return (ReadLine(bus->scl_port, bus->scl_pin) == GPIO_PIN_SET) &&
           (ReadLine(bus->sda_port, bus->sda_pin) == GPIO_PIN_SET);
}

void SoftI2c_IdleLevelsWithInternalPullup(SoftI2cBus *bus, bool *scl_high, bool *sda_high)
{
    bool scl = false;
    bool sda = false;

    if ((bus != 0) && (bus->scl_port != 0) && (bus->sda_port != 0)) {
        /* 先释放为高再切成带上拉的输入，切换过程不向总线输出任何低电平。 */
        ReleaseBothLines(bus);
        ApplyPinMode(bus->scl_port, bus->scl_pin, GPIO_MODE_INPUT, GPIO_PULLUP);
        ApplyPinMode(bus->sda_port, bus->sda_pin, GPIO_MODE_INPUT, GPIO_PULLUP);
        /* 内部上拉约 40 kΩ，几十 pF 线容几个微秒即稳定；这里留足余量。 */
        BSP_DelayUs(500U);
        scl = (ReadLine(bus->scl_port, bus->scl_pin) == GPIO_PIN_SET);
        sda = (ReadLine(bus->sda_port, bus->sda_pin) == GPIO_PIN_SET);
        /* 固定切回开漏输出后再释放；兼容模式下 ReleaseLine 会再切成上拉输入。 */
        ApplyPinMode(bus->scl_port, bus->scl_pin, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL);
        ApplyPinMode(bus->sda_port, bus->sda_pin, GPIO_MODE_OUTPUT_OD, GPIO_NOPULL);
        ReleaseBothLines(bus);
    }
    if (scl_high != 0) {
        *scl_high = scl;
    }
    if (sda_high != 0) {
        *sda_high = sda;
    }
}

void SoftI2c_LineLevels(const SoftI2cBus *bus, bool *scl_high, bool *sda_high)
{
    bool scl = false;
    bool sda = false;
    if ((bus != 0) && (bus->scl_port != 0) && (bus->sda_port != 0)) {
        scl = (ReadLine(bus->scl_port, bus->scl_pin) == GPIO_PIN_SET);
        sda = (ReadLine(bus->sda_port, bus->sda_pin) == GPIO_PIN_SET);
    }
    if (scl_high != 0) {
        *scl_high = scl;
    }
    if (sda_high != 0) {
        *sda_high = sda;
    }
}

const char *SoftI2c_StatusName(SoftI2cStatus status)
{
    switch (status) {
    case SOFT_I2C_OK: return "OK";
    case SOFT_I2C_NACK: return "NACK";
    case SOFT_I2C_TIMEOUT: return "TIMEOUT";
    case SOFT_I2C_BUS_STUCK: return "STUCK";
    case SOFT_I2C_ARGUMENT: return "ARG";
    default: return "UNKNOWN";
    }
}
