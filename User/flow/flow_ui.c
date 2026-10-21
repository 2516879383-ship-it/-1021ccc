#include "flow_ui.h"
#include <stdio.h>
#include <string.h>
void FlowUi_Line(char t[43],unsigned row,uint8_t detail,const FlowSnapshot *d)
{
    char count[21]; t[0]='\0';
    if (!d) { if (row==0u) strcpy(t,"FLOW unavailable"); return; }
    if (row==0u) { strcpy(t,detail ? "FLOW diagnostics" : "PMW3901 - counts"); return; }
    if (detail) {
        switch (row) {
        case 1u: snprintf(t,43,"%s C:%u T:%u F:%u",Flow_StateName(d),d->comm_ok,d->tracking,d->fresh); break;
        case 2u: snprintf(t,43,"SEQ:%lu ZERO:%lu",(unsigned long)d->sequence,(unsigned long)d->zero_sequence); break;
        case 3u: snprintf(t,43,"ERR: %s",Flow_ErrorName(d->error)); break;
        case 4u: snprintf(t,43,"FAULT: %s",Flow_ErrorName(d->last_fault)); break;
        case 5u: snprintf(t,43,"PROC:%luus MAXdt:%lums",(unsigned long)d->max_process_us,(unsigned long)d->max_interval_ms); break;
        case 6u: snprintf(t,43,"CLR:%lu RAW:%u/%u/%u",(unsigned long)d->clears,d->raw.raw_sum,d->raw.raw_max,d->raw.raw_min); break;
        case 7u: strcpy(t,"SPI2 703125Hz / target 100Hz"); break;
        case 8u: strcpy(t,"Sensor axes / no mm calibration"); break;
        default: break;
        }
        return;
    }
    switch (row) {
    case 1u: snprintf(t,43,"%s ID:%02X/%02X REV:%02X",Flow_StateName(d),d->product,d->inverse,d->revision); break;
    case 2u: if (!d->have_sample) { strcpy(t,"dx:-- dy:-- counts"); break; } snprintf(t,43,"dx:%ld dy:%ld counts",(long)d->raw.dx,(long)d->raw.dy); break;
    case 3u: Flow_FormatCount(count,d->total_x); snprintf(t,43,"X:%s counts",count); break;
    case 4u: Flow_FormatCount(count,d->total_y); snprintf(t,43,"Y:%s counts",count); break;
    case 5u: if (!d->have_sample) { strcpy(t,"Q:-- SH:---- MOT:-- OBS:--"); break; } snprintf(t,43,"Q:%u SH:%04X MOT:%02X OBS:%02X",d->raw.squal,d->raw.shutter,d->raw.motion,d->raw.observation); break;
    case 6u: if (!d->have_sample) { strcpy(t,"dt:-- age:-- ms"); break; } snprintf(t,43,"dt:%lu age:%lu ms",(unsigned long)d->interval_ms,(unsigned long)d->age_ms); break;
    case 7u: snprintf(t,43,"OK:%lu BAD:%lu E:%lu",(unsigned long)d->accepted,(unsigned long)d->rejected,(unsigned long)d->errors); break;
    case 8u: snprintf(t,43,"S:%lu G:%lu M:%lu",(unsigned long)d->segments,(unsigned long)d->gaps,(unsigned long)d->missed); break;
    default: break;
    }
}
