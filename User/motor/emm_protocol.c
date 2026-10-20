#include "emm_protocol.h"
#include <string.h>

static EmmCommand command(uint8_t address, uint8_t code, uint8_t length)
{
    EmmCommand c;
    memset(&c, 0, sizeof(c));
    c.bytes[0] = address; c.bytes[1] = code;
    c.bytes[length - 1u] = 0x6Bu; c.code = code; c.length = length;
    return c;
}

EmmCommand Emm_Query(uint8_t address, uint8_t code) { return command(address, code, 3u); }
EmmCommand Emm_Enable(uint8_t address, uint8_t enabled)
{
    EmmCommand c = command(address, EMM_ENABLE, 6u);
    c.bytes[2] = 0xABu; c.bytes[3] = enabled ? 1u : 0u; return c;
}
EmmCommand Emm_Velocity(uint8_t address, uint8_t direction, uint16_t rpm, uint8_t acceleration)
{
    EmmCommand c = command(address, EMM_VELOCITY, 8u);
    c.bytes[2] = direction; c.bytes[3] = (uint8_t)(rpm >> 8);
    c.bytes[4] = (uint8_t)rpm; c.bytes[5] = acceleration; return c;
}
EmmCommand Emm_Move(uint8_t address, uint8_t direction, uint16_t rpm,
                    uint8_t acceleration, uint32_t pulses)
{
    EmmCommand c = command(address, EMM_MOVE, 13u);
    c.bytes[2] = direction; c.bytes[3] = (uint8_t)(rpm >> 8);
    c.bytes[4] = (uint8_t)rpm; c.bytes[5] = acceleration;
    c.bytes[6] = (uint8_t)(pulses >> 24); c.bytes[7] = (uint8_t)(pulses >> 16);
    c.bytes[8] = (uint8_t)(pulses >> 8); c.bytes[9] = (uint8_t)pulses;
    return c; /* relative movement, no synchronous cache */
}
EmmCommand Emm_Stop(uint8_t address)
{
    EmmCommand c = command(address, EMM_STOP, 5u); c.bytes[2] = 0x98u; return c;
}

EmmCommand Emm_VelocityEx(uint8_t address, uint8_t direction, uint16_t rpm,
                          uint8_t acceleration, uint8_t cached)
{
    EmmCommand c = Emm_Velocity(address, direction, rpm, acceleration);
    if (!address || address > 7u || direction > 1u || rpm > 3000u || cached > 1u) c.length = 0u;
    c.bytes[6] = cached; return c;
}
EmmCommand Emm_MoveEx(uint8_t address, uint8_t direction, uint16_t rpm,
    uint8_t acceleration, uint32_t pulses, EmmPositionMode mode, uint8_t cached)
{
    EmmCommand c = Emm_Move(address, direction, rpm, acceleration, pulses);
    if (!address || address > 7u || direction > 1u || rpm > 3000u ||
        (unsigned)mode > 2u || cached > 1u) c.length = 0u;
    c.bytes[10] = (uint8_t)mode; c.bytes[11] = cached; return c;
}
int Emm_Multi(EmmCommand *out, const EmmCommand *m, uint8_t count)
{
    EmmCommand c;
    unsigned i, length = 5u, seen = 0u;
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    if (!m || !count || count > 7u) return 0;
    c = command(0u, EMM_MULTI, 5u);
    for (i = 0u; i < count; ++i) {
        unsigned a = m[i].bytes[0];
        if (!a || a > 7u || (seen & (1u << a)) || m[i].code != m[0].code ||
            (m[i].code != EMM_VELOCITY && m[i].code != EMM_MOVE) ||
            m[i].bytes[1] != m[i].code || m[i].bytes[2] > 1u ||
            (((unsigned)m[i].bytes[3] << 8) | m[i].bytes[4]) > 3000u ||
            (m[i].code == EMM_MOVE && m[i].bytes[10] > 2u) ||
            m[i].length != (m[i].code == EMM_MOVE ? 13u : 8u) ||
            m[i].bytes[m[i].length - 1u] != 0x6Bu ||
            m[i].bytes[m[i].length - 2u] != 0u ||
            length + m[i].length > sizeof(c.bytes)) return 0;
        seen |= 1u << a;
        memcpy(c.bytes + length - 1u, m[i].bytes, m[i].length);
        length += m[i].length;
    }
    c.bytes[2] = (uint8_t)(length >> 8); c.bytes[3] = (uint8_t)length;
    c.bytes[length - 1u] = 0x6Bu; c.length = (uint8_t)length;
    *out = c; return 1;
}
int Emm_Wheels(EmmCommand *out, const int32_t rpm[4], uint8_t acceleration)
{
    EmmCommand m[4];
    unsigned i;
    if (!out || !rpm) return 0;
    memset(out, 0, sizeof(*out));
    for (i = 0u; i < 4u; ++i) {
        if (rpm[i] < -3000 || rpm[i] > 3000) return 0;
        m[i] = Emm_VelocityEx((uint8_t)(i + 1u), (uint8_t)(rpm[i] < 0),
            (uint16_t)(rpm[i] < 0 ? -rpm[i] : rpm[i]), acceleration, 0u);
    }
    return Emm_Multi(out, m, 4u);
}
EmmCommand Emm_SyncTrigger(void)
{ EmmCommand c = command(0u, EMM_SYNC, 4u); c.bytes[2] = 0x66u; return c; }

void EmmParser_Init(EmmParser *p, uint8_t address)
{
    memset(p, 0, sizeof(*p)); p->address = address;
}

static uint8_t frame_length(uint8_t code)
{
    switch (code) {
    case EMM_VERSION: return 7u;
    case EMM_POSITION: return 8u;
    case EMM_SPEED: return 6u;
    case EMM_STATUS: case EMM_ENABLE: case EMM_VELOCITY:
    case EMM_MOVE: case EMM_STOP: case EMM_SYNC: return 4u;
    default: return 0u;
    }
}

static void discard(EmmParser *p, uint8_t count)
{
    p->used = (uint8_t)(p->used - count);
    memmove(p->bytes, p->bytes + count, p->used);
}

static int decode(const uint8_t *b, EmmFrame *f)
{
    uint32_t magnitude;
    memset(f, 0, sizeof(*f)); f->address = b[0]; f->code = b[1];
    if (b[1] == EMM_POSITION || b[1] == EMM_SPEED) {
        if (b[2] > 1u) return 0;
        if (b[1] == EMM_POSITION) {
            magnitude = ((uint32_t)b[3] << 24) | ((uint32_t)b[4] << 16) |
                        ((uint32_t)b[5] << 8) | b[6];
            f->position = b[2] ? -(int64_t)magnitude : (int64_t)magnitude;
        } else {
            magnitude = ((uint32_t)b[3] << 8) | b[4];
            f->speed = b[2] ? -(int32_t)magnitude : (int32_t)magnitude;
        }
    } else if (b[1] == EMM_STATUS) f->flags = b[2];
    else if (b[1] == EMM_VERSION) {
        f->firmware = (uint16_t)(((uint16_t)b[2] << 8) | b[3]);
        f->hardware = (uint16_t)(((uint16_t)b[4] << 8) | b[5]);
    }
    else {
        if (b[2] != 2u && b[2] != 0x12u && b[2] != 0x22u &&
            b[2] != 0xE2u && b[2] != 0xEEu && b[2] != 0x9Fu) return 0;
        f->result = b[2];
    }
    return 1;
}

void EmmParser_Feed(EmmParser *p, const uint8_t *data, size_t length,
                    EmmFrameHandler handler, void *context)
{
    size_t i;
    uint8_t wanted;
    EmmFrame frame;
    for (i = 0; i < length; ++i) {
        if (p->used == sizeof(p->bytes)) { discard(p, 1u); ++p->rejected; }
        p->bytes[p->used++] = data[i];
        while (p->used >= 2u) {
            wanted = frame_length(p->bytes[1]);
            if ((p->address ? p->bytes[0] != p->address :
                 (p->bytes[0] < 1u || p->bytes[0] > 7u)) || !wanted) {
                discard(p, 1u); ++p->rejected; continue;
            }
            if (p->used < wanted) break;
            if (p->bytes[wanted - 1u] != 0x6Bu || !decode(p->bytes, &frame)) {
                discard(p, 1u); ++p->rejected; continue;
            }
            discard(p, wanted);
            if (handler) handler(context, &frame);
        }
    }
}
