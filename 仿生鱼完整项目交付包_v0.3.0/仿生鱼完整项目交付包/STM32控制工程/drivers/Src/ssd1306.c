#include "ssd1306.h"

#include <string.h>

#include "app_config.h"

/* 固定 5x7 ASCII 子集：状态页不使用中文点阵或动态字体，避免占用 F103C8 RAM。
 * 字符串只需使用此表中的大写字母、数字与标点；其余字符显示为 '?'。 */
static const char kGlyphCharacters[] = " 0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ:+-.%?/";
static const uint8_t kGlyphColumns[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* space */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 0 */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 1 */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 2 */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 3 */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 4 */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 5 */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 6 */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 7 */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 8 */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 9 */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* A */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* B */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* C */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* D */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* E */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* F */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* G */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* H */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* I */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* J */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* K */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* L */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* M */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* N */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* O */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* P */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* Q */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* R */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* S */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* T */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* U */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* V */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* W */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* X */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* Y */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* Z */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* : */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* + */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* - */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* . */
    {0x62, 0x64, 0x08, 0x13, 0x23}, /* % */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* ? */
    {0x20, 0x10, 0x08, 0x04, 0x02}  /* / */
};

static const uint8_t *FindGlyph(char character)
{
    uint16_t index;
    for (index = 0U; index < (sizeof(kGlyphCharacters) - 1U); index++) {
        if (kGlyphCharacters[index] == character) {
            return kGlyphColumns[index];
        }
    }
    /* '?' 位于倒数第二项。 */
    return kGlyphColumns[(sizeof(kGlyphCharacters) - 1U) - 2U];
}

static bool SendCommands(Ssd1306 *display, const uint8_t *commands, uint16_t length)
{
    uint8_t packet[4];
    SoftI2cStatus status;
    uint16_t index;

    /* SSD1306 I2C 每次控制字节 0x00 后可跟多条命令。本函数的小批量逻辑可
     * 兼容初始化和页/列定位，避免使用没有错误返回的第三方 I2C 回调。 */
    if (length <= 3U) {
        packet[0] = 0x00U;
        for (index = 0U; index < length; index++) {
            packet[index + 1U] = commands[index];
        }
        status = SoftI2c_Write(display->bus, display->address_7bit, packet, (uint16_t)(length + 1U));
    } else {
        /* 初始化序列不应切成单字节，最多 28 字节，使用一个局部固定数组。 */
        uint8_t init_packet[32];
        if (length > (sizeof(init_packet) - 1U)) {
            return false;
        }
        init_packet[0] = 0x00U;
        memcpy(&init_packet[1], commands, length);
        status = SoftI2c_Write(display->bus, display->address_7bit, init_packet,
                               (uint16_t)(length + 1U));
    }
    if (status != SOFT_I2C_OK) {
        display->i2c_failures++;
        display->i2c_error_active = true;
        return false;
    }
    return true;
}

bool Ssd1306_Init(Ssd1306 *display, SoftI2cBus *bus, uint8_t address_7bit)
{
    /* 128x64 SSD1306、page addressing mode。地址与面板方向均待实物确认。 */
    static const uint8_t kInitSequence[] = {
        0xAEU, 0xD5U, 0x80U, 0xA8U, 0x3FU, 0xD3U, 0x00U, 0x40U,
        0x8DU, 0x14U, 0x20U, 0x02U, 0xA1U, 0xC8U, 0xDAU, 0x12U,
        0x81U, 0x7FU, 0xD9U, 0xF1U, 0xDBU, 0x40U, 0xA4U, 0xA6U, 0xAFU
    };

    if ((display == 0) || (bus == 0) || (address_7bit > 0x7FU)) {
        return false;
    }
    memset(display, 0, sizeof(*display));
    display->bus = bus;
    display->address_7bit = address_7bit;
    if (SoftI2c_Probe(bus, address_7bit) != SOFT_I2C_OK) {
        display->i2c_failures++;
        display->i2c_error_active = true;
        return false;
    }
    if (!SendCommands(display, kInitSequence, sizeof(kInitSequence))) {
        return false;
    }
    display->initialized = true;
    Ssd1306_Clear(display);
    return Ssd1306_BeginRefresh(display);
}

void Ssd1306_Clear(Ssd1306 *display)
{
    if (display != 0) {
        memset(display->framebuffer, 0, sizeof(display->framebuffer));
    }
}

void Ssd1306_DrawPixel(Ssd1306 *display, uint8_t x, uint8_t y, bool on)
{
    uint16_t offset;
    uint8_t mask;
    if ((display == 0) || (x >= SSD1306_WIDTH) || (y >= SSD1306_HEIGHT)) {
        return;
    }
    offset = (uint16_t)x + ((uint16_t)(y >> 3U) * SSD1306_WIDTH);
    mask = (uint8_t)(1U << (y & 0x07U));
    if (on) {
        display->framebuffer[offset] |= mask;
    } else {
        display->framebuffer[offset] &= (uint8_t)~mask;
    }
}

void Ssd1306_DrawChar(Ssd1306 *display, uint8_t x, uint8_t y, char character)
{
    const uint8_t *glyph = FindGlyph(character);
    uint8_t column;
    uint8_t row;
    for (column = 0U; column < 5U; column++) {
        for (row = 0U; row < 7U; row++) {
            Ssd1306_DrawPixel(display, (uint8_t)(x + column), (uint8_t)(y + row),
                              (glyph[column] & (uint8_t)(1U << row)) != 0U);
        }
    }
}

void Ssd1306_DrawString(Ssd1306 *display, uint8_t x, uint8_t y, const char *text)
{
    if ((display == 0) || (text == 0)) {
        return;
    }
    while ((*text != '\0') && ((uint16_t)x + 5U < SSD1306_WIDTH)) {
        Ssd1306_DrawChar(display, x, y, *text);
        x = (uint8_t)(x + 6U);
        text++;
    }
}

bool Ssd1306_BeginRefresh(Ssd1306 *display)
{
    if ((display == 0) || !display->initialized || display->refresh_active) {
        return false;
    }
    display->page = 0U;
    display->column = 0U;
    display->page_address_pending = true;
    display->refresh_active = true;
    return true;
}

void Ssd1306_Service(Ssd1306 *display)
{
    uint8_t commands[3];
    uint8_t packet[1U + CFG_OLED_CHUNK_BYTES];
    uint8_t chunk;
    SoftI2cStatus status;

    if ((display == 0) || !display->initialized || !display->refresh_active) {
        return;
    }
    if (display->page >= SSD1306_PAGES) {
        display->refresh_active = false;
        /* 一整帧的页地址/数据都成功才解除活动 I2C 故障；单次成功命令不足以
         * 证明总线已恢复。累计失败计数仍保留供调试。 */
        display->i2c_error_active = false;
        return;
    }
    if (display->page_address_pending) {
        commands[0] = (uint8_t)(0xB0U | display->page);
        commands[1] = (uint8_t)(display->column & 0x0FU);
        commands[2] = (uint8_t)(0x10U | (display->column >> 4U));
        if (!SendCommands(display, commands, sizeof(commands))) {
            display->refresh_active = false;
            return;
        }
        display->page_address_pending = false;
        return; /* 一个 service 只做一项 I2C 工作，给控制循环让路。 */
    }

    chunk = CFG_OLED_CHUNK_BYTES;
    if ((uint16_t)display->column + chunk > SSD1306_WIDTH) {
        chunk = (uint8_t)(SSD1306_WIDTH - display->column);
    }
    packet[0] = 0x40U; /* data control byte */
    memcpy(&packet[1], &display->framebuffer[(uint16_t)display->page * SSD1306_WIDTH + display->column],
           chunk);
    status = SoftI2c_Write(display->bus, display->address_7bit, packet, (uint16_t)(chunk + 1U));
    if (status != SOFT_I2C_OK) {
        display->i2c_failures++;
        display->i2c_error_active = true;
        display->refresh_active = false;
        return;
    }

    display->column = (uint8_t)(display->column + chunk);
    if (display->column >= SSD1306_WIDTH) {
        display->page++;
        display->column = 0U;
        display->page_address_pending = true;
    }
}

bool Ssd1306_IsIdle(const Ssd1306 *display)
{
    return (display != 0) && !display->refresh_active;
}

uint32_t Ssd1306_I2cFailureCount(const Ssd1306 *display)
{
    return (display == 0) ? 0U : display->i2c_failures;
}

bool Ssd1306_I2cErrorActive(const Ssd1306 *display)
{
    return (display != 0) && display->i2c_error_active;
}
