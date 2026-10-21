/* Sensor wire substitute. Real flow driver/service run in all QrApp tests. */
#include "flow/bsp_flow_spi.h"
#include <string.h>
static uint8_t address,phase,high=1u;
static uint32_t ticks;
static unsigned failed,bursts;
void FlowTest_Fail(unsigned value) { failed=value; }
unsigned FlowTest_Bursts(void) { return bursts; }
static int init(void *ctx) { (void)ctx; high=1u; return 1; }
static void select_cs(void *ctx,uint8_t value) { (void)ctx; high=value; phase=0u; }
static int delay_us(void *ctx,uint32_t us) { (void)ctx; ticks+=us; return 1; }
static uint32_t cycles(void *ctx) { (void)ctx; return ticks; }
static int exchange(void *ctx,const uint8_t *tx,uint8_t *rx,size_t n)
{
    static const uint8_t b[12]={0xB0,0x3F,1,0,0xFF,0xFF,50,40,80,10,1,2};
    (void)ctx; ticks+=(uint32_t)n*12u;
    if (high || failed) return 0;
    if (!phase) { address=tx[0]; phase=1u; rx[0]=0u; return 1; }
    if (address==0x16u && n==12u) { memcpy(rx,b,n); ++bursts; }
    else { memset(rx,0,n); if (address==0x00u) rx[0]=0x49u; if (address==0x5Fu) rx[0]=0xB6u; }
    return 1;
}
FlowIo BspFlowSpi_Io(void)
{ FlowIo io={0,init,select_cs,exchange,delay_us,cycles,1u}; failed=0u; bursts=0u; return io; }
