#include "chassis_odometry.h"
#include "chassis_kinematics.h"
#include <math.h>
#define RAD 0.01745329251994329577
int Chassis_GyroValid(const GyroSnapshot *g, uint32_t now)
{
    return g && g->state==GYRO_RUNNING && g->calibrated && g->have_sample &&
        g->angle_valid && g->rate_valid && !g->angle_gap && g->calibration!=GYRO_CAL_RUNNING &&
        now-g->sample_ms<=30u && Chassis_Finite(g->angle_deg) && Chassis_Finite(g->rate_dps);
}
int Chassis_GyroContinuous(const GyroSnapshot *a, const GyroSnapshot *b)
{
    return a->gaps==b->gaps && a->restarts==b->restarts && a->zeros==b->zeros &&
        a->cal_requests==b->cal_requests && a->invalid==b->invalid &&
        a->errors==b->errors && a->saturations==b->saturations &&
        (int32_t)(b->sequence-a->sequence)>=0;
}
static int flow_valid(const FlowSnapshot *f, uint32_t now)
{
    return f && f->state==FLOW_READY && f->have_sample && f->valid && f->tracking &&
        f->comm_ok && !f->overflow && now-f->sample_ms<=100u;
}
static int flow_continuous(const FlowSnapshot *a, const FlowSnapshot *b)
{
    return a->rejected==b->rejected && a->gaps==b->gaps && a->segments==b->segments &&
        a->clears==b->clears && a->errors==b->errors && a->overflow==b->overflow &&
        (int32_t)(b->sequence-a->sequence)>=0;
}
static void invalidate(ChassisOdometry *o)
{
    if (o->baseline || o->valid) ++o->generation;
    o->baseline=o->valid=0u;
}
static int aligned(const ChassisOdometry *o, uint32_t time, double *angle)
{
    const ChassisGyroPoint *a=&o->gyro[0], *b=&o->gyro[1];
    uint32_t dt;
    if (!o->gyro_count) return 0;
    if (b->sample_ms==time) { *angle=b->angle; return 1; }
    if (o->gyro_count<2u || (int32_t)(time-a->sample_ms)<0 ||
        (int32_t)(b->sample_ms-time)<0) return 0;
    dt=b->sample_ms-a->sample_ms;
    if (!dt || dt>14u) return 0;
    *angle=a->angle+(b->angle-a->angle)*(time-a->sample_ms)/dt;
    return 1;
}
void Chassis_OdometryProcess(ChassisOdometry *o, const ChassisConfig *c,
    const FlowSnapshot *f, const GyroSnapshot *g, uint32_t now)
{
    double angle, a, b, m, mount, dx, dy, bx, by;
    if (!Chassis_GyroValid(g,now)) { invalidate(o); o->gyro_count=0u; return; }
    if (o->gyro_count && !Chassis_GyroContinuous(&o->gyro_guard,g)) {
        invalidate(o); o->gyro_count=0u;
    }
    if (!o->gyro_count || g->sequence!=o->gyro[1].sequence) {
        o->gyro[0]=o->gyro[1]; o->gyro[1].angle=g->angle_deg;
        o->gyro[1].sequence=g->sequence; o->gyro[1].sample_ms=g->sample_ms;
        if (o->gyro_count<2u) ++o->gyro_count;
    }
    o->gyro_guard=*g;
    if (Chassis_ConfigIssues(c,1u) || !flow_valid(f,now)) { invalidate(o); return; }
    if (o->baseline && !flow_continuous(&o->flow_guard,f)) invalidate(o);
    if (o->baseline && now-o->flow_guard.sample_ms>100u) invalidate(o);
    if (o->baseline && f->sequence==o->flow_guard.sequence) return;
    /* Wait for a bracket; never extrapolate a new flow sample. Counts can be
       accumulated across skipped samples only within one uninterrupted segment. */
    if (!aligned(o,f->sample_ms,&angle)) return;
    if (!o->baseline) {
        o->x=o->y=0.0; o->baseline=o->valid=1u;
    } else {
        dx=((double)f->total_x-(double)o->flow_guard.total_x)*c->flow_x_mm_count;
        dy=((double)f->total_y-(double)o->flow_guard.total_y)*c->flow_y_mm_count;
        mount=(double)c->flow_mount_deg*RAD; bx=cos(mount)*dx-sin(mount)*dy;
        by=sin(mount)*dx+cos(mount)*dy;
        a=o->flow_angle*RAD; b=angle*RAD; m=(a+b)*0.5;
        o->x+=cos(m)*bx-sin(m)*by-
            ((cos(b)-cos(a))*c->flow_offset_x_mm-(sin(b)-sin(a))*c->flow_offset_y_mm);
        o->y+=sin(m)*bx+cos(m)*by-
            ((sin(b)-sin(a))*c->flow_offset_x_mm+(cos(b)-cos(a))*c->flow_offset_y_mm);
        if (!Chassis_Finite(o->x) || !Chassis_Finite(o->y)) { invalidate(o); return; }
    }
    o->flow_angle=angle; o->flow_guard=*f;
}
