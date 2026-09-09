/**
 * @file oled_ui.h
 * @brief 固定内存的 Astra 风格状态页适配层。
 *
 * 该接口与电机控制隔离；它只消费快照。详见 README 对 oled-ui-astra 上游限制的说明。
 */
#ifndef OLED_UI_H
#define OLED_UI_H

#include <stdbool.h>
#include <stdint.h>

#include "app_types.h"
#include "ssd1306.h"

typedef struct {
    Ssd1306 *display;
    uint32_t next_render_ms;
} OledUi;

void OledUi_Init(OledUi *ui, Ssd1306 *display, uint32_t now_ms);
void OledUi_Update(OledUi *ui, uint32_t now_ms, const BfSystemSnapshot *snapshot);
void OledUi_Service(OledUi *ui);

#endif /* OLED_UI_H */
