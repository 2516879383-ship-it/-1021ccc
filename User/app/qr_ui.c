#include "qr_ui.h"
#include "./lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>

static const uint16_t row_y[9] = {8u, 32u, 60u, 92u, 114u, 136u, 158u, 180u, 202u};
static void line(QrUi *u, unsigned i, const char *text)
{
    if (!u->redraw && strcmp(u->previous[i], text) == 0) return;
    LCD_SetTextColor(LCD_COLOR_BLACK);
    LCD_FillRect(8u, row_y[i], 464u, i == 2u ? 26u : 18u);
    LCD_SetFont(i == 2u && !u->motor_page ? &Font24 : &Font16);
    LCD_SetTextColor(LCD_COLOR_WHITE);
    LCD_DisplayStringAt(8u, row_y[i], (uint8_t *)text, LEFT_MODE);
    strncpy(u->previous[i], text, 42u); u->previous[i][42] = '\0';
}
static void buttons(uint8_t motor)
{
    LCD_SetFont(&Font16); LCD_SetTextColor(LCD_COLOR_WHITE);
    LCD_FillRect(8u, 232u, 224u, 32u); LCD_FillRect(248u, 232u, 224u, 32u);
    LCD_SetTextColor(LCD_COLOR_BLACK); LCD_SetBackColor(LCD_COLOR_WHITE);
    LCD_DisplayStringAt(20u, 240u, (uint8_t *)(motor ? "READ ONLY" : "RESCAN"), LEFT_MODE);
    LCD_DisplayStringAt(260u, 240u, (uint8_t *)(motor ? "QR" : "MOTOR"), LEFT_MODE);
    LCD_SetBackColor(LCD_COLOR_BLACK);
}
void QrUi_Init(QrUi *u)
{ memset(u, 0, sizeof(*u)); u->redraw = 1u; }

QrUiAction QrUi_Touch(QrUi *u, int32_t x, int32_t y, uint8_t down)
{
    uint8_t edge = (uint8_t)(down && !u->was_down);
    u->was_down = down;
    if (!edge || y < 232 || y >= 264) return QR_UI_NONE;
    if (x >= 248 && x < 472) { u->motor_page = (uint8_t)!u->motor_page; u->redraw = 1u; }
    else if (!u->motor_page && x >= 8 && x < 232) { u->redraw = 1u; return QR_UI_RESCAN; }
    return QR_UI_NONE;
}

static const char *error(const QrSnapshot *d)
{
    switch (d->last_error) {
    case QR_ERROR_NONE: return "NONE";
    case QR_ERROR_INVALID: return "INVALID CODE";
    case QR_ERROR_FRAME_TIMEOUT: return "PARTIAL TIMEOUT";
    case QR_ERROR_OVERFLOW: return "RX OVERFLOW";
    case QR_ERROR_UART: return "UART ERROR";
    case QR_ERROR_LOCKED: return "IGNORED / LOCKED";
    default: return "UNKNOWN";
    }
}
static void qr_page(QrUi *u, const QrService *s, uint32_t now)
{
    char t[43];
    const QrSnapshot *d = &s->data;
    line(u, 0u, "XM1605 - UART config unverified");
    snprintf(t, sizeof(t), "S%lu %s", (unsigned long)d->session, QrService_StateName(s)); line(u, 1u, t);
    line(u, 2u, d->valid ? d->current.text : "--");
    if (d->valid) snprintf(t, sizeof(t), "ACCEPT:%lu AT:%lums", (unsigned long)d->accepted_seq,
                          (unsigned long)d->accepted_ms);
    else snprintf(t, sizeof(t), "WAIT:%lus%s", (unsigned long)((now - d->session_ms) / 1000u),
                  s->stream.session_sync ? " SYNC BOUNDARY" : "");
    line(u, 3u, t);
    snprintf(t, sizeof(t), "ERR: %s%s%s", error(d), d->last_error == QR_ERROR_INVALID ? " / " : "",
             d->last_error == QR_ERROR_INVALID ? QrTaskCode_ErrorName(d->code_error) : ""); line(u, 4u, t);
    snprintf(t, sizeof(t), "RX:%lu REC:%lu", (unsigned long)d->raw_bytes, (unsigned long)d->records); line(u, 5u, t);
    snprintf(t, sizeof(t), "BAD:%lu PART:%lu", (unsigned long)d->invalid_records,
             (unsigned long)d->frame_timeouts); line(u, 6u, t);
    snprintf(t, sizeof(t), "OVF:%lu UART:%lu", (unsigned long)d->overflows, (unsigned long)d->uart_errors); line(u, 7u, t);
    snprintf(t, sizeof(t), "DUP:%lu IGN:%lu RX:%s", (unsigned long)d->duplicates,
             (unsigned long)d->ignored, d->uart_ready ? "OK" : "ERR"); line(u, 8u, t);
}
static void motor_page(QrUi *u, const MotorSnapshot *d, uint32_t now)
{
    char t[43];
    int64_t a;
    line(u, 0u, "MOTOR READ ONLY - DEMO DISABLED");
    snprintf(t, sizeof(t), "ADDR:%u UART5 115200 8N1", (unsigned)d->address); line(u, 1u, t);
    snprintf(t, sizeof(t), "LINK: %s", d->online ? "ONLINE" : "OFFLINE / WAITING"); line(u, 2u, t);
    if (d->valid & MOTOR_VALID_POSITION) {
        a = d->position_counts * 36000 / 65536;
        snprintf(t, sizeof(t), "POS:%s%ld.%02lu deg%s", a < 0 ? "-" : "",
                 (long)((a < 0 ? -a : a) / 100), (unsigned long)((a < 0 ? -a : a) % 100),
                 (uint32_t)(now - d->position_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "");
    } else strcpy(t, "POS:-- deg");
    line(u, 3u, t);
    if (d->valid & MOTOR_VALID_SPEED) snprintf(t, sizeof(t), "SPEED:%ld RPM%s", (long)d->speed_rpm,
        (uint32_t)(now - d->speed_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "");
    else strcpy(t, "SPEED:-- RPM");
    line(u, 4u, t);
    if (d->valid & MOTOR_VALID_STATUS) snprintf(t, sizeof(t), "ENABLE:%u IN POS:%u%s",
        (unsigned)(d->status_flags & 1u), (unsigned)((d->status_flags >> 1) & 1u),
        (uint32_t)(now - d->status_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "");
    else strcpy(t, "ENABLE:-- IN POS:--");
    line(u, 5u, t);
    if (d->valid & MOTOR_VALID_STATUS) snprintf(t, sizeof(t), "STALL:%u PROTECT:%u%s",
        (unsigned)((d->status_flags >> 2) & 1u), (unsigned)((d->status_flags >> 3) & 1u),
        (uint32_t)(now - d->status_ms) > MOTOR_DATA_STALE_MS ? " STALE" : "");
    else strcpy(t, "STALL:-- PROTECT:--");
    line(u, 6u, t);
    snprintf(t, sizeof(t), "RX:%lu TX:%lu BAD:%lu", (unsigned long)d->rx_count,
             (unsigned long)d->tx_count, (unsigned long)d->bad_frames); line(u, 7u, t);
    snprintf(t, sizeof(t), "ERR:%u TIMEOUT:%lu", (unsigned)d->last_error, (unsigned long)d->timeouts); line(u, 8u, t);
}
void QrUi_Process(QrUi *u, const QrService *s, const MotorSnapshot *motor, uint32_t now)
{
    if (!u->redraw && (uint32_t)(now - u->last_draw) < 100u) return;
    u->last_draw = now;
    if (u->redraw) {
        LCD_SetBackColor(LCD_COLOR_BLACK); LCD_Clear(LCD_COLOR_BLACK); buttons(u->motor_page);
    }
    if (u->motor_page) motor_page(u, motor, now); else qr_page(u, s, now);
    u->redraw = 0u;
}
