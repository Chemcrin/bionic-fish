/** @file ssd1306.h @brief SSD1306 128x64、固定帧缓冲、按小块刷新的驱动。 */
#ifndef SSD1306_H
#define SSD1306_H

#include <stdbool.h>
#include <stdint.h>

#include "soft_i2c.h"

#define SSD1306_WIDTH  128U
#define SSD1306_HEIGHT 64U
#define SSD1306_PAGES  (SSD1306_HEIGHT / 8U)
#define SSD1306_BUFFER_BYTES (SSD1306_WIDTH * SSD1306_PAGES)

typedef struct {
    SoftI2cBus *bus;
    uint8_t address_7bit;
    uint8_t framebuffer[SSD1306_BUFFER_BYTES];
    uint8_t page;
    uint8_t column;
    bool page_address_pending;
    bool refresh_active;
    bool initialized;
    bool i2c_error_active;
    uint32_t i2c_failures;
} Ssd1306;

bool Ssd1306_Init(Ssd1306 *display, SoftI2cBus *bus, uint8_t address_7bit);
void Ssd1306_Clear(Ssd1306 *display);
void Ssd1306_DrawPixel(Ssd1306 *display, uint8_t x, uint8_t y, bool on);
void Ssd1306_DrawChar(Ssd1306 *display, uint8_t x, uint8_t y, char character);
void Ssd1306_DrawString(Ssd1306 *display, uint8_t x, uint8_t y, const char *text);
bool Ssd1306_BeginRefresh(Ssd1306 *display);
void Ssd1306_Service(Ssd1306 *display);
bool Ssd1306_IsIdle(const Ssd1306 *display);
uint32_t Ssd1306_I2cFailureCount(const Ssd1306 *display);
bool Ssd1306_I2cErrorActive(const Ssd1306 *display);

#endif /* SSD1306_H */
