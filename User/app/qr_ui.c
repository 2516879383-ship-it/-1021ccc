#include "qr_ui.h"
#include "./flow/flow_ui.h"
#include "./gyro/gyro_ui.h"
#include "./lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>
#include "./motor/motor_test_config.h"

static const uint16_t row_y[9] = {8u, 32u, 60u, 92u, 114u, 136u, 158u, 180u, 202u};
static void line(QrUi *u, unsigned i, const char *text)
{
    /* 296 title pixels / Font16.Width(11) = 26 complete characters. */
    unsigned limit = i ? 42u : 26u;
    strncpy(u->target[i], text, limit); u->target[i][limit] = '\0';
}
void QrUi_Init(QrUi *u)
{ memset(u, 0, sizeof(*u)); u->redraw = 1u; }

QrUiAction QrUi_Touch(QrUi *u, int32_t x, int32_t y, uint8_t down)
{
    uint8_t edge = (uint8_t)(down && !u->was_down);
    u->was_down = down;
    if (!edge || x < 8 || x >= 472 || y < 0 || y >= 264) return QR_UI_NONE;
    if (x>=312 && x<392 && y>=4 && y<28) {
        u->gyro_page=1u; u->gyro_detail=0u; u->redraw=1u; return QR_UI_NONE;
    }
    if (x >= 400 && y >= 4 && y < 28) {
        if (u->gyro_page) u->gyro_detail=(uint8_t)!u->gyro_detail;
        else if (u->flow_page) u->flow_detail = (uint8_t)!u->flow_detail;
        else { u->flow_page = 1u; u->flow_detail = 0u; }
        u->redraw = 1u; return QR_UI_NONE;
    }
    if (y >= 232) {
        if (x >= 248) {
            if (u->gyro_page) u->gyro_page=0u;
            else if (u->flow_page) u->flow_page = 0u;
            else u->motor_page = (uint8_t)!u->motor_page;
            u->redraw = 1u;
        }
        else if (x >= 160 && x < 240) return QR_UI_STOP_ALL;
        else if (x < 152) { if (u->motor_page && !u->flow_page && !u->gyro_page) u->redraw = 1u;
            return u->gyro_page ? (x<88 ? QR_UI_GYRO_CAL : QR_UI_GYRO_ZERO) :
            (u->flow_page ? QR_UI_FLOW_CLEAR : (u->motor_page ? QR_UI_MODE : QR_UI_RESCAN)); }
    } else if (u->motor_page && !u->flow_page && !u->gyro_page) {
        if (!u->detail) {
            unsigned i;
            for (i = 1u; i <= 7u; ++i) if (y >= row_y[i] && y < row_y[i] + 18) {
                u->selected = (uint8_t)(i - 1u); u->detail = 1u; u->redraw = 1u; break;
            }
        } else if (y >= 136 && y < 154) {
            u->test_action = (uint8_t)((u->test_action + 1u) % 5u); u->redraw = 1u;
        } else if (y >= 180 && y < 198) return x < 240 ? QR_UI_RUN : QR_UI_STOP_AXIS;
        else if (y >= 202 && y < 220) {
            if (x < 240) return QR_UI_STOP_WHEELS;
            u->detail = 0u; u->redraw = 1u;
        }
    }
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
    line(u, 0u, "XM1605 UART UNVERIFIED");
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
static void counts(char *t, size_t size, int64_t value)
{
    uint64_t v = (uint64_t)(value < 0 ? -value : value);
    snprintf(t, size, "%s%lu", value < 0 ? "-" : "", (unsigned long)v);
}
static void motor_page(QrUi *u, const MotorBus *b, uint32_t now)
{
    static const char *actions[] = {"ENABLE", "SPEED", "POSITION", "WHEELS 0xAA", "SYNC WHEELS"};
    char t[43], value[16], target[16];
    const MotorNode *n = &b->nodes[u->selected];
    const MotorSnapshot *d = &n->data;
    unsigned i;
    line(u, 0u, b->manual ? "MOTOR MANUAL / STOP READY" : "MOTOR READ ONLY");
    if (!u->detail) {
        for (i = 0u; i < MOTOR_COUNT; ++i) {
            n = &b->nodes[i]; d = &n->data;
            if (MotorBus_Fresh(n, MOTOR_VALID_SPEED, now)) snprintf(value, sizeof(value), "%ld", (long)d->speed_rpm);
            else strcpy(value, "--");
            if (MotorBus_Fresh(n, MOTOR_VALID_STATUS, now)) snprintf(target, sizeof(target), "%02X", d->status_flags);
            else strcpy(target, "--");
            snprintf(t, sizeof(t), "%u %-4s %-5s RPM:%s S:%s", (unsigned)d->address, MotorBus_Role(d->address),
                n->stop_unconfirmed ? "STOP?" : d->online ? (MotorBus_Fresh(n, MOTOR_VALID_SPEED, now) && MotorBus_Fresh(n, MOTOR_VALID_STATUS, now) ? "ON" : "STALE") :
                d->rx_count ? "OFF" : "WAIT", value, target);
            line(u, i + 1u, t);
        }
        snprintf(t, sizeof(t), "%.42s", b->message); line(u, 8u, t); return;
    }
    snprintf(t, sizeof(t), "%u %s %s %s", (unsigned)d->address, MotorBus_Role(d->address),
        d->online ? "ON" : "OFF/WAIT", n->stop_unconfirmed ? "STOP UNCONF" : MotorBus_ActionName(n->action)); line(u, 1u, t);
    if (d->valid & MOTOR_VALID_POSITION) counts(value, sizeof(value), d->position_counts); else strcpy(value, "--");
    if (n->target_known) counts(target, sizeof(target), n->target_counts); else strcpy(target, "--");
    snprintf(t, sizeof(t), "P:%s T:%s%s", value, target,
        MotorBus_Fresh(n, MOTOR_VALID_POSITION, now) ? "" : " STALE"); line(u, 2u, t);
    if (MotorBus_Fresh(n, MOTOR_VALID_SPEED, now)) snprintf(value, sizeof(value), "%ld", (long)d->speed_rpm);
    else strcpy(value, "--");
    if (MotorBus_Fresh(n, MOTOR_VALID_STATUS, now)) snprintf(target, sizeof(target), "%02X", d->status_flags);
    else strcpy(target, "--");
    snprintf(t, sizeof(t), "RPM:%s STATUS:%s ERR:%u R:%02X/%02X", value, target, (unsigned)d->last_error,
        (unsigned)d->command_code, (unsigned)d->command_result); line(u, 3u, t);
    snprintf(t, sizeof(t), "AGE p/s/t:%lu/%lu/%lu", (unsigned long)(d->valid & 1u ? now - d->position_ms : 99999u),
        (unsigned long)(d->valid & 2u ? now - d->speed_ms : 99999u),
        (unsigned long)(d->valid & 4u ? now - d->status_ms : 99999u)); line(u, 4u, t);
    snprintf(t, sizeof(t), "ACTION: %s [tap to cycle]", actions[u->test_action]); line(u, 5u, t);
    snprintf(t, sizeof(t), "%.42s", b->message); line(u, 6u, t);
    line(u, 7u, b->manual ? "RUN ONCE                  STOP AXIS" : "MANUAL REQUIRED           STOP AXIS");
    line(u, 8u, "STOP WHEELS               BACK");
}
/* Each step emits one <=480x4 rectangle or <=2 glyphs. A HAL/LCD timeout
   may exceed this target; the gyro still detects and invalidates that gap.
   No service is called here: all modules resume in the common app loop. */
enum { DRAW_IDLE, DRAW_CLEAR, DRAW_ROWS, DRAW_TEXT, DRAW_BUTTONS, DRAW_LABELS };
enum { JOB_MOTOR=1, JOB_FLOW=2, JOB_GYRO=4, JOB_DETAIL=8, JOB_MANUAL=16 };
static void text_step(QrUi *u, uint16_t x, uint16_t y, const char *text, sFONT *font)
{
    char part[3]; unsigned n=0u;
    while (n<2u && text[u->offset+n]) { part[n]=text[u->offset+n]; ++n; }
    part[n]='\0';
    LCD_SetFont(font);
    LCD_DisplayStringAt((uint16_t)(x+u->offset*font->Width),y,(uint8_t *)part,LEFT_MODE);
    u->offset=(uint8_t)(u->offset+n);
}
static void draw_step(QrUi *u)
{
    static const uint16_t bx[5]={160u,8u,248u,312u,400u};
    static const uint16_t by[5]={232u,232u,232u,4u,4u};
    static const uint16_t bw[5]={80u,144u,224u,80u,72u};
    static const uint16_t lx[6]={168u,20u,92u,260u,318u,406u};
    const char *label;
    uint8_t f=u->job_flags;
    if (u->drawing==DRAW_CLEAR) {
        LCD_SetBackColor(LCD_COLOR_BLACK); LCD_SetTextColor(LCD_COLOR_BLACK);
        LCD_FillRect(0u,u->clear_y,480u,4u); u->clear_y+=4u;
        if (u->clear_y>=272u) { u->button=u->strip=0u; u->drawing=DRAW_BUTTONS; }
    } else if (u->drawing==DRAW_ROWS) {
        while (u->row<9u && !strcmp(u->previous[u->row],u->target[u->row])) ++u->row;
        if (u->row==9u) { u->drawing=DRAW_IDLE; return; }
        {
            unsigned height=u->row==2u ? 26u : 18u;
            unsigned h=height-u->strip; if (h>4u) h=4u;
            LCD_SetBackColor(LCD_COLOR_BLACK); LCD_SetTextColor(LCD_COLOR_BLACK);
            LCD_FillRect(8u,(uint16_t)(row_y[u->row]+u->strip),u->row ? 464u : 296u,(uint16_t)h);
            u->strip=(uint8_t)(u->strip+h);
            if (u->strip==height) { u->offset=0u; u->drawing=DRAW_TEXT; }
        }
    } else if (u->drawing==DRAW_TEXT) {
        LCD_SetBackColor(LCD_COLOR_BLACK); LCD_SetTextColor(LCD_COLOR_WHITE);
        if (u->target[u->row][u->offset]) text_step(u,8u,row_y[u->row],u->target[u->row],
            u->row==2u && !(f & (JOB_MOTOR|JOB_FLOW|JOB_GYRO)) ? &Font24 : &Font16);
        if (!u->target[u->row][u->offset]) {
            strcpy(u->previous[u->row],u->target[u->row]); ++u->row;
            u->strip=0u; u->drawing=DRAW_ROWS;
        }
    } else if (u->drawing==DRAW_BUTTONS) {
        LCD_SetTextColor(LCD_COLOR_WHITE);
        LCD_FillRect(bx[u->button],(uint16_t)(by[u->button]+u->strip),bw[u->button],4u);
        u->strip+=4u;
        if (u->strip==(u->button<3u ? 32u : 24u)) {
            u->strip=0u;
            if (++u->button==5u) { u->button=u->offset=0u; u->drawing=DRAW_LABELS; }
        }
    } else if (u->drawing==DRAW_LABELS) {
        switch (u->button) {
        case 0u: label="STOP"; break;
        case 1u: label=(f & JOB_GYRO) ? "CAL" : (f & JOB_FLOW) ? "CLEAR" :
            (f & JOB_MOTOR) ? ((f & JOB_MANUAL) ? "READ ONLY" : "MANUAL") : "RESCAN"; break;
        case 2u: label=(f & JOB_GYRO) ? "ZERO" : ""; break;
        case 3u: label=(f & (JOB_FLOW|JOB_GYRO)) ? "BACK" : (f & JOB_MOTOR) ? "QR" : "MOTOR"; break;
        case 4u: label="GYRO"; break;
        default: label=(f & (JOB_FLOW|JOB_GYRO)) ? ((f & JOB_DETAIL) ? "DATA" : "MORE") : "FLOW"; break;
        }
        LCD_SetBackColor(LCD_COLOR_WHITE); LCD_SetTextColor(LCD_COLOR_BLACK);
        if (label[u->offset]) text_step(u,lx[u->button],u->button<4u ? 240u : 10u,label,&Font16);
        LCD_SetBackColor(LCD_COLOR_BLACK);
        if (!label[u->offset]) {
            u->offset=0u;
            if (++u->button==6u) { u->row=u->strip=0u; u->drawing=DRAW_ROWS; }
        }
    }
}
void QrUi_Process(QrUi *u, const QrService *s, const MotorBus *motor, const FlowSnapshot *flow,
                  const GyroSnapshot *gyro, uint32_t now)
{
    if (u->redraw || (!u->drawing && (uint32_t)(now-u->last_draw)>=100u)) {
        /* Page/layout changes cancel the old job. Ordinary refreshes freeze one
           snapshot, finish it at loop speed, then take the next >=100ms request. */
        if (u->redraw) {
            memset(u->previous,0,sizeof(u->previous)); u->clear_y=0u; u->drawing=DRAW_CLEAR;
        } else { u->row=u->strip=0u; u->drawing=DRAW_ROWS; }
        u->redraw=0u; u->last_draw=now;
        u->job_flags=(uint8_t)((u->motor_page ? JOB_MOTOR : 0u) | (u->flow_page ? JOB_FLOW : 0u) |
            (u->gyro_page ? JOB_GYRO : 0u) | (motor->manual ? JOB_MANUAL : 0u) |
            ((u->gyro_page ? u->gyro_detail : u->flow_detail) ? JOB_DETAIL : 0u));
        if (u->gyro_page || u->flow_page) {
            unsigned i; char t[43];
            for (i=0u;i<9u;++i) {
                if (u->gyro_page) GyroUi_Line(t,i,u->gyro_detail,gyro);
                else FlowUi_Line(t,i,u->flow_detail,flow);
                line(u,i,t);
            }
        } else if (u->motor_page) motor_page(u,motor,now); else qr_page(u,s,now);
    }
    draw_step(u);
}
