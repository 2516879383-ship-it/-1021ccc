#include "gyro_service.h"
#include "gyro_config.h"
#include <string.h>
#include <limits.h>
static float absf(float x) { return x<0.0f ? -x : x; }
static void gap(GyroService *s,GyroError e)
{
    if (!s->data.angle_gap) ++s->data.gaps;
    s->data.angle_gap=1u; s->data.angle_valid=0u; s->have_previous=0u;
    s->data.error=s->data.last_fault=e;
}
static void cal_fail(GyroService *s,GyroCalibration c)
{
    s->data.calibration=c;
    gap(s,GYRO_GAP); /* Keep the old bias, never resume its old trajectory. */
}
static GyroError chip_error(int r)
{
    if (r==ICM_ID_ERROR) return GYRO_ID;
    if (r==ICM_RESET_ERROR || r==ICM_RESET_DETECTED) return GYRO_RESET;
    if (r==ICM_CONFIG_ERROR) return GYRO_CONFIG;
    return GYRO_BUS;
}
static void fault(GyroService *s,int r,uint32_t now)
{
    if (s->data.calibration==GYRO_CAL_RUNNING) cal_fail(s,GYRO_CAL_ERROR);
    gap(s,chip_error(r)); s->data.state=GYRO_ERROR;
    s->data.raw_valid=s->data.rate_valid=0u;
    s->data.bus_error=s->chip.bus_error; ++s->data.errors;
    s->due=now+s->retry_ms;
    if (s->retry_ms<GYRO_RETRY_MAX_MS) s->retry_ms*=2u;
}
static void start(GyroService *s,uint32_t now)
{
    GyroBusError e;
    s->data.state=GYRO_INIT; s->data.error=GYRO_OK;
    s->data.calibrated=s->data.have_sample=s->data.raw_valid=0u;
    s->data.rate_valid=s->data.angle_valid=s->have_previous=0u;
    /* Preserve a failed calibration result for diagnosis until next CAL. */
    if (s->data.calibration==GYRO_CAL_DONE) s->data.calibration=GYRO_CAL_NONE;
    Icm_Begin(&s->chip,now);
    e=s->io.init(s->io.ctx);
    if (e!=GYRO_BUS_OK) { s->chip.bus_error=e; fault(s,ICM_BUS_ERROR,now); }
}
void Gyro_Init(GyroService *s,GyroIo io,uint32_t now)
{
    memset(s,0,sizeof(*s)); s->io=io;
    s->data.axis=GYRO_AXIS; s->data.sign=GYRO_SIGN;
    s->retry_ms=GYRO_RETRY_MIN_MS; start(s,now);
}
static int fresh(const GyroService *s,uint32_t now)
{
    return s->data.state==GYRO_RUNNING && s->data.have_sample && s->data.raw_valid &&
        (uint32_t)(now-s->data.sample_ms)<=GYRO_STALE_MS;
}
int Gyro_RequestCalibration(GyroService *s,uint32_t now,uint8_t motor_active)
{
    if (s->data.calibration==GYRO_CAL_RUNNING) return 0;
    ++s->data.cal_requests;
    if (motor_active) { s->data.calibration=GYRO_CAL_MOTOR; return 0; }
    if (!fresh(s,now)) { s->data.calibration=GYRO_CAL_NOT_READY; return 0; }
    s->data.angle_valid=0u; s->have_previous=0u;
    s->data.calibration=GYRO_CAL_RUNNING; s->data.rate_valid=0u;
    s->data.cal_count=s->data.cal_elapsed_ms=0u; s->cal_start=now;
    memset(s->mean,0,sizeof(s->mean)); memset(s->m2,0,sizeof(s->m2));
    return 1;
}
int Gyro_ZeroAngle(GyroService *s,uint32_t now)
{
    if (!fresh(s,now) || !s->data.calibrated || s->data.calibration==GYRO_CAL_RUNNING) return 0;
    /* Boundary is latest acquired sample, not touch/UI refresh time. */
    s->data.angle_deg=0.0; s->data.angle_valid=1u; s->data.angle_gap=0u;
    s->previous_rate=s->data.rate_dps; s->have_previous=1u; ++s->data.zeros;
    return 1;
}
static int calibrate(GyroService *s,uint32_t now)
{
    unsigned i; float x,delta; GyroSnapshot *d=&s->data;
    if (!d->cal_count) s->cal_first=now;
    ++d->cal_count; d->cal_elapsed_ms=now-s->cal_first;
    for (i=0u;i<3u;++i) {
        x=(float)d->raw[i]/GYRO_LSB_PER_DPS;
        if (d->cal_count==1u) s->minimum[i]=s->maximum[i]=x;
        if (x<s->minimum[i]) s->minimum[i]=x;
        if (x>s->maximum[i]) s->maximum[i]=x;
        delta=x-s->mean[i]; s->mean[i]+=delta/(float)d->cal_count;
        s->m2[i]+=delta*(x-s->mean[i]);
        if (absf(x)>GYRO_CAL_MAX_ABS_DPS || s->maximum[i]-s->minimum[i]>GYRO_CAL_MAX_RANGE_DPS) {
            cal_fail(s,GYRO_CAL_MOVING); return 0;
        }
    }
    if (d->cal_count<GYRO_CAL_SAMPLES || d->cal_elapsed_ms<GYRO_CAL_MS) return 0;
    for (i=0u;i<3u;++i) if (absf(s->mean[i])>GYRO_CAL_MAX_MEAN_DPS ||
        s->m2[i]/(float)(d->cal_count-1u)>GYRO_CAL_MAX_VARIANCE) {
        cal_fail(s,GYRO_CAL_MOVING); return 0;
    }
    d->bias_dps=(float)GYRO_SIGN*s->mean[GYRO_AXIS]; d->calibrated=1u;
    d->calibration=GYRO_CAL_DONE; d->angle_deg=0.0;
    d->angle_gap=0u; d->angle_valid=1u; s->have_previous=0u;
    return 1;
}
static void process(GyroService *s,uint32_t now,uint8_t motor_active)
{
    GyroSnapshot *d=&s->data; uint32_t at,dt,poll_dt; int r; int16_t raw[3];
    if (d->state==GYRO_ERROR) {
        if ((int32_t)(now-s->due)>=0) { ++d->restarts; start(s,now); }
        return;
    }
    if (d->state==GYRO_INIT) {
        r=Icm_Step(&s->chip,&s->io,now); d->who=s->chip.who;
        if (r==ICM_READY) {
            d->state=GYRO_RUNNING; d->bus_error=GYRO_BUS_OK;
            s->last_poll=s->last_check=s->ready_ms=s->io.millis(s->io.ctx);
            s->retry_ms=GYRO_RETRY_MIN_MS;
        } else if (r<0) fault(s,r,s->io.millis(s->io.ctx));
        return;
    }
    if (d->calibration==GYRO_CAL_RUNNING) {
        if (motor_active) cal_fail(s,GYRO_CAL_MOTOR);
        else if ((uint32_t)(now-s->cal_start)>=GYRO_CAL_TIMEOUT_MS) cal_fail(s,GYRO_CAL_TIMEOUT);
    }
    if ((uint32_t)(now-(d->have_sample ? d->sample_ms : s->ready_ms))>GYRO_STALE_MS) {
        if (d->calibration==GYRO_CAL_RUNNING) cal_fail(s,GYRO_CAL_GAP);
        gap(s,GYRO_STALE); d->raw_valid=d->rate_valid=0u;
        if ((uint32_t)(now-(d->have_sample ? d->sample_ms : s->ready_ms))>=GYRO_NO_DATA_RESTART_MS) {
            fault(s,ICM_RESET_ERROR,now); return;
        }
    }
    poll_dt=now-s->last_poll;
    if (poll_dt<GYRO_POLL_MS) return;
    s->last_poll=now;
    if (poll_dt>d->max_poll_ms) d->max_poll_ms=poll_dt;
    /* With no FIFO a polling blackout of one ODR can hide a lost frame.
       Conservatively break continuity even if the next value looks plausible. */
    if (d->have_sample && poll_dt>=GYRO_PERIOD_MS) {
        gap(s,GYRO_GAP);
        if (d->calibration==GYRO_CAL_RUNNING) cal_fail(s,GYRO_CAL_GAP);
    }
    if ((uint32_t)(now-s->last_check)>=GYRO_CHECK_MS) {
        s->last_check=now; r=Icm_Check(&s->chip,&s->io); d->who=s->chip.who;
        if (r<0) fault(s,r,s->io.millis(s->io.ctx));
        return; /* <=1 config transaction; resume DRDY polling next step. */
    }
    r=Icm_Fetch(&s->chip,&s->io,raw,&at); d->bus_error=s->chip.bus_error;
    if (r==ICM_NO_DATA) return;
    if (r==ICM_INVALID || r==ICM_SATURATED) {
        if (r==ICM_INVALID) ++d->invalid; else ++d->saturations;
        if (d->calibration==GYRO_CAL_RUNNING) cal_fail(s,GYRO_CAL_ERROR);
        gap(s,r==ICM_INVALID ? GYRO_BAD_SAMPLE : GYRO_SATURATED);
        d->raw_valid=d->rate_valid=0u; return;
    }
    if (r<0) { fault(s,r,s->io.millis(s->io.ctx)); return; }
    dt=d->have_sample ? at-d->sample_ms : 0u;
    d->interval_ms=dt;
    if (dt>d->max_interval_ms) d->max_interval_ms=dt;
    if (d->have_sample && (dt==0u || dt>GYRO_MAX_SAMPLE_MS)) {
        if (dt>=2u*GYRO_PERIOD_MS) d->missed+=dt/GYRO_PERIOD_MS-1u;
        gap(s,GYRO_GAP);
        if (d->calibration==GYRO_CAL_RUNNING) cal_fail(s,GYRO_CAL_GAP);
    }
    memcpy(d->raw,raw,sizeof(raw)); d->raw_dps=(float)(GYRO_SIGN*(int32_t)raw[GYRO_AXIS])/GYRO_LSB_PER_DPS;
    d->sample_ms=at; d->have_sample=d->raw_valid=1u; ++d->sequence;
    d->error=GYRO_OK;
    if (d->calibration==GYRO_CAL_RUNNING) (void)calibrate(s,at);
    d->rate_dps=d->raw_dps-d->bias_dps;
    d->rate_valid=(uint8_t)(d->calibrated && d->calibration!=GYRO_CAL_RUNNING);
    if (d->rate_valid && d->angle_valid) {
        if (s->have_previous) d->angle_deg+=(double)(s->previous_rate+d->rate_dps)*0.0005*(double)dt;
        s->previous_rate=d->rate_dps; s->have_previous=1u;
    }
}
void Gyro_Process(GyroService *s,uint32_t now,uint8_t motor_active)
{
    uint32_t began=s->io.cycles(s->io.ctx),cost;
    process(s,now,motor_active);
    cost=(uint32_t)(s->io.cycles(s->io.ctx)-began)/s->io.cycles_per_us;
    if (cost>s->data.max_process_us) s->data.max_process_us=cost;
}
void Gyro_GetSnapshot(const GyroService *s,uint32_t now,GyroSnapshot *out)
{
    *out=s->data; out->age_ms=out->have_sample ? now-out->sample_ms : UINT32_MAX;
    if (!fresh(s,now)) {
        out->raw_valid=out->rate_valid=out->angle_valid=0u;
        if (out->state==GYRO_RUNNING) out->error=GYRO_STALE;
    }
}
const char *Gyro_StateName(const GyroSnapshot *d)
{
    if (d->state==GYRO_INIT) return "INIT";
    if (d->state==GYRO_ERROR) return d->error==GYRO_ID ? "NOT FOUND" : "ERROR";
    if (d->calibration==GYRO_CAL_RUNNING) return "CALIBRATING";
    if (!d->raw_valid) return d->error==GYRO_STALE ? "STALE" : "INVALID";
    if (!d->calibrated) return "UNCAL / RAW";
    if (!d->angle_valid) return "ANGLE GAP";
    return "READY";
}
const char *Gyro_ErrorName(GyroError e)
{
    static const char *names[]={"NONE","I2C","BAD ID","RESET/NO DRDY","CONFIG",
        "INVALID DATA","SATURATED","SAMPLE GAP","STALE"};
    return (unsigned)e<sizeof(names)/sizeof(names[0]) ? names[e] : "UNKNOWN";
}
const char *Gyro_CalName(GyroCalibration c)
{
    static const char *names[]={"CAL REQUIRED","KEEP STILL","OK","MOVING","TIMEOUT",
        "ERROR","MOTOR ACTIVE","SAMPLE GAP","NOT READY"};
    return (unsigned)c<sizeof(names)/sizeof(names[0]) ? names[c] : "UNKNOWN";
}
