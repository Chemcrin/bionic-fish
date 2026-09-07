/* USER CODE BEGIN Header */
/** @file main.h @brief 与原理图一一对应的唯一引脚定义。 */
/* USER CODE END Header */
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* TB6612：A、B 两桥分别连接 42 两相步进电机的线圈 A、线圈 B。 */
#define TB6612_PWMA_Pin       GPIO_PIN_0
#define TB6612_PWMA_GPIO_Port GPIOA
#define TB6612_PWMB_Pin       GPIO_PIN_1
#define TB6612_PWMB_GPIO_Port GPIOA
#define TB6612_AIN1_Pin       GPIO_PIN_12
#define TB6612_AIN1_GPIO_Port GPIOB
#define TB6612_AIN2_Pin       GPIO_PIN_13
#define TB6612_AIN2_GPIO_Port GPIOB
#define TB6612_BIN1_Pin       GPIO_PIN_14
#define TB6612_BIN1_GPIO_Port GPIOB
#define TB6612_BIN2_Pin       GPIO_PIN_15
#define TB6612_BIN2_GPIO_Port GPIOB

/* 舵机。 */
#define SERVO_SIG_Pin         GPIO_PIN_6
#define SERVO_SIG_GPIO_Port   GPIOA

/* SSD1306：软件 I2C，不启用 STM32 I2C 外设。 */
#define OLED_SCL_Pin          GPIO_PIN_8
#define OLED_SCL_GPIO_Port    GPIOB
#define OLED_SDA_Pin          GPIO_PIN_9
#define OLED_SDA_GPIO_Port    GPIOB

/* JY61P：独立软件 I2C；RX/TX 未接 STM32，绝不分配 UART。 */
#define JY61P_SCL_Pin         GPIO_PIN_6
#define JY61P_SCL_GPIO_Port   GPIOB
#define JY61P_SDA_Pin         GPIO_PIN_7
#define JY61P_SDA_GPIO_Port   GPIOB

/* 按键和运行指示灯。 */
#define KEY1_Pin              GPIO_PIN_0
#define KEY1_GPIO_Port        GPIOB
#define KEY2_Pin              GPIO_PIN_1
#define KEY2_GPIO_Port        GPIOB
#define KEY3_Pin              GPIO_PIN_8
#define KEY3_GPIO_Port        GPIOA
#define RUN_LED_Pin           GPIO_PIN_13
#define RUN_LED_GPIO_Port     GPIOC

void Error_Handler(void);

#ifdef __cplusplus
}
#endif
#endif /* __MAIN_H */
