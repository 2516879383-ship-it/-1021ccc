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

void EmmParser_Init(EmmParser *p, uint8_t address)
{
    memset(p, 0, sizeof(*p)); p->address = address;
}

static uint8_t frame_length(uint8_t code)
{
    switch (code) {
    case EMM_POSITION: return 8u;
    case EMM_SPEED: return 6u;
    case EMM_STATUS: case EMM_ENABLE: case EMM_VELOCITY:
    case EMM_MOVE: case EMM_STOP: return 4u;
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
    memset(f, 0, sizeof(*f)); f->code = b[1];
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
    else {
        if (b[2] != 2u && b[2] != 0xE2u && b[2] != 0xEEu && b[2] != 0x9Fu) return 0;
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
            if (p->bytes[0] != p->address || !wanted) {
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
