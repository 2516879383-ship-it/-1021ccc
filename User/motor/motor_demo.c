#include "motor_demo.h"
#include "motor_demo_config.h"
#include <string.h>

static void enter(MotorDemo *d, MotorDemoState state, uint32_t now)
{ d->state = state; d->state_ms = now; }

void MotorDemo_Init(MotorDemo *d, uint32_t now_ms)
{
    memset(d, 0, sizeof(*d)); d->state = DEMO_BOOT;
    d->boot_ms = now_ms; d->state_ms = now_ms;
}

static void fail(MotorDemo *d, MotorService *s, MotorError error, uint32_t now)
{
    d->fault = error; s->data.last_error = error;
    /* Remove queued motion. A query can be cancelled because each opcode is matched separately. */
    s->queued_valid = 0u;
    if (s->pending && s->query) { s->pending = 0u; s->parser.used = 0u; }
    enter(d, DEMO_FAULT_SEND_STOP, now);
}

static void submit(MotorDemo *d, MotorService *s, EmmCommand command,
                    MotorDemoState wait_state, uint32_t now)
{
    if (MotorService_Submit(s, &command)) {
        d->expected_command_seq = s->data.command_seq + 1u;
        enter(d, wait_state, now);
    }
}

static int ack(MotorDemo *d, MotorService *s, uint32_t now)
{
    if (s->data.command_seq == d->expected_command_seq) {
        return s->data.command_result == EMM_ACK_OK ? 1 : -1;
    }
    return (uint32_t)(now - d->state_ms) >= MOTOR_DEMO_COMMAND_WAIT_MS ? -1 : 0;
}

static void start_zero_check(MotorDemo *d, const MotorSnapshot *data)
{ d->observed_speed_seq = data->speed_seq; d->zero_samples = 0u; }

static int zero_speed(MotorDemo *d, const MotorSnapshot *data, uint32_t now)
{
    if (d->observed_speed_seq != data->speed_seq) {
        d->observed_speed_seq = data->speed_seq;
        if ((data->valid & MOTOR_VALID_SPEED) && (uint32_t)(now - data->speed_ms) <= MOTOR_DATA_STALE_MS &&
            data->speed_rpm >= -MOTOR_DEMO_ZERO_RPM && data->speed_rpm <= MOTOR_DEMO_ZERO_RPM) {
            if (d->zero_samples < MOTOR_DEMO_ZERO_SAMPLES) ++d->zero_samples;
        } else d->zero_samples = 0u;
    }
    return d->zero_samples >= MOTOR_DEMO_ZERO_SAMPLES &&
        (uint32_t)(now - data->speed_ms) <= MOTOR_DATA_STALE_MS;
}

static int new_status(const MotorDemo *d, const MotorSnapshot *data, uint32_t now)
{
    return data->status_seq != d->observed_status_seq && (data->valid & MOTOR_VALID_STATUS) &&
           (uint32_t)(now - data->status_ms) <= MOTOR_DATA_STALE_MS;
}

void MotorDemo_Process(MotorDemo *d, MotorService *s, uint32_t now)
{
    int result;
    if (d->state >= DEMO_SEND_ENABLE && d->state < DEMO_DONE) {
        if (!s->data.online) fail(d,s,MOTOR_ERROR_TIMEOUT,now);
        else if ((s->data.status_flags & 0x0Cu) != 0u) fail(d,s,MOTOR_ERROR_STALL,now);
    }
    if (d->state >= DEMO_FAULT_SEND_STOP) s->data.last_error = d->fault;
    switch (d->state) {
    case DEMO_BOOT:
        if ((uint32_t)(now - d->boot_ms) >= MOTOR_DEMO_BOOT_WAIT_MS) enter(d,DEMO_WAIT_LINK,now);
        break;
    case DEMO_WAIT_LINK:
        if (s->data.online) {
            if (s->data.status_flags & 0x0Cu) fail(d,s,MOTOR_ERROR_STALL,now);
            else enter(d,DEMO_SEND_ENABLE,now);
        } else if ((uint32_t)(now - d->state_ms) >= MOTOR_DEMO_LINK_TIMEOUT_MS)
            fail(d,s,MOTOR_ERROR_TIMEOUT,now);
        break;
    case DEMO_SEND_ENABLE: submit(d,s,Emm_Enable(s->data.address,1u),DEMO_ENABLE_ACK,now); break;
    case DEMO_ENABLE_ACK:
        result=ack(d,s,now);
        if (result > 0) {
            d->observed_status_seq=s->data.status_seq; enter(d,DEMO_ENABLE_CONFIRM,now);
        } else if (result < 0) fail(d,s,s->data.last_error == MOTOR_ERROR_NONE ? MOTOR_ERROR_TIMEOUT : s->data.last_error,now);
        break;
    case DEMO_ENABLE_CONFIRM:
        if (new_status(d,&s->data,now) && (s->data.status_flags & 1u)) enter(d,DEMO_SEND_SPEED,now);
        else if ((uint32_t)(now-d->state_ms) >= MOTOR_DEMO_CONFIRM_MS) fail(d,s,MOTOR_ERROR_TIMEOUT,now);
        break;
    case DEMO_SEND_SPEED:
        submit(d,s,Emm_Velocity(s->data.address,MOTOR_DEMO_DIRECTION,MOTOR_DEMO_RPM,
                MOTOR_DEMO_SPEED_ACCEL),DEMO_SPEED_ACK,now); break;
    case DEMO_SPEED_ACK:
        result=ack(d,s,now);
        if (result > 0) enter(d,DEMO_SPEED_RUN,now);
        else if (result < 0) fail(d,s,s->data.last_error,now);
        break;
    case DEMO_SPEED_RUN:
        if ((uint32_t)(now-d->state_ms) >= MOTOR_DEMO_SPEED_RUN_MS) enter(d,DEMO_SEND_STOP,now);
        break;
    case DEMO_SEND_STOP: submit(d,s,Emm_Stop(s->data.address),DEMO_STOP_ACK,now); break;
    case DEMO_STOP_ACK:
        result=ack(d,s,now);
        if (result > 0) { start_zero_check(d,&s->data); enter(d,DEMO_WAIT_ZERO,now); }
        else if (result < 0) fail(d,s,s->data.last_error,now);
        break;
    case DEMO_WAIT_ZERO:
        if (zero_speed(d,&s->data,now)) enter(d,DEMO_SEND_DISABLE,now);
        else if ((uint32_t)(now-d->state_ms) >= MOTOR_DEMO_STOP_TIMEOUT_MS) fail(d,s,MOTOR_ERROR_MOTION_TIMEOUT,now);
        break;
    case DEMO_SEND_DISABLE: submit(d,s,Emm_Enable(s->data.address,0u),DEMO_DISABLE_ACK,now); break;
    case DEMO_DISABLE_ACK:
        result=ack(d,s,now);
        if (result > 0) { d->observed_status_seq=s->data.status_seq; enter(d,DEMO_DISABLE_CONFIRM,now); }
        else if (result < 0) fail(d,s,s->data.last_error,now);
        break;
    case DEMO_DISABLE_CONFIRM:
        if (new_status(d,&s->data,now) && !(s->data.status_flags & 1u)) enter(d,DEMO_DONE,now);
        else if ((uint32_t)(now-d->state_ms) >= MOTOR_DEMO_CONFIRM_MS) fail(d,s,MOTOR_ERROR_TIMEOUT,now);
        break;
    case DEMO_FAULT_SEND_STOP:
        submit(d,s,Emm_Stop(s->data.address),DEMO_FAULT_STOP_ACK,now); break;
    case DEMO_FAULT_STOP_ACK:
        if (ack(d,s,now)) enter(d,DEMO_FAULT_SEND_DISABLE,now);
        break;
    case DEMO_FAULT_SEND_DISABLE:
        submit(d,s,Emm_Enable(s->data.address,0u),DEMO_FAULT_DISABLE_ACK,now); break;
    case DEMO_FAULT_DISABLE_ACK:
        if (ack(d,s,now)) {
            start_zero_check(d,&s->data); d->observed_status_seq=s->data.status_seq;
            enter(d,DEMO_FAULT_VERIFY,now);
        } break;
    case DEMO_FAULT_VERIFY: case DEMO_FAULT:
        if (zero_speed(d,&s->data,now) && new_status(d,&s->data,now) && !(s->data.status_flags & 1u)) {
            d->cleanup_confirmed=1u; enter(d,DEMO_FAULT,now);
        } else if (d->state == DEMO_FAULT_VERIFY && (uint32_t)(now-d->state_ms) >= MOTOR_DEMO_CONFIRM_MS)
            enter(d,DEMO_FAULT,now);
        break;
    case DEMO_DONE: default: break;
    }
}

const char *MotorDemo_StateName(const MotorDemo *d)
{
    static const char * const names[] = {
        "BOOT WAIT", "WAIT LINK", "ENABLE SEND", "ENABLE ACK", "VERIFY ENABLE",
        "SPEED SEND", "SPEED ACK", "SPEED 1000 RPM", "STOP SEND", "STOP ACK", "VERIFY ZERO",
        "DISABLE SEND", "DISABLE ACK", "VERIFY DISABLE", "DONE (queries continue)",
        "FAULT: STOP SEND", "FAULT: STOP ACK", "FAULT: DISABLE SEND", "FAULT: DISABLE ACK",
        "FAULT: VERIFY STOP", "FAULT / STOP UNCONFIRMED"
    };
    if (d->state == DEMO_FAULT && d->cleanup_confirmed) return "FAULT / STOP CONFIRMED";
    return names[d->state];
}
