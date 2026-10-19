#include "app/motor_ui.h"
#include "app/app_runtime.h"
#include "lcd/bsp_lcd.h"
#include <stdio.h>
#include <string.h>

static char displayed[12][64];
static unsigned draws, bad_layout;
static sFONT *active_font;
void LCD_SetFont(sFONT *font) { active_font=font; }
void LCD_SetBackColor(uint32_t color) { (void)color; }
void LCD_SetTextColor(uint32_t color) { (void)color; }
void LCD_Clear(uint32_t color) { (void)color; memset(displayed,0,sizeof(displayed)); }
void LCD_FillRect(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{ if ((unsigned)x+width>480u || (unsigned)y+height>272u) ++bad_layout; }
void LCD_DisplayStringAt(uint16_t x, uint16_t y, uint8_t *text, unsigned mode)
{
    unsigned row=(y-8u)/21u;
    (void)mode; ++draws;
    if(row>=12 || !active_font || (unsigned)x+strlen((char *)text)*active_font->Width>480u ||
       (unsigned)y+active_font->Height>272u) {
        ++bad_layout; return;
    }
    strcpy(displayed[row],(char *)text);
}
void AppTouch_GetState(int32_t *x, int32_t *y, uint8_t *pressed)
{ *x=479; *y=271; *pressed=1; }

int TestMotorUi(void)
{
    MotorSnapshot d={0};
    unsigned previous_draws;
    d.address=1;
    MotorUi_Init(); MotorUi_Process(&d,"BOOT WAIT",0);
    if(draws!=12 || !strstr(displayed[4],"--") || !strstr(displayed[9],"RESULT:--")) return 0;
    previous_draws=draws;
    MotorUi_Process(&d,"BOOT WAIT",50);
    if(draws!=previous_draws) return 0;
    MotorUi_Process(&d,"BOOT WAIT",100);
    if(draws!=previous_draws) return 0; /* unchanged lines are not redrawn */
    d.valid=7; d.online=1; d.position_counts=-92; d.speed_rpm=-1000;
    d.status_flags=0x8B; d.position_ms=d.speed_ms=d.status_ms=d.last_rx_ms=200;
    d.rx_count=3; d.command_seq=1; d.command_result=2;
    MotorUi_Process(&d,"WAIT POSITION / 32000 PULSES",200);
    if(!strstr(displayed[4],"-0.50") || !strstr(displayed[5],"-1000")) return 0;
    if(!strstr(displayed[6],"ENABLE: 1  IN POS: 1") || !strstr(displayed[7],"PROTECT: 1")) return 0;
    d.online=0; MotorUi_Process(&d,"FAULT / STOP UNCONFIRMED",1000);
    if(!strstr(displayed[3],"OFFLINE") || !strstr(displayed[4],"STALE") || !strstr(displayed[5],"STALE")) return 0;
    d.position_counts=-INT64_C(4294967295); d.rx_count=UINT32_MAX;
    d.tx_count=d.bad_frames=d.timeouts=UINT32_MAX;
    MotorUi_Process(&d,"FAULT / STOP UNCONFIRMED",UINT32_MAX);
    if(bad_layout) return 0;
    puts("PASS: UI no-data, negative values, stale data, partial redraw and 480x272 bounds");
    return 1;
}
