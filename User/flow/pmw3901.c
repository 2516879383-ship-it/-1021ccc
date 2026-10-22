/* PMW3901 Arduino driver
 * Copyright (c) 2017 Bitcraze AB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* Optimisation table adapted from Bitcraze Arduino, commit
   d322d98d6f61757352d11d922cd194539e165231. See THIRD_PARTY_NOTICES.md.
   All waits are foreground deadlines; one table entry per call. */
/*
 * PMW3901 寄存器驱动：保持上游优化表和许可声明，提供 SPI 事务、运动突发解码及分步启动。
 * 毫秒等待存为下次调用期限，微秒级片选时序由可超时的适配接口完成。
 */
#include "pmw3901.h"
#include <string.h>
static const struct { uint8_t reg, value; uint16_t wait_ms; } setup[] = {
{0x7F, 0x00, 0},
{0x61, 0xAD, 0},
{0x7F, 0x03, 0},
{0x40, 0x00, 0},
{0x7F, 0x05, 0},
{0x41, 0xB3, 0},
{0x43, 0xF1, 0},
{0x45, 0x14, 0},
{0x5B, 0x32, 0},
{0x5F, 0x34, 0},
{0x7B, 0x08, 0},
{0x7F, 0x06, 0},
{0x44, 0x1B, 0},
{0x40, 0xBF, 0},
{0x4E, 0x3F, 0},
{0x7F, 0x08, 0},
{0x65, 0x20, 0},
{0x6A, 0x18, 0},
{0x7F, 0x09, 0},
{0x4F, 0xAF, 0},
{0x5F, 0x40, 0},
{0x48, 0x80, 0},
{0x49, 0x80, 0},
{0x57, 0x77, 0},
{0x60, 0x78, 0},
{0x61, 0x78, 0},
{0x62, 0x08, 0},
{0x63, 0x50, 0},
{0x7F, 0x0A, 0},
{0x45, 0x60, 0},
{0x7F, 0x00, 0},
{0x4D, 0x11, 0},
{0x55, 0x80, 0},
{0x74, 0x1F, 0},
{0x75, 0x1F, 0},
{0x4A, 0x78, 0},
{0x4B, 0x78, 0},
{0x44, 0x08, 0},
{0x45, 0x50, 0},
{0x64, 0xFF, 0},
{0x65, 0x1F, 0},
{0x7F, 0x14, 0},
{0x65, 0x60, 0},
{0x66, 0x08, 0},
{0x63, 0x78, 0},
{0x7F, 0x15, 0},
{0x48, 0x58, 0},
{0x7F, 0x07, 0},
{0x41, 0x0D, 0},
{0x43, 0x14, 0},
{0x4B, 0x0E, 0},
{0x45, 0x0F, 0},
{0x44, 0x42, 0},
{0x4C, 0x80, 0},
{0x7F, 0x10, 0},
{0x5B, 0x02, 0},
{0x7F, 0x07, 0},
{0x40, 0x41, 0},
{0x70, 0x00, 0},
{0, 0, 100},
{0x32, 0x44, 0},
{0x7F, 0x07, 0},
{0x40, 0x40, 0},
{0x7F, 0x06, 0},
{0x62, 0xf0, 0},
{0x63, 0x00, 0},
{0x7F, 0x0D, 0},
{0x48, 0xC0, 0},
{0x6F, 0xd5, 0},
{0x7F, 0x00, 0},
{0x5B, 0xa0, 0},
{0x4E, 0xA8, 0},
{0x5A, 0x50, 0},
{0x40, 0x80, 0},
};
static int reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
/*
 * 封装一次寄存器读写，满足地址/数据/片选保持时序；
 * 即使中途失败也执行片选释放路径，防止占住 SPI 器件。
 */
static int transaction(const FlowIo *io, uint8_t address, uint8_t *data, size_t n, int write)
{
    uint8_t unused, zeros[12] = {0};
    int ok;
    io->select(io->ctx, 0u);
    ok = io->delay_us(io->ctx, 50u);
    if (ok) ok = io->exchange(io->ctx, &address, &unused, 1u);
    if (ok && !write) ok = io->delay_us(io->ctx, 50u); /* tSRAD >= 35us */
    if (ok) ok = io->exchange(io->ctx, write ? data : zeros, write ? &unused : data, n);
    /* Always hold and release CS, even after a partly consumed read. */
    if (!io->delay_us(io->ctx, 50u)) ok = 0; /* write hold >=35us */
    io->select(io->ctx, 1u);
    if (!io->delay_us(io->ctx, write ? 200u : 50u)) ok = 0;
    return ok;
}
int Pmw_Read(const FlowIo *io, uint8_t reg, uint8_t *data, size_t size)
{
    if (size == 0u || size > 12u) return 0;
    return transaction(io, (uint8_t)(reg & 0x7Fu), data, size, 0);
}
int Pmw_Write(const FlowIo *io, uint8_t reg, uint8_t value)
{ return transaction(io, (uint8_t)(reg | 0x80u), &value, 1u, 1); }
/*
 * 读取产品号、反码和版本；产品/反码都匹配后才认定器件身份有效。
 */
int Pmw_CheckId(Pmw3901 *p, const FlowIo *io)
{
    if (!Pmw_Read(io, 0x00u, &p->product, 1u) ||
        !Pmw_Read(io, 0x5Fu, &p->inverse, 1u) ||
        !Pmw_Read(io, 0x01u, &p->revision, 1u)) return PMW_SPI_ERROR;
    return p->product == 0x49u && p->inverse == 0xB6u ? PMW_READY : PMW_ID_ERROR;
}
static int16_t signed_word(uint8_t lo, uint8_t hi)
{
    uint16_t v = (uint16_t)((uint16_t)hi * 256u + lo);
    return (int16_t)(v < 32768u ? (int32_t)v : (int32_t)v - 65536);
}
/*
 * 解码 12 字节运动突发：dx/dy 为小端有符号数，shutter 高字节在前，
 * 同时保留质量、运动标志和原始亮度供服务层判断跟踪有效性。
 */
void Pmw_Decode(const uint8_t b[12], PmwMotion *m)
{
    m->motion=b[0]; m->observation=b[1];
    m->dx=signed_word(b[2],b[3]); m->dy=signed_word(b[4],b[5]);
    m->squal=b[6]; m->raw_sum=b[7]; m->raw_max=b[8]; m->raw_min=b[9];
    /* Burst shutter is upper byte first, unlike little-endian deltas. */
    m->shutter=(uint16_t)((uint16_t)b[10]*256u+b[11]);
}
/*
 * 读取一致的运动突发，拒绝全 00/全 FF 总线异常响应；质量门控由光流服务继续判断。
 */
int Pmw_Burst(const FlowIo *io, PmwMotion *m)
{
    uint8_t b[12]; unsigned i; uint8_t any=0u, all=0xFFu;
    if (!Pmw_Read(io,0x16u,b,sizeof(b))) return PMW_SPI_ERROR;
    Pmw_Decode(b,m);
    for (i=0u;i<12u;++i) { any |= b[i]; all &= b[i]; }
    return any == 0u || all == 0xFFu ? PMW_DATA_ERROR : PMW_READY;
}
void Pmw_Begin(Pmw3901 *p, uint32_t now)
{ memset(p,0,sizeof(*p)); p->due=now+40u; }
/*
 * 上电/恢复初始化状态机：复位、识别、清启动数据、逐项写优化表、等待并复核。
 * 就绪前消费一次不确定的启动位移而不发布，避免把重启期间运动计入新采样段。
 */
int Pmw_Step(Pmw3901 *p, const FlowIo *io, uint32_t now)
{
    uint8_t discard; int result; PmwMotion m;
    if (!reached(now,p->due)) return PMW_PENDING;
    switch (p->state) {
    case 0u: io->select(io->ctx,0u); p->due=now+1u; p->state=1u; break;
    case 1u: io->select(io->ctx,1u); p->due=now+1u; p->state=2u; break;
    case 2u:
        if (!Pmw_Write(io,0x3Au,0x5Au)) return PMW_SPI_ERROR;
        p->due=now+5u; p->state=3u; break;
    case 3u:
        result=Pmw_CheckId(p,io); if (result!=PMW_READY) return result;
        p->step=0u; p->state=4u; break;
    case 4u:
        if (!Pmw_Read(io,(uint8_t)(0x02u+p->step),&discard,1u)) return PMW_SPI_ERROR;
        if (++p->step==5u) { p->step=0u; p->state=5u; p->due=now+1u; }
        break;
    case 5u:
        if (p->step < sizeof(setup)/sizeof(setup[0])) {
            if (setup[p->step].wait_ms) p->due=now+setup[p->step].wait_ms;
            else if (!Pmw_Write(io,setup[p->step].reg,setup[p->step].value)) return PMW_SPI_ERROR;
            ++p->step;
        } else { p->state=6u; p->due=now+50u; }
        break;
    case 6u:
        result=Pmw_CheckId(p,io); if (result!=PMW_READY) return result;
        p->state=7u; break;
    case 7u:
        /* Consume uncertain startup/recovery displacement without publishing it. */
        result=Pmw_Burst(io,&m); if (result!=PMW_READY) return result;
        p->state=8u; return PMW_READY;
    default: return PMW_READY;
    }
    return PMW_PENDING;
}
