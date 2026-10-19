#ifndef EMM_PROTOCOL_H
#define EMM_PROTOCOL_H
#include <stddef.h>
#include <stdint.h>

#define EMM_POSITION 0x36u
#define EMM_SPEED    0x35u
#define EMM_STATUS   0x3Au
#define EMM_ENABLE  0xF3u
#define EMM_VELOCITY 0xF6u
#define EMM_MOVE    0xFDu
#define EMM_STOP    0xFEu
#define EMM_ACK_OK  0x02u
#define EMM_ACK_TIMEOUT 0xFFu
#define EMM_ACK_TRANSPORT 0xFCu

typedef struct { uint8_t bytes[16], length, code; } EmmCommand;
typedef struct {
    uint8_t code, result, flags;
    int64_t position;
    int32_t speed;
} EmmFrame;
typedef struct { uint8_t address, used, bytes[32]; uint32_t rejected; } EmmParser;
typedef void (*EmmFrameHandler)(void *context, const EmmFrame *frame);

void EmmParser_Init(EmmParser *p, uint8_t address);
void EmmParser_Feed(EmmParser *p, const uint8_t *data, size_t length,
                    EmmFrameHandler handler, void *context);
EmmCommand Emm_Query(uint8_t address, uint8_t code);
EmmCommand Emm_Enable(uint8_t address, uint8_t enabled);
EmmCommand Emm_Velocity(uint8_t address, uint8_t direction, uint16_t rpm, uint8_t acceleration);
EmmCommand Emm_Move(uint8_t address, uint8_t direction, uint16_t rpm,
                    uint8_t acceleration, uint32_t pulses);
EmmCommand Emm_Stop(uint8_t address);
#endif
