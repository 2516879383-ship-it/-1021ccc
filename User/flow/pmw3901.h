#ifndef PMW3901_H
#define PMW3901_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    void *ctx;
    int (*init)(void *ctx);
    void (*select)(void *ctx, uint8_t high);
    int (*exchange)(void *ctx, const uint8_t *tx, uint8_t *rx, size_t size);
    int (*delay_us)(void *ctx, uint32_t us);
    uint32_t (*cycles)(void *ctx);
    uint32_t cycles_per_us;
} FlowIo;
typedef struct {
    int16_t dx, dy;
    uint16_t shutter;
    uint8_t motion, observation, squal, raw_sum, raw_max, raw_min;
} PmwMotion;
typedef struct {
    uint32_t due;
    uint16_t step;
    uint8_t state, product, inverse, revision;
} Pmw3901;
enum { PMW_PENDING, PMW_READY, PMW_SPI_ERROR, PMW_ID_ERROR, PMW_DATA_ERROR };
void Pmw_Begin(Pmw3901 *p, uint32_t now);
int Pmw_Step(Pmw3901 *p, const FlowIo *io, uint32_t now);
int Pmw_Read(const FlowIo *io, uint8_t reg, uint8_t *data, size_t size);
int Pmw_Write(const FlowIo *io, uint8_t reg, uint8_t value);
int Pmw_CheckId(Pmw3901 *p, const FlowIo *io);
int Pmw_Burst(const FlowIo *io, PmwMotion *motion);
void Pmw_Decode(const uint8_t data[12], PmwMotion *motion);
#endif
