#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "motor/motor_service.h"

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); return 0; } } while (0)
typedef struct {
    unsigned sends;
    int busy, reject;
    uint8_t last[16], length;
} FakeTransport;

static int fake_send(void *context, const uint8_t *data, uint8_t length)
{
    FakeTransport *f = (FakeTransport *)context;
    if (f->reject) return 0;
    memcpy(f->last, data, length); f->length = length; ++f->sends; f->busy = 1;
    return 1;
}
static int fake_busy(void *context) { return ((FakeTransport *)context)->busy; }
static void init(MotorService *s, FakeTransport *f)
{
    MotorTransport transport;
    memset(f, 0, sizeof(*f)); transport.context = f;
    transport.send = fake_send; transport.busy = fake_busy;
    MotorService_Init(s, transport, 1u, 0u);
}
static void capture(void *context, const EmmFrame *frame)
{
    EmmFrame *frames = (EmmFrame *)context;
    frames[0].flags++; frames[frames[0].flags] = *frame;
}

static int test_protocol(void)
{
    const uint8_t pos[] = {1,0x36,1,0xFF,0xFF,0xFF,0xFF,0x6B};
    const uint8_t speed[] = {1,0x35,1,0x03,0xE8,0x6B};
    const uint8_t status[] = {1,0x3A,0x8B,0x6B};
    const uint8_t embedded_tail[] = {1,0x36,0,0,0x6B,0,0x6B,0x6B};
    const uint8_t bad[] = {9,0x36,0,1,2,3,4,0x6B,1,0x36,2,0,0,0,0,0x6B,
                           1,0x3A,3,0xFF,1,0x99,0x6B};
    const uint8_t ack[] = {1,0xFD,2,0x6B,1,0xFD,0x9F,0x6B,1,0xF3,0xE2,0x6B,1,0xFE,0xEE,0x6B};
    const uint8_t expected_move[] = {1,0xFD,0,3,0xE8,0,0,0,0x7D,0,0,0,0x6B};
    const uint8_t expected_velocity[] = {1,0xF6,0,3,0xE8,10,0,0x6B};
    EmmParser p;
    EmmFrame frames[20] = {{0}};
    EmmCommand c;
    unsigned i;
    c = Emm_Move(1,0,1000,0,32000);
    CHECK(c.length == sizeof(expected_move) && !memcmp(c.bytes, expected_move, sizeof(expected_move)));
    c = Emm_Velocity(1,0,1000,10);
    CHECK(c.length == sizeof(expected_velocity) && !memcmp(c.bytes, expected_velocity, sizeof(expected_velocity)));
    c = Emm_Enable(1,1); CHECK(c.length == 6 && c.bytes[2] == 0xAB && c.bytes[3] == 1);
    c = Emm_Stop(1); CHECK(c.length == 5 && c.bytes[2] == 0x98);
    EmmParser_Init(&p, 1);
    for (i=0; i<sizeof(pos); ++i) EmmParser_Feed(&p,pos+i,1,capture,frames);
    CHECK(frames[0].flags == 1 && frames[1].position == -INT64_C(4294967295));
    EmmParser_Feed(&p,speed,sizeof(speed),capture,frames);
    EmmParser_Feed(&p,status,sizeof(status),capture,frames);
    CHECK(frames[2].speed == -1000 && frames[3].flags == 0x8B);
    EmmParser_Feed(&p,embedded_tail,sizeof(embedded_tail),capture,frames);
    CHECK(frames[4].position == INT64_C(0x006B006B));
    EmmParser_Feed(&p,bad,sizeof(bad),capture,frames);
    EmmParser_Feed(&p,status,sizeof(status),capture,frames);
    CHECK(frames[0].flags == 5 && p.rejected > 0);
    EmmParser_Feed(&p,ack,sizeof(ack),capture,frames);
    CHECK(frames[0].flags == 9 && frames[6].result == 2 && frames[7].result == 0x9F);
    CHECK(frames[8].result == 0xE2 && frames[9].result == 0xEE);
    for (i=1; i<sizeof(embedded_tail); ++i) {
        memset(frames,0,sizeof(frames)); EmmParser_Init(&p,1);
        EmmParser_Feed(&p,embedded_tail,i,capture,frames);
        EmmParser_Feed(&p,embedded_tail+i,sizeof(embedded_tail)-i,capture,frames);
        CHECK(frames[0].flags == 1 && frames[1].position == INT64_C(0x006B006B));
    }
    return 1;
}

static int test_service(void)
{
    MotorService s;
    FakeTransport f;
    const uint8_t pos[] = {1,0x36,0,0,1,0,0,0x6B};
    const uint8_t speed[] = {1,0x35,0,0,0,0x6B};
    const uint8_t status[] = {1,0x3A,3,0x6B};
    const uint8_t ack[] = {1,0xF3,2,0x6B};
    const uint8_t async[] = {1,0xFD,0x9F,0x6B};
    EmmCommand c = Emm_Enable(1,1);
    init(&s,&f);
    MotorService_Process(&s,5); CHECK(f.sends == 1 && f.last[1] == EMM_POSITION);
    f.busy = 0; MotorService_Process(&s,6);
    MotorService_Receive(&s,pos,sizeof(pos),7); MotorService_Process(&s,10);
    CHECK(f.sends == 2 && f.last[1] == EMM_SPEED);
    f.busy = 0; MotorService_Receive(&s,speed,sizeof(speed),11); MotorService_Process(&s,15);
    CHECK(f.sends == 3 && f.last[1] == EMM_STATUS);
    f.busy = 0; MotorService_Receive(&s,status,sizeof(status),16); MotorService_Process(&s,17);
    CHECK(s.data.online && s.data.position_counts == 65536 && s.data.status_flags == 3);
    CHECK(MotorService_Submit(&s,&c)); CHECK(!MotorService_Submit(&s,&c));
    MotorService_Process(&s,20); CHECK(f.last[1] == EMM_ENABLE && f.sends == 4);
    f.busy = 0; MotorService_Process(&s,21);
    MotorService_Receive(&s,async,sizeof(async),22); CHECK(!s.data.command_seq);
    MotorService_Receive(&s,ack,sizeof(ack),23); CHECK(s.data.command_seq == 1 && s.data.command_result == 2);
    MotorService_Process(&s,700); CHECK(!s.data.online);
    init(&s,&f); f.reject=1; MotorService_Process(&s,5); CHECK(!f.sends && !s.transmitted);
    f.reject=0; MotorService_Process(&s,6); CHECK(f.sends == 1);
    f.busy=0; MotorService_Process(&s,7); MotorService_Process(&s,107);
    CHECK(s.data.timeouts == 1 && f.sends == 1);
    MotorService_Receive(&s,pos,sizeof(pos),108); CHECK(s.pending && !s.transmitted);
    MotorService_Process(&s,206); CHECK(f.sends == 1);
    MotorService_Process(&s,207); CHECK(f.sends == 2);
    f.busy=0; MotorService_Process(&s,208); MotorService_Process(&s,308);
    MotorService_Process(&s,408); CHECK(f.sends == 3);
    f.busy=0; MotorService_Process(&s,409); MotorService_Process(&s,509);
    CHECK(s.data.timeouts == 3 && !s.pending);
    init(&s,&f); CHECK(MotorService_Submit(&s,&c));
    MotorService_Process(&s,5); f.busy=0; MotorService_Process(&s,6);
    MotorService_Process(&s,106); CHECK(s.data.command_result == EMM_ACK_TIMEOUT);
    MotorService_Process(&s,206); CHECK(f.last[1] == EMM_POSITION && f.sends == 2);
    MotorService_Receive(&s,ack,sizeof(ack),207); CHECK(s.data.command_seq == 1 && s.pending);
    MotorService_TransportError(&s,MOTOR_ERROR_RX_OVERFLOW,208);
    CHECK(s.data.last_error == MOTOR_ERROR_RX_OVERFLOW && s.data.valid == 0);
    init(&s,&f); MotorService_Process(&s,5); MotorService_Process(&s,25);
    CHECK(s.data.last_error == MOTOR_ERROR_UART);
    init(&s,&f); f.reject=1; MotorService_Process(&s,5); MotorService_Process(&s,105);
    CHECK(s.data.last_error == MOTOR_ERROR_UART && !s.pending);
    f.reject=0; MotorService_Process(&s,205);
    f.busy=0; MotorService_Receive(&s,speed,sizeof(speed),206);
    MotorService_Process(&s,210); f.busy=0;
    MotorService_Receive(&s,status,sizeof(status),211);
    MotorService_Process(&s,305); f.busy=0;
    MotorService_Receive(&s,pos,sizeof(pos),306);
    MotorService_Process(&s,307); CHECK(s.data.online);
    return 1;
}

int main(void)
{
    if (!test_protocol() || !test_service()) return 1;
    printf("PASS: %u checks (protocol + motor service)\n", checks);
    return 0;
}
