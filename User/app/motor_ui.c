#include "motor_ui.h"
#include "app_runtime.h"
#include "./lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>

#define UI_ROWS 12u
#define UI_COLS 59u
static char previous[UI_ROWS][UI_COLS + 1u];
static uint32_t last_draw;
static uint8_t first_draw;

static void row(uint32_t index, const char *text)
{
    if (strcmp(previous[index], text) == 0) return;
    LCD_SetTextColor(LCD_COLOR_BLACK);
    LCD_FillRect(8u, (uint16_t)(8u + index * 21u), 464u, 18u);
    LCD_SetTextColor(LCD_COLOR_WHITE);
    LCD_DisplayStringAt(8u, (uint16_t)(8u + index * 21u), (uint8_t *)text, LEFT_MODE);
    strncpy(previous[index], text, UI_COLS);
    previous[index][UI_COLS] = '\0';
}

static const char *error_name(MotorError error)
{
    switch (error) {
    case MOTOR_ERROR_NONE: return "NONE";
    case MOTOR_ERROR_TIMEOUT: return "RX TIMEOUT";
    case MOTOR_ERROR_REJECTED: return "MOTOR REJECTED";
    case MOTOR_ERROR_FORMAT: return "FRAME/FORMAT";
    case MOTOR_ERROR_RX_OVERFLOW: return "RX OVERFLOW";
    case MOTOR_ERROR_UART: return "UART ERROR";
    case MOTOR_ERROR_MOTION_TIMEOUT: return "MOTION TIMEOUT";
    case MOTOR_ERROR_STALL: return "STALL/PROTECT";
    default: return "UNKNOWN";
    }
}

void MotorUi_Init(void)
{
    memset(previous, 0, sizeof(previous));
    first_draw = 1u;
    LCD_SetFont(&Font16);
    LCD_SetBackColor(LCD_COLOR_BLACK);
    LCD_Clear(LCD_COLOR_BLACK);
}

void MotorUi_Process(const MotorSnapshot *d, const char *stage, uint32_t now_ms)
{
    char text[UI_COLS + 1u];
    int32_t x, y;
    int64_t angle;
    uint8_t pressed;
    const char *stale;
    if (!first_draw && (uint32_t)(now_ms - last_draw) < 100u) return;
    first_draw = 0u; last_draw = now_ms;
    row(0u, "X42S Emm - Motor telemetry");
    snprintf(text, sizeof(text), "ADDR %u   UART5 115200 8N1", (unsigned)d->address);
    row(1u, text);
    snprintf(text, sizeof(text), "DEMO: %s", stage); row(2u, text);
    snprintf(text, sizeof(text), "LINK: %s", d->online ? "ONLINE" : "OFFLINE / WAITING");
    row(3u, text);
    if (d->valid & MOTOR_VALID_POSITION) {
        angle = d->position_counts * 36000 / 65536;
        stale = (uint32_t)(now_ms - d->position_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "";
        snprintf(text, sizeof(text), "POS: %s%ld.%02lu deg%s", angle < 0 ? "-" : "",
                 (long)((angle < 0 ? -angle : angle) / 100),
                 (unsigned long)((angle < 0 ? -angle : angle) % 100), stale);
    } else strcpy(text, "POS: -- deg");
    row(4u, text);
    if (d->valid & MOTOR_VALID_SPEED) {
        snprintf(text, sizeof(text), "SPEED: %ld RPM%s", (long)d->speed_rpm,
                 (uint32_t)(now_ms - d->speed_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "");
    } else strcpy(text, "SPEED: -- RPM");
    row(5u, text);
    if (d->valid & MOTOR_VALID_STATUS) {
        stale = (uint32_t)(now_ms - d->status_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "";
        snprintf(text, sizeof(text), "ENABLE: %u  IN POS: %u%s",
                 (unsigned)(d->status_flags & 1u), (unsigned)((d->status_flags >> 1) & 1u), stale);
        row(6u, text);
        snprintf(text, sizeof(text), "STALL: %u  PROTECT: %u%s",
                 (unsigned)((d->status_flags >> 2) & 1u),
                 (unsigned)((d->status_flags >> 3) & 1u), stale);
    } else { row(6u, "ENABLE: --  IN POS: --"); strcpy(text, "STALL: --  PROTECT: --"); }
    row(7u, text);
    snprintf(text, sizeof(text), "RX:%lu TX:%lu BAD:%lu", (unsigned long)d->rx_count,
             (unsigned long)d->tx_count, (unsigned long)d->bad_frames); row(8u, text);
    snprintf(text, sizeof(text), "ERROR: %s ACK:%02X", error_name(d->last_error),
             (unsigned)d->command_result); row(9u, text);
    if (d->rx_count) snprintf(text, sizeof(text), "UP:%lus RX AGE:%lums TIMEOUT:%lu",
        (unsigned long)(now_ms / 1000u), (unsigned long)(now_ms - d->last_rx_ms),
        (unsigned long)d->timeouts);
    else snprintf(text, sizeof(text), "UP:%lus RX AGE:--", (unsigned long)(now_ms / 1000u));
    row(10u, text);
    AppTouch_GetState(&x, &y, &pressed);
    snprintf(text, sizeof(text), "TOUCH: %ld,%ld %s (status only)", (long)x, (long)y,
             pressed ? "DOWN" : "UP"); row(11u, text);
}
