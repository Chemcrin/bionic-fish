#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "bsp_board.h"
#include "bsp_time.h"
#include "main.h"
#include "tim.h"

/* 2026-09-23：TIM2 周期由 50 µs 改为 2 s，占空比改为两挡（满速 95% / 低速 60%），
 * 且 SetBridgeCompare **不再等待自然 Update**（2 s 的等待会卡死控制循环），
 * 改为写 CCR 后主动置 UG 立即装载。
 *
 * 本文件把 TIM2 的硬件契约建模如下：
 *   - CCR 写入只改影子寄存器；
 *   - 计数器回绕（自然 Update）或软件 UG 都会把影子提交到活动寄存器；
 *   - 只有"驱动中"的桥才计入连续高电平。
 * 关键不变式：**方向脚只能在 compare 已提交后才置位**，否则会出现用陈旧占空比
 * 驱动的回归。 */
enum { PERIOD_TICKS = 20000, TICKS_PER_US = 10000 };
/* 10% 与满速挡/低速挡的期望 tick 数，均以 PERIOD_TICKS 为基。 */
#define TICKS_FOR(pct) ((uint32_t)(((uint64_t)(PERIOD_TICKS) * (pct)) / 100ULL))

static struct {
    uint32_t counter, shadow[2], active[2], high_ticks[2], max_high_ticks[2];
    uint32_t updates, ug_events, writes, flag_reads;
    bool running, update_flag;
} motor;
static uint64_t clock_ticks;
static uint32_t micros_offset, micros_reads, error_calls;
static uint32_t servo_compare;

GPIO_TypeDef host_gpio_a, host_gpio_b, host_gpio_c;
TIM_HandleTypeDef htim2 = {2U}, htim3 = {3U}, htim4 = {4U};

static unsigned int ChannelIndex(uint32_t channel)
{
    assert(channel == TIM_CHANNEL_1 || channel == TIM_CHANNEL_2);
    return channel == TIM_CHANNEL_1 ? 0U : 1U;
}

static uint32_t BridgeInputs(unsigned int bridge)
{
    return (host_gpio_b.ODR >> (12U + bridge * 2U)) & 3U;
}

static bool IsDriving(unsigned int bridge)
{
    uint32_t inputs = BridgeInputs(bridge);
    return inputs == 1U || inputs == 2U;
}

static void AdvanceTicks(uint32_t ticks)
{
    while (ticks-- != 0U) {
        for (unsigned int i = 0U; i < 2U; i++) {
            if (IsDriving(i) && motor.counter < motor.active[i]) {
                motor.high_ticks[i]++;
                if (motor.high_ticks[i] > motor.max_high_ticks[i]) {
                    motor.max_high_ticks[i] = motor.high_ticks[i];
                }
            } else {
                motor.high_ticks[i] = 0U;
            }
        }
        clock_ticks++;
        if (motor.running && ++motor.counter == PERIOD_TICKS) {
            motor.counter = 0U;
            motor.active[0] = motor.shadow[0];
            motor.active[1] = motor.shadow[1];
            motor.update_flag = true;
            motor.updates++;
        }
    }
}

uint32_t BSP_Micros(void)
{
    micros_reads++;
    AdvanceTicks(TICKS_PER_US);
    return (uint32_t)(clock_ticks / TICKS_PER_US) + micros_offset;
}

uint32_t HostPwmGetAutoreload(TIM_HandleTypeDef *timer)
{
    assert(timer == &htim2);
    return PERIOD_TICKS - 1U;
}

uint32_t HostPwmGetCompare(TIM_HandleTypeDef *timer, uint32_t channel)
{
    assert(timer == &htim2);
    return motor.shadow[ChannelIndex(channel)];
}

void HostPwmSetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    if (timer == &htim3) {
        assert(channel == TIM_CHANNEL_1);
        servo_compare = value;
        return;
    }
    assert(timer == &htim2);
    unsigned int bridge = ChannelIndex(channel);
    if (value == PERIOD_TICKS) {
        /* STOP/coast 必须先撤方向再要求 PWM 恒高。 */
        assert(BridgeInputs(bridge) == 0U);
    }
    motor.shadow[bridge] = value;
    motor.writes++;
}

/* BSP 现在用 HAL_TIM_GenerateEvent(UPDATE/UG) 立即提交影子寄存器。 */
HAL_StatusTypeDef HAL_TIM_GenerateEvent(TIM_HandleTypeDef *timer, uint32_t source)
{
    assert(timer == &htim2 && source == TIM_EVENTSOURCE_UPDATE);
    motor.active[0] = motor.shadow[0];
    motor.active[1] = motor.shadow[1];
    motor.counter = 0U;
    motor.update_flag = true;
    motor.ug_events++;
    return HAL_OK;
}

uint32_t HostPwmGetFlag(TIM_HandleTypeDef *timer, uint32_t flag)
{
    assert(timer == &htim2 && flag == TIM_FLAG_UPDATE);
    motor.flag_reads++;
    return motor.update_flag ? TIM_FLAG_UPDATE : RESET;
}

HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)channel;
    if (timer == &htim2) {
        motor.running = true;
    }
    return HAL_OK;
}

void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        port->ODR |= pin;
    } else {
        port->ODR &= ~(uint32_t)pin;
    }
    for (unsigned int i = 0U; i < 2U; i++) {
        /* 不变式：驱动中的桥，其影子寄存器必须已提交到活动寄存器
         * （即 SetBridgeCompare 的提交确实发生在 ApplyBridge 之前）。 */
        assert(!IsDriving(i) || motor.shadow[i] == motor.active[i]);
    }
    AdvanceTicks(1U);
}

GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (port->ODR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pin)
{
    port->ODR ^= pin;
}

void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *init)
{
    /* 只记录 STBY 引脚被配置过；输出电平由 HAL_GPIO_WritePin 维护。 */
    (void)port;
    (void)init;
}

void Error_Handler(void)
{
    error_calls++;
    BSP_Board_EmergencyCoast();
    /* 刻意返回：失败的调用方不得再落地它的方向。 */
}

static void PrepareCoast(uint32_t counter)
{
    memset(&motor, 0, sizeof(motor));
    host_gpio_a.ODR = host_gpio_b.ODR = host_gpio_c.ODR = 0U;
    clock_ticks = 0U;
    micros_offset = micros_reads = error_calls = 0U;
    BSP_Board_Init();
    AdvanceTicks(PERIOD_TICKS);
    assert(servo_compare == CFG_SERVO_CENTER_PULSE_US);
    assert(motor.active[0] == PERIOD_TICKS && motor.active[1] == PERIOD_TICKS);
    motor.counter = counter;
    motor.updates = motor.ug_events = motor.writes = motor.flag_reads = 0U;
    motor.update_flag = true; /* 陈旧 UIF 不得授权新的占空比。 */
}

/* 1) 从 coast 切到任一挡位：必须在**不等待自然 Update**的前提下立刻提交占空比并驱动。
 *    这正是 2 s 周期下最关键的回归点——旧实现会在这里阻塞最长 2 秒。 */
static void TestImmediateCommitWithoutWaiting(void)
{
    const uint8_t duties[] = {(uint8_t)CFG_MOTOR_LOW_DUTY_PERCENT,
                              (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT};
    for (unsigned int d = 0U; d < 2U; d++) {
        PrepareCoast(1U);
        uint32_t updates_before = motor.updates;

        BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, duties[d]);
        assert(motor.active[0] == TICKS_FOR(duties[d]));
        assert(motor.ug_events == 1U && BridgeInputs(0U) == 1U);
        /* 关键：没有靠自然回绕来提交。 */
        assert(motor.updates == updates_before);

        BSP_BridgeB_Set(BSP_BRIDGE_REVERSE, duties[d]);
        assert(motor.active[1] == TICKS_FOR(duties[d]));
        assert(motor.ug_events == 2U && BridgeInputs(1U) == 2U);
        assert(error_calls == 0U);
    }
}

/* 2) 重复设置同一挡位 -> compare 未变 -> early return：不写 CCR、不置 UG。 */
static void TestRedundantSetIsNoOp(void)
{
    PrepareCoast(1U);
    BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    uint32_t writes = motor.writes, ugs = motor.ug_events;
    BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    assert(motor.writes == writes && motor.ug_events == ugs);
}

/* 3) 换向：方向脚翻转，但占空比不变时不应重复写 CCR。 */
static void TestSteadyDirectionReversalAndCoast(void)
{
    PrepareCoast(1U);
    BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    BSP_BridgeB_Set(BSP_BRIDGE_FORWARD, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    uint32_t writes = motor.writes;
    uint32_t ugs = motor.ug_events;

    const BspBridgeState a[] = {BSP_BRIDGE_REVERSE, BSP_BRIDGE_REVERSE,
                                BSP_BRIDGE_FORWARD, BSP_BRIDGE_FORWARD};
    const BspBridgeState b[] = {BSP_BRIDGE_FORWARD, BSP_BRIDGE_REVERSE,
                                BSP_BRIDGE_REVERSE, BSP_BRIDGE_FORWARD};
    for (unsigned int i = 0U; i < 4U; i++) {
        BSP_BridgeA_Set(a[i], (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
        BSP_BridgeB_Set(b[i], (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
        AdvanceTicks(PERIOD_TICKS / 4U);
    }
    /* 占空比全程未变，所以不应有任何多余的 CCR 写或 UG。 */
    assert(motor.writes == writes && motor.ug_events == ugs);

    BSP_BridgeA_Coast();
    BSP_BridgeB_Coast();
    assert(BridgeInputs(0U) == 0U && BridgeInputs(1U) == 0U);
    assert(motor.shadow[0] == PERIOD_TICKS && motor.shadow[1] == PERIOD_TICKS);
}

/* 4) 满速挡 95%：每个周期都必须出现低电平段（开关边沿），
 *    这正是修"TB6612 带不动负载"的核心——恒高无沿会导致高侧驱动无法维持。 */
static void TestFullDutyKeepsSwitchingEdges(void)
{
    PrepareCoast(1U);
    BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    BSP_BridgeB_Set(BSP_BRIDGE_REVERSE, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);

    uint32_t expected = TICKS_FOR(CFG_MOTOR_FULL_DUTY_PERCENT);
    assert(motor.active[0] == expected && motor.active[1] == expected);
    /* 95% < 100%：compare 严格小于周期，PWM 必然在每个周期内翻转一次。 */
    assert(expected < PERIOD_TICKS);
    /* 若曾经误用 100%，这里会等于周期，即恒高无边沿。 */
    assert(expected != PERIOD_TICKS);

    AdvanceTicks(PERIOD_TICKS * 2U);
    /* 连续高电平不应达到整个周期——说明确实存在低电平段。 */
    assert(motor.max_high_ticks[0] < PERIOD_TICKS);
    assert(motor.max_high_ticks[1] < PERIOD_TICKS);
    assert(error_calls == 0U);
}

/* 5) coast 恒为 IN1=IN2=低 + CCR=PERIOD_TICKS，永不产生 brake。 */
static void TestCoastNeverBrakes(void)
{
    PrepareCoast(1U);
    BSP_BridgeA_Set(BSP_BRIDGE_COAST, (uint8_t)CFG_MOTOR_FULL_DUTY_PERCENT);
    BSP_BridgeB_Coast();
    assert(BridgeInputs(0U) == 0U && BridgeInputs(1U) == 0U);
    assert(motor.shadow[0] == PERIOD_TICKS && motor.shadow[1] == PERIOD_TICKS);
    AdvanceTicks(PERIOD_TICKS);
    assert(motor.active[0] == PERIOD_TICKS && motor.active[1] == PERIOD_TICKS);
    assert(BridgeInputs(0U) == 0U && BridgeInputs(1U) == 0U);
}

int main(void)
{
    /* 周期契约：计数频率 × 周期毫秒数 / 1000 必须正好等于 ARR+1，即 2 s。 */
    assert((CFG_APB1_TIMER_CLOCK_HZ / (CFG_MOTOR_TIM_PRESCALER + 1UL)) *
               CFG_MOTOR_PWM_PERIOD_MS / 1000UL ==
           (CFG_MOTOR_TIM_PERIOD + 1UL));
    /* 满速挡必须严格小于 100%，否则慢周期 PWM 会退化成恒高、没有开关边沿。 */
    assert(CFG_MOTOR_FULL_DUTY_PERCENT < 100U);
    assert(CFG_MOTOR_LOW_DUTY_PERCENT < CFG_MOTOR_FULL_DUTY_PERCENT);

    TestImmediateCommitWithoutWaiting();
    TestRedundantSetIsNoOp();
    TestSteadyDirectionReversalAndCoast();
    TestFullDutyKeepsSwitchingEdges();
    TestCoastNeverBrakes();
    puts("bsp_pwm_host_test: PASS (immediate commit, 95% edges, coast, no brake)");
    return 0;
}
