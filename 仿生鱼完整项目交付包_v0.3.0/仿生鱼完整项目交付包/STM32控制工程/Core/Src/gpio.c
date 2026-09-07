#include "gpio.h"

#include "main.h"

void MX_GPIO_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /* 先给安全电平，再切为输出，避免上电毛刺驱动 TB6612。 */
    HAL_GPIO_WritePin(GPIOB, TB6612_AIN1_Pin | TB6612_AIN2_Pin |
                             TB6612_BIN1_Pin | TB6612_BIN2_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOC, RUN_LED_Pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(GPIOB, OLED_SCL_Pin | OLED_SDA_Pin | JY61P_SCL_Pin | JY61P_SDA_Pin,
                      GPIO_PIN_SET);

    GPIO_InitStruct.Pin = TB6612_AIN1_Pin | TB6612_AIN2_Pin |
                          TB6612_BIN1_Pin | TB6612_BIN2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = RUN_LED_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* 两条软件 I2C 均为开漏，外部上拉必须接 3.3 V。绝不可配置为推挽 5 V。 */
    GPIO_InitStruct.Pin = OLED_SCL_Pin | OLED_SDA_Pin | JY61P_SCL_Pin | JY61P_SDA_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* 补充原理图确认按键为低电平有效；使用内部上拉。 */
    GPIO_InitStruct.Pin = KEY1_Pin | KEY2_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    GPIO_InitStruct.Pin = KEY3_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}
