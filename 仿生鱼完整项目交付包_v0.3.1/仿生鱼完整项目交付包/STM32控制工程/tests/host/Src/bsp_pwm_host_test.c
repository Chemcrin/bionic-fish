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

/* Model the relevant TIM2 hardware contract, not the BSP's update algorithm:
 * CCR writes change preload only; natural counter rollover commits both CCRs. */
enum { PERIOD_TICKS = 3600, DUTY_TICKS = 360, TICKS_PER_US = 72 };
static struct {
    uint32_t counter, shadow[2], active[2], high_ticks[2], max_high_ticks[2];
    uint32_t updates, clears, writes, flag_reads;
    bool running, update_flag;
} motor;
static uint64_t clock_ticks;
static uint32_t micros_offset, micros_reads, error_calls, pause_on_flag_ticks;
static uint32_t pause_on_second_micros_ticks;
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
    if (micros_reads == 2U && pause_on_second_micros_ticks != 0U) {
        AdvanceTicks(pause_on_second_micros_ticks);
        pause_on_second_micros_ticks = 0U;
    }
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
        /* STOP/coast must remove direction before requesting constant PWM high. */
        assert(BridgeInputs(bridge) == 0U);
    }
    motor.shadow[bridge] = value;
    motor.writes++;
}

void HostPwmClearFlag(TIM_HandleTypeDef *timer, uint32_t flag)
{
    assert(timer == &htim2 && flag == TIM_FLAG_UPDATE);
    motor.update_flag = false;
    motor.clears++;
}

uint32_t HostPwmGetFlag(TIM_HandleTypeDef *timer, uint32_t flag)
{
    assert(timer == &htim2 && flag == TIM_FLAG_UPDATE);
    motor.flag_reads++;
    if (pause_on_flag_ticks != 0U) {
        AdvanceTicks(pause_on_flag_ticks);
        pause_on_flag_ticks = 0U;
    }
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
        /* A stale active 100% compare must never be exposed to a winding. */
        assert(!IsDriving(i) || motor.active[i] <= DUTY_TICKS);
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

void Error_Handler(void)
{
    error_calls++;
    BSP_Board_EmergencyCoast();
    /* Deliberately return: the failed caller must not apply its direction. */
}

static void PrepareCoast(uint32_t counter)
{
    memset(&motor, 0, sizeof(motor));
    host_gpio_a.ODR = host_gpio_b.ODR = host_gpio_c.ODR = 0U;
    clock_ticks = 0U;
    micros_offset = micros_reads = error_calls = pause_on_flag_ticks = 0U;
    pause_on_second_micros_ticks = 0U;
    BSP_Board_Init();
    AdvanceTicks(PERIOD_TICKS);
    assert(servo_compare == CFG_SERVO_CENTER_PULSE_US);
    assert(motor.active[0] == PERIOD_TICKS && motor.active[1] == PERIOD_TICKS);
    motor.counter = counter;
    motor.updates = motor.clears = motor.writes = motor.flag_reads = 0U;
    motor.update_flag = true; /* An old UIF must not authorize the new duty. */
}

static void TestStartupAtEveryCounterPhase(void)
{
    for (uint32_t start = 0U; start < PERIOD_TICKS; start++) {
        PrepareCoast(start);
        uint64_t initial_ticks = clock_ticks;
        BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, 10U);
        assert(motor.active[0] == DUTY_TICKS && motor.updates >= 1U);
        assert(motor.clears == 1U && BridgeInputs(0U) == 1U);
        uint32_t updates_before_b = motor.updates;
        BSP_BridgeB_Set(BSP_BRIDGE_REVERSE, 10U);
        assert(motor.active[1] == DUTY_TICKS && motor.updates > updates_before_b);
        assert(motor.clears == 2U && BridgeInputs(1U) == 2U);
        AdvanceTicks(PERIOD_TICKS * 2U);
        assert(motor.counter == (start + clock_ticks - initial_ticks) % PERIOD_TICKS);
        assert(motor.max_high_ticks[0] <= DUTY_TICKS);
        assert(motor.max_high_ticks[1] <= DUTY_TICKS);
        assert(error_calls == 0U);
    }
}

static void TestSteadyCommutationAndCoast(void)
{
    PrepareCoast(1U);
    BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, 10U);
    BSP_BridgeB_Set(BSP_BRIDGE_FORWARD, 10U);
    uint32_t previous_reads = micros_reads;
    uint32_t previous_clears = motor.clears;
    uint32_t previous_writes = motor.writes;
    const BspBridgeState a[] = {BSP_BRIDGE_REVERSE, BSP_BRIDGE_REVERSE,
                               BSP_BRIDGE_FORWARD, BSP_BRIDGE_FORWARD};
    const BspBridgeState b[] = {BSP_BRIDGE_FORWARD, BSP_BRIDGE_REVERSE,
                               BSP_BRIDGE_REVERSE, BSP_BRIDGE_FORWARD};
    for (unsigned int i = 0U; i < 4U; i++) {
        BSP_BridgeA_Set(a[i], 10U);
        BSP_BridgeB_Set(b[i], 10U);
        AdvanceTicks(PERIOD_TICKS);
    }
    assert(micros_reads == previous_reads && motor.clears == previous_clears);
    assert(motor.writes == previous_writes);
    assert(motor.max_high_ticks[0] <= DUTY_TICKS && motor.max_high_ticks[1] <= DUTY_TICKS);
    BSP_BridgeA_Set(BSP_BRIDGE_COAST, 10U);
    BSP_BridgeB_Coast();
    assert(BridgeInputs(0U) == 0U && BridgeInputs(1U) == 0U);
    assert(motor.shadow[0] == PERIOD_TICKS && motor.shadow[1] == PERIOD_TICKS);
    AdvanceTicks(PERIOD_TICKS);
    assert(motor.active[0] == PERIOD_TICKS && motor.active[1] == PERIOD_TICKS);
}

static void TestFreshUpdateWinsAfterPreemption(void)
{
    for (unsigned int during_time_read = 0U; during_time_read < 2U; during_time_read++) {
        PrepareCoast(1U);
        if (during_time_read != 0U) {
            pause_on_second_micros_ticks = 150U * TICKS_PER_US;
        } else {
            pause_on_flag_ticks = 150U * TICKS_PER_US;
        }
        BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, 10U);
        assert(error_calls == 0U && BridgeInputs(0U) == 1U);
        assert(motor.active[0] == DUTY_TICKS);
    }
}

static void TestStoppedTimerTimesOut(void)
{
    for (unsigned int bridge = 0U; bridge < 2U; bridge++) {
        PrepareCoast(1U);
        motor.running = false;
        micros_offset = UINT32_MAX - 20U; /* Exercise elapsed-time wraparound. */
        uint64_t started_at = clock_ticks;
        if (bridge == 0U) {
            BSP_BridgeA_Set(BSP_BRIDGE_FORWARD, 10U);
        } else {
            BSP_BridgeB_Set(BSP_BRIDGE_REVERSE, 10U);
        }
        assert(error_calls == 1U && motor.updates == 0U);
        assert(BridgeInputs(0U) == 0U && BridgeInputs(1U) == 0U);
        assert(motor.shadow[0] == PERIOD_TICKS && motor.shadow[1] == PERIOD_TICKS);
        assert(clock_ticks - started_at >= 100U * TICKS_PER_US);
        assert(clock_ticks - started_at < 103U * TICKS_PER_US);
    }
}

int main(void)
{
    TestStartupAtEveryCounterPhase();
    TestSteadyCommutationAndCoast();
    TestFreshUpdateWinsAfterPreemption();
    TestStoppedTimerTimesOut();
    puts("bsp_pwm_host_test: PASS (3600 startup phases, steady commutation, coast, timeout)");
    return 0;
}
