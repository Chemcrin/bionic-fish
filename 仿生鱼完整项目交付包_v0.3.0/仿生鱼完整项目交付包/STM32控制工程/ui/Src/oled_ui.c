#include "oled_ui.h"

#include <stdio.h>
#include <string.h>

#include "app_config.h"

static void DdegToText(int16_t ddeg, char *out, size_t capacity)
{
    long value = (long)ddeg;
    unsigned long magnitude = (value < 0L) ? (unsigned long)(-value) : (unsigned long)value;
    (void)snprintf(out, capacity, "%c%lu.%01lu", (value < 0L) ? '-' : '+',
                   magnitude / 10UL, magnitude % 10UL);
}

static void DrawSnapshot(Ssd1306 *display, const BfSystemSnapshot *snapshot)
{
    char line[24];
    char roll[10];
    char pitch[10];
    char yaw[10];

    Ssd1306_Clear(display);
    (void)snprintf(line, sizeof(line), "STEP:%s T:%3u", snapshot->step_running ? "ON" : "OFF",
                   (unsigned int)snapshot->step_target_rpm);
    Ssd1306_DrawString(display, 0U, 0U, line);
    (void)snprintf(line, sizeof(line), "EST:%3u ACT:NA",
                   (unsigned int)snapshot->step_commanded_rpm);
    Ssd1306_DrawString(display, 0U, 10U, line);
    (void)snprintf(line, sizeof(line), "SER:%+d LNK:%u", (int)snapshot->servo_deg,
                   snapshot->command_link_alive ? 1U : 0U);
    Ssd1306_DrawString(display, 0U, 20U, line);
    if (snapshot->attitude.valid) {
        DdegToText(snapshot->attitude.roll_ddeg, roll, sizeof(roll));
        DdegToText(snapshot->attitude.pitch_ddeg, pitch, sizeof(pitch));
        DdegToText(snapshot->attitude.yaw_ddeg, yaw, sizeof(yaw));
        (void)snprintf(line, sizeof(line), "R:%s P:%s", roll, pitch);
        Ssd1306_DrawString(display, 0U, 32U, line);
        (void)snprintf(line, sizeof(line), "Y:%s E:%03lX", yaw,
                       (unsigned long)(snapshot->active_faults & 0xFFFUL));
    } else {
        Ssd1306_DrawString(display, 0U, 32U, "R:--- P:---");
        (void)snprintf(line, sizeof(line), "Y:--- E:%03lX",
                       (unsigned long)(snapshot->active_faults & 0xFFFUL));
    }
    Ssd1306_DrawString(display, 0U, 44U, line);
    Ssd1306_DrawString(display, 0U, 56U, "ASTRA STATIC UI");
}

void OledUi_Init(OledUi *ui, Ssd1306 *display, uint32_t now_ms)
{
    if (ui == 0) {
        return;
    }
    memset(ui, 0, sizeof(*ui));
    ui->display = display;
    ui->display_available = (display != 0) && display->initialized;
    ui->next_render_ms = now_ms;
}

void OledUi_Update(OledUi *ui, uint32_t now_ms, const BfSystemSnapshot *snapshot)
{
    if ((ui == 0) || !ui->display_available || (snapshot == 0) ||
        ((int32_t)(now_ms - ui->next_render_ms) < 0) || !Ssd1306_IsIdle(ui->display)) {
        return;
    }
    DrawSnapshot(ui->display, snapshot);
    if (Ssd1306_BeginRefresh(ui->display)) {
        ui->next_render_ms = now_ms + CFG_OLED_FRAME_PERIOD_MS;
    }
}

void OledUi_Service(OledUi *ui)
{
    if ((ui != 0) && ui->display_available) {
        Ssd1306_Service(ui->display);
    }
}
