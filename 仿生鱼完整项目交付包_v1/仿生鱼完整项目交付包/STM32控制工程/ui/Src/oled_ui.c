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

/* BfMove -> 单字母；非法值按停止显示，不谎报在转。 */
static char MoveCode(BfMove move)
{
    switch (move) {
    case BF_MOVE_FORWARD:
        return 'F';
    case BF_MOVE_REVERSE:
        return 'R';
    case BF_MOVE_STOP:
    default:
        return 'S';
    }
}

static void DrawSnapshot(Ssd1306 *display, const BfSystemSnapshot *snapshot)
{
    char line[32];
    char roll[10];
    char pitch[10];
    char yaw[10];

    Ssd1306_Clear(display);
    (void)snprintf(line, sizeof(line), "M1:%c M2:%c",
                   MoveCode(snapshot->motor_a), MoveCode(snapshot->motor_b));
    Ssd1306_DrawString(display, 0U, 0U, line);
    (void)snprintf(line, sizeof(line), "PA:%u PB:%u",
                   (snapshot->motor_a != BF_MOVE_STOP)
                       ? (unsigned int)CFG_MOTOR_A_DUTY_PERCENT : 0U,
                   (snapshot->motor_b != BF_MOVE_STOP)
                       ? (unsigned int)CFG_MOTOR_B_DUTY_PERCENT : 0U);
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
    /* y=56 原为静态标题 "ASTRA STATIC UI"（不携带任何信息、白占 8px 字体空间）。
     * 现在改放 STA 地址：同网段的手机/PC 直接开 http://<ip>:9000 就能用网页控制。 */
    (void)snprintf(line, sizeof(line), "WEB %s:9000", snapshot->sta_ip);
    Ssd1306_DrawString(display, 0U, 56U, line);
}

void OledUi_Init(OledUi *ui, Ssd1306 *display, uint32_t now_ms)
{
    if (ui == 0) {
        return;
    }
    memset(ui, 0, sizeof(*ui));
    ui->display = display;
    ui->next_render_ms = now_ms;
}

void OledUi_Update(OledUi *ui, uint32_t now_ms, const BfSystemSnapshot *snapshot)
{
    if ((ui == 0) || (ui->display == 0) || !ui->display->initialized || (snapshot == 0) ||
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
    if ((ui != 0) && (ui->display != 0)) {
        Ssd1306_Service(ui->display);
    }
}
