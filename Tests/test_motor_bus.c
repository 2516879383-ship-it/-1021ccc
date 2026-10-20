#include "motor/motor_bus.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"BUS FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
typedef struct {
    MotorBus bus;
    uint32_t now, tx_done, arrive[7];
    uint8_t bytes[128], length, busy, response, online, drop_code, reject_addr, hold_tx, refuse_stop, never_arrive;
    uint8_t flags[7], cached[7], cached_code[7];
    int32_t rpm[7], cache_rpm[7];
    int64_t pos[7], target[7], cache_target[7], last_target[7];
    unsigned sends[8][256], motions, triggers, groups, reject_send;
    uint32_t first_stop_ms, max_age[3];
} Sim;
static int send(void *ctx, const uint8_t *bytes, uint8_t length)
{
    Sim *s = ctx;
    if (s->reject_send) return 0;
    CHECK(!s->busy && length <= 128u);
    memcpy(s->bytes, bytes, length); s->length = length; s->busy = 1u;
    s->tx_done = s->now + 1u + length / 11u;
    ++s->sends[bytes[0]][bytes[1]];
    if (bytes[1] == EMM_STOP && !s->first_stop_ms) s->first_stop_ms = s->now;
    if (bytes[1] == EMM_VELOCITY || bytes[1] == EMM_MOVE || bytes[1] == EMM_ENABLE) ++s->motions;
    return 1;
}
static int busy(void *ctx) { return ((Sim *)ctx)->busy; }
static int32_t velocity(const uint8_t *c)
{ int32_t r = ((int32_t)c[3] << 8) | c[4]; return c[2] ? -r : r; }
static void apply(Sim *s, const uint8_t *c)
{
    unsigned i = c[0] - 1u;
    uint32_t pulses;
    int64_t delta;
    if (!(s->online & (1u << i))) return;
    if (c[1] == EMM_ENABLE) { s->flags[i] = (uint8_t)((s->flags[i] & 0xFEu) | c[3]); return; }
    if (c[1] == EMM_STOP) { if (!s->refuse_stop) { s->rpm[i] = 0; s->arrive[i] = 0u; } return; }
    if (c[1] == EMM_VELOCITY) {
        if (c[6]) { s->cached[i] = 1u; s->cache_rpm[i] = velocity(c); s->cached_code[i] = c[1]; }
        else s->rpm[i] = velocity(c);
    } else if (c[1] == EMM_MOVE) {
        pulses = ((uint32_t)c[6] << 24) | ((uint32_t)c[7] << 16) | ((uint32_t)c[8] << 8) | c[9];
        delta = (int64_t)pulses * 65536 / 3200;
        if (c[2]) delta = -delta;
        delta += c[10] == 0u ? s->last_target[i] : c[10] == 2u ? s->pos[i] : 0;
        if (c[11]) { s->cached[i] = 1u; s->cache_target[i] = delta; s->cached_code[i] = c[1]; }
        else {
            s->target[i] = s->last_target[i] = delta;
            if (delta != s->pos[i]) { s->arrive[i] = s->now + 180u; s->rpm[i] = 10; }
        }
    }
}
static void reply(Sim *s)
{
    unsigned a = s->bytes[0], i;
    uint8_t c = s->bytes[1], r[8] = {0}, n = 4u;
    uint32_t v;
    r[0] = (uint8_t)a; r[1] = c; r[2] = 2u;
    if (c == EMM_MULTI) {
        ++s->groups;
        for (i = 4u; i + 8u < s->length; i += 8u) apply(s, s->bytes + i);
        a = r[0] = 1u; c = r[1] = EMM_VELOCITY;
    } else if (c == EMM_SYNC) {
        ++s->triggers;
        for (i = 0u; i < 7u; ++i) if (s->cached[i]) {
            if (s->cached_code[i] == EMM_VELOCITY) s->rpm[i] = s->cache_rpm[i];
            else { s->target[i] = s->last_target[i] = s->cache_target[i]; s->arrive[i] = s->now + 180u; s->rpm[i] = 10; }
            s->cached[i] = 0u;
        }
        a = r[0] = 1u;
    } else if (a >= 1u && a <= 7u) {
        if (a == s->reject_addr && (c == EMM_MOVE || c == EMM_VELOCITY)) r[2] = 0xE2u;
        else apply(s, s->bytes);
    }
    if (!(s->online & (1u << (a - 1u))) || c == s->drop_code) return;
    i = a - 1u;
    if (c == EMM_POSITION) {
        n = 8u; r[2] = (uint8_t)(s->pos[i] < 0); v = (uint32_t)(r[2] ? -s->pos[i] : s->pos[i]);
        r[3] = (uint8_t)(v >> 24); r[4] = (uint8_t)(v >> 16); r[5] = (uint8_t)(v >> 8); r[6] = (uint8_t)v;
    } else if (c == EMM_SPEED) {
        n = 6u; r[2] = (uint8_t)(s->rpm[i] < 0); v = (uint32_t)(r[2] ? -s->rpm[i] : s->rpm[i]);
        r[3] = (uint8_t)(v >> 8); r[4] = (uint8_t)v;
    } else if (c == EMM_STATUS) r[2] = s->flags[i];
    else if (c == EMM_VERSION) { n = 7u; r[2] = 0; r[3] = 200u; r[4] = 3u; r[5] = 20u; }
    r[n - 1u] = 0x6Bu;
    /* Split every response, exercising real incremental parsing on the bus. */
    MotorBus_Receive(&s->bus, r, 2u, s->now);
    MotorBus_Receive(&s->bus, r + 2u, n - 2u, s->now);
}
static void run(Sim *s, uint32_t duration)
{
    uint32_t elapsed;
    unsigned i,j;
    for (elapsed=0u; elapsed<duration; ++elapsed,++s->now) {
        for (i = 0u; i < 7u; ++i) if (s->arrive[i] && (int32_t)(s->now-s->arrive[i])>=0) {
            if (!s->never_arrive) s->pos[i] = s->target[i];
            s->rpm[i] = 0; s->arrive[i] = 0u; s->flags[i] |= 2u;
        }
        if (s->busy && !s->hold_tx && (int32_t)(s->now-s->tx_done)>=0) { s->busy = 0u; s->response = 1u; }
        MotorBus_Process(&s->bus, s->now);
        if (s->response) { s->response = 0u; reply(s); }
        for(i=0;i<7u;++i) for(j=0;j<3u;++j) {
            MotorSnapshot *d=&s->bus.nodes[i].data;
            uint32_t at=j==0u ? d->speed_ms : j==1u ? d->position_ms : d->status_ms;
            uint8_t valid=j==0u ? MOTOR_VALID_SPEED : j==1u ? MOTOR_VALID_POSITION : MOTOR_VALID_STATUS;
            if((d->valid&valid) && s->now-at>s->max_age[j]) s->max_age[j]=s->now-at;
        }
    }
}
static void init(Sim *s)
{
    MotorTransport t;
    memset(s, 0, sizeof(*s)); s->online = 0x7Fu;
    memset(s->flags, 3, sizeof(s->flags));
    t.context = s; t.send = send; t.busy = busy;
    MotorBus_Init(&s->bus, t, 0u); run(s, 2500u);
}
static MotorAction move(uint8_t a, uint32_t pulses, EmmPositionMode mode, uint8_t cached)
{
    MotorAction m;
    memset(&m, 0, sizeof(m)); m.command = Emm_MoveEx(a, 0u, 10u, 100u, pulses, mode, cached);
    m.pulses_per_rev = 3200u; m.timeout_ms = 3000u; return m;
}
static MotorAction speed(uint8_t a, uint8_t cached)
{
    MotorAction m;
    memset(&m, 0, sizeof(m)); m.command = Emm_VelocityEx(a, 0u, 10u, 100u, cached);
    m.lease_ms = 1000u; return m;
}
static void capture(void *ctx, const EmmFrame *f)
{ EmmFrame *r = ctx; r[f->address] = *f; ++r[0].flags; }
static void protocol(void)
{
    EmmCommand c, m[7]; EmmFrame f[8] = {{0}}; EmmParser p;
    int32_t rpm[4] = {10,-12,14,-16};
    const uint8_t v[] = {7,0x1F,0,200,3,20,0x6B};
    const uint8_t noise[] = {0xFF,8,0xFD,2,0x6B,2,0xFD,0x12,0x6B,3,0xFD,0x22,0x6B};
    unsigned i;
    CHECK(Emm_Wheels(&c, rpm, 100u));
    CHECK(c.length == 37u && c.bytes[0] == 0u && c.bytes[1] == 0xAAu && c.bytes[2] == 0u && c.bytes[3] == 37u);
    for (i = 0u; i < 4u; ++i) {
        CHECK(c.bytes[4u + 8u*i] == i+1u && c.bytes[5u+8u*i] == EMM_VELOCITY);
        CHECK(c.bytes[6u+8u*i] == i%2u && c.bytes[8u+8u*i] == 10u+2u*i);
        CHECK(c.bytes[10u+8u*i] == 0u && c.bytes[11u+8u*i] == 0x6Bu);
    }
    CHECK(c.bytes[36] == 0x6Bu);
    rpm[0] = INT32_MIN; CHECK(!Emm_Wheels(&c,rpm,0u) && !c.length);
    c = Emm_MoveEx(5,1,3000,255,UINT32_MAX,EMM_REL_ACTUAL,1);
    CHECK(c.length==13 && c.bytes[10]==2u && c.bytes[11]==1u && c.bytes[6]==255u);
    c = Emm_MoveEx(5,1,3001,255,0,EMM_ABSOLUTE,0); CHECK(!c.length);
    for(i=0;i<7;++i) m[i] = Emm_MoveEx((uint8_t)(i+1u),0,1,0,0,EMM_ABSOLUTE,0);
    CHECK(Emm_Multi(&c,m,7) && c.length==96u);
    m[0].length=128; CHECK(!Emm_Multi(&c,m,7) && !c.length);
    m[0]=Emm_Query(1,EMM_POSITION); CHECK(!Emm_Multi(&c,m,7));
    EmmParser_Init(&p,0u); EmmParser_Feed(&p,noise,sizeof(noise),capture,f);
    for(i=0;i<sizeof(v);++i) EmmParser_Feed(&p,v+i,1u,capture,f);
    CHECK(f[0].flags==3u && f[2].result==0x12u && f[3].result==0x22u);
    CHECK(f[7].firmware==200u && f[7].hardware==0x0314u);
}
static void polling(void)
{
    Sim s; unsigned i; uint32_t before;
    init(&s);
    CHECK(!s.bus.manual && !s.motions && s.bus.sync_locked);
    for(i=0;i<7;++i) CHECK(s.bus.nodes[i].data.online && s.bus.nodes[i].data.valid==7u);
    printf("METRIC healthy: %lu TX + %lu RX bytes in %lu ms; max feedback gaps", (unsigned long)s.bus.tx_bytes,
        (unsigned long)s.bus.rx_bytes,(unsigned long)s.now);
    for(i=0;i<3;++i) printf(" %lu",(unsigned long)s.bus.nodes[0].max_poll_gap_ms[i]);
    puts(" ms");
    printf("METRIC max field ages: %lu/%lu/%lu ms\n",(unsigned long)s.max_age[0],(unsigned long)s.max_age[1],(unsigned long)s.max_age[2]);
    s.online &= 0x7Eu; run(&s,4000);
    CHECK(!s.bus.nodes[0].data.online && s.bus.nodes[6].data.online && s.bus.nodes[6].data.valid==7u);
    before=s.bus.nodes[6].data.rx_count; run(&s,4000);
    CHECK(s.bus.nodes[6].data.rx_count>before+10u);
    printf("METRIC node 1 offline: node 7 max feedback gaps %lu/%lu/%lu ms; timeouts %lu\n",
        (unsigned long)s.bus.nodes[6].max_poll_gap_ms[0],(unsigned long)s.bus.nodes[6].max_poll_gap_ms[1],
        (unsigned long)s.bus.nodes[6].max_poll_gap_ms[2],(unsigned long)s.bus.timeout_count);
    s.online |= 1u; run(&s,3000); CHECK(s.bus.nodes[0].data.online && !s.motions);
    MotorBus_ReadVersions(&s.bus); run(&s,2500);
    for(i=0;i<7;++i) CHECK(s.bus.nodes[i].version_valid && s.bus.nodes[i].firmware==200u);
}
static void groups_stops(void)
{
    Sim s; int32_t rpm[4]={10,-12,14,-16}, newer[4]={11,-13,15,-17}; unsigned i;
    uint32_t start_samples;
    uint8_t wrong[]={2,EMM_VELOCITY,2,0x6B}, done[]={1,EMM_VELOCITY,0x9F,0x6B};
    init(&s); CHECK(!MotorBus_Wheels(&s.bus,rpm,100,1000,s.now));
    MotorBus_SetManual(&s.bus,1,s.now);
    CHECK(MotorBus_Wheels(&s.bus,rpm,100,1000,s.now));
    CHECK(MotorBus_Wheels(&s.bus,newer,100,1000,s.now));
    run(&s,200); CHECK(s.groups==1u && s.bus.group_ack==2u && s.rpm[0]==11 && s.rpm[3]==-17);
    for(i=4;i<7;++i) CHECK(s.rpm[i]==0);
    run(&s,1200);
    for(i=0;i<4;++i) CHECK(s.rpm[i]==0 && s.sends[i+1u][EMM_STOP]==1u);
    CHECK(s.sends[6][EMM_ENABLE]==0u);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.drop_code=EMM_VELOCITY;
    CHECK(MotorBus_Wheels(&s.bus,rpm,100,1000,s.now)); run(&s,20);
    MotorBus_Receive(&s.bus,wrong,sizeof(wrong),s.now); MotorBus_Receive(&s.bus,done,sizeof(done),s.now);
    CHECK(s.bus.pending && s.bus.active.kind!=0u && !s.bus.group_ack);
    run(&s,1800); CHECK(s.groups==1u && s.sends[4][EMM_STOP]==1u && !s.rpm[3]);
    /* Address 1 disappears after fresh preflight; other wheels still execute. */
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.online &= 0x7Eu;
    CHECK(MotorBus_Wheels(&s.bus,rpm,100,1000,s.now)); run(&s,1500);
    CHECK(s.groups==1u && s.sends[2][EMM_STOP]==1u && s.rpm[1]==0);
    CHECK(s.bus.nodes[0].action!=MOTOR_STOPPED && s.bus.nodes[1].action==MOTOR_STOPPED);
    /* Stop admits a saturated queue and preempts an unanswered query. */
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    for(i=1;i<=7;++i) CHECK(MotorBus_Enable(&s.bus,(uint8_t)i,1,s.now));
    CHECK(s.bus.queue_count==MOTOR_BUS_QUEUE);
    MotorBus_Stop(&s.bus,MOTOR_ALL,s.now); CHECK(!s.bus.queue_count);
    CHECK(strstr(s.bus.message,"STOP REQUESTED")!=0);
    run(&s,2000); CHECK(!s.motions);
    for(i=0;i<7;++i) CHECK(s.bus.nodes[i].action==MOTOR_STOPPED);
    /* A fresh fault on one wheel stops all wheels and does not disable the lift. */
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    CHECK(MotorBus_Wheels(&s.bus,rpm,100,5000,s.now)); run(&s,200); s.flags[2]|=8u; run(&s,1800);
    for(i=1;i<=4;++i) CHECK(s.sends[i][EMM_STOP]==1u);
    CHECK(!s.sends[6][EMM_ENABLE]);
    /* A 10 Hz wheel producer shares the bus with measured feedback. */
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    start_samples=s.bus.nodes[0].poll_samples[0];
    for(i=0;i<20u;++i) { CHECK(MotorBus_Wheels(&s.bus,rpm,100,1000,s.now)); run(&s,100); }
    CHECK(s.groups>=18u && s.groups<=20u && s.bus.nodes[6].data.online);
    printf("METRIC 10Hz producer: %u groups/2000 ms, wheel speed samples %lu; no timeouts %lu\n",
        s.groups,(unsigned long)(s.bus.nodes[0].poll_samples[0]-start_samples),(unsigned long)s.bus.timeout_count);
    MotorBus_Stop(&s.bus,MOTOR_WHEELS,s.now); run(&s,1600);
    CHECK(!s.rpm[0] && !s.rpm[3]);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.refuse_stop=1u;
    CHECK(MotorBus_Wheels(&s.bus,rpm,100,1000,s.now)); run(&s,4500);
    CHECK(s.bus.nodes[0].action==MOTOR_UNKNOWN && s.rpm[0]==10 && !s.sends[6][EMM_ENABLE]);
}
static void positions(void)
{
    Sim s; MotorAction m; unsigned i;
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    m=move(5,32,EMM_REL_ACTUAL,0); CHECK(MotorBus_Submit(&s.bus,&m,s.now));
    CHECK(!MotorBus_Submit(&s.bus,&m,s.now)); run(&s,1400);
    CHECK(s.bus.nodes[4].action==MOTOR_COMPLETE && s.pos[4]==655);
    m=move(6,0,EMM_ABSOLUTE,0); CHECK(MotorBus_Submit(&s.bus,&m,s.now)); run(&s,1400);
    CHECK(s.bus.nodes[5].action==MOTOR_COMPLETE && s.pos[5]==0);
    m=move(5,32,EMM_REL_TARGET,0); CHECK(MotorBus_Submit(&s.bus,&m,s.now)); run(&s,1400);
    CHECK(s.bus.nodes[4].action==MOTOR_COMPLETE && s.pos[4]==1310);
    m=move(7,32,EMM_REL_ACTUAL,0); s.never_arrive=1u;
    CHECK(MotorBus_Submit(&s.bus,&m,s.now)); run(&s,400);
    CHECK(s.bus.nodes[6].action!=MOTOR_COMPLETE); run(&s,4000);
    CHECK(s.sends[7][EMM_MOVE]==1u && s.sends[7][EMM_STOP]==1u);
    /* ACK loss never retries a relative position. Recovery queries cannot replay it. */
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.drop_code=EMM_MOVE;
    m=move(5,32,EMM_REL_ACTUAL,0); CHECK(MotorBus_Submit(&s.bus,&m,s.now)); run(&s,2000);
    s.drop_code=0u; run(&s,2000); CHECK(s.sends[5][EMM_MOVE]==1u);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    m=move(5,32,EMM_REL_ACTUAL,0); CHECK(MotorBus_Submit(&s.bus,&m,s.now));
    MotorBus_Stop(&s.bus,(uint8_t)(1u<<4),s.now); run(&s,2000); CHECK(!s.sends[5][EMM_MOVE]);
    for(i=0;i<7;++i) CHECK(!s.sends[i+1u][EMM_ENABLE]);
}
static void synchronous(void)
{
    Sim s; MotorAction a[3]; unsigned i;
    init(&s); MotorBus_SetManual(&s.bus,1,s.now);
    for(i=0;i<3;++i) a[i]=move((uint8_t)(i+5),32,EMM_REL_ACTUAL,1);
    CHECK(!MotorBus_Sync(&s.bus,a,3,s.now));
    /* Fixture represents all physical motor caches known empty after a full power cycle. */
    s.bus.sync_locked=0u; s.bus.sync_state=MOTOR_SYNC_IDLE;
    CHECK(MotorBus_Sync(&s.bus,a,3,s.now)); run(&s,1800);
    CHECK(s.triggers==1u && s.bus.sync_state==MOTOR_SYNC_DONE);
    for(i=4;i<7;++i) CHECK(s.bus.nodes[i].action==MOTOR_COMPLETE);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.bus.sync_locked=0u; s.bus.sync_state=MOTOR_SYNC_IDLE;
    s.reject_addr=6; CHECK(MotorBus_Sync(&s.bus,a,3,s.now)); run(&s,1500);
    CHECK(!s.triggers && s.bus.sync_locked && s.bus.sync_state==MOTOR_SYNC_LOCKED && s.cached[4]);
    CHECK(!MotorBus_Sync(&s.bus,a,3,s.now)); run(&s,2000); CHECK(!s.triggers);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.bus.sync_locked=0u; s.bus.sync_state=MOTOR_SYNC_IDLE;
    CHECK(MotorBus_Sync(&s.bus,a,3,s.now)); run(&s,15);
    MotorBus_Stop(&s.bus,MOTOR_ALL,s.now); run(&s,1500);
    CHECK(!s.triggers && s.bus.sync_locked);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); s.bus.sync_locked=0u; s.bus.sync_state=MOTOR_SYNC_IDLE;
    s.drop_code=EMM_MOVE; CHECK(MotorBus_Sync(&s.bus,a,3,s.now)); run(&s,1500);
    CHECK(!s.triggers && s.bus.sync_locked);
}
static void transport(void)
{
    Sim s; MotorAction m;
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); m=speed(5,0);
    CHECK(MotorBus_Submit(&s.bus,&m,s.now)); s.hold_tx=1u; run(&s,50);
    CHECK(s.bus.transport_errors && !s.bus.nodes[0].data.valid);
    s.hold_tx=0u; run(&s,3000); CHECK(s.sends[5][EMM_VELOCITY]==1u);
    init(&s); MotorBus_SetManual(&s.bus,1,s.now); m=move(5,32,EMM_REL_ACTUAL,0);
    CHECK(MotorBus_Submit(&s.bus,&m,s.now));
    MotorBus_TransportError(&s.bus,MOTOR_ERROR_RX_OVERFLOW,s.now); run(&s,3000);
    CHECK(!s.sends[5][EMM_MOVE] && s.bus.nodes[6].data.online);
    init(&s); s.reject_send=1u; run(&s,200); CHECK(s.bus.transport_errors);
    s.reject_send=0u; run(&s,3000); CHECK(s.bus.nodes[6].data.online);
}
static void isolation_wrap(void)
{
    Sim s; MotorTransport t; unsigned i; uint8_t a, late[6]={0,EMM_SPEED,0,0,99,0x6B};
    uint32_t seq, at;
    init(&s); s.drop_code=EMM_SPEED;
    for(i=0;i<1000u && !(s.bus.pending && s.bus.sent && s.bus.active.code==EMM_SPEED);++i) run(&s,1);
    CHECK(i<1000u); a=s.bus.active.address; seq=s.bus.nodes[a-1u].data.speed_seq;
    run(&s,80u); late[0]=a;
    MotorBus_Receive(&s.bus,late,sizeof(late),s.now);
    CHECK(s.bus.nodes[a-1u].data.speed_seq==seq && s.bus.nodes[a-1u].data.speed_rpm!=99);
    s.drop_code=0u; run(&s,2500u); CHECK(s.bus.nodes[a-1u].data.speed_seq>seq);
    init(&s); s.drop_code=EMM_SPEED;
    for(i=0;i<1000u && !(s.bus.pending && s.bus.sent && s.bus.active.code==EMM_SPEED);++i) run(&s,1);
    at=s.now; MotorBus_Stop(&s.bus,MOTOR_ALL,s.now); run(&s,20);
    CHECK(s.first_stop_ms>=at && s.first_stop_ms-at<=15u);
    memset(&s,0,sizeof(s)); s.online=0x7Fu; memset(s.flags,3,sizeof(s.flags));
    s.now=UINT32_MAX-20u; t.context=&s; t.send=send; t.busy=busy;
    MotorBus_Init(&s.bus,t,s.now); run(&s,2500u);
    CHECK(s.now<3000u && !s.motions);
    for(i=0;i<7;++i) CHECK(s.bus.nodes[i].data.online && s.bus.nodes[i].data.valid==7u);
}
int main(void)
{
    protocol(); polling(); groups_stops(); positions(); synchronous(); transport(); isolation_wrap();
    printf("PASS: %u multi-motor checks (protocol, seven nodes, scheduling, faults, stop, sync)\n",checks);
    return 0;
}
