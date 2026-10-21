#include "flow/bsp_flow_spi.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks, clocks, reset_n, config_n, exchanges;
static uint32_t tick_cycles=UINT32_MAX-10000u;
static uint16_t cs;
static int spi_fail, frozen;
GPIO_TypeDef fake_gpiob,fake_gpioc,fake_gpioe;
SPI_TypeDef fake_spi2;
FlowDwt fake_dwt;
FlowDebug fake_debug;
uint32_t SystemCoreClock=180000000u;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FLOW SPI FAIL %d: %s\n",__LINE__,#x); exit(1); } } while(0)
uint32_t FakeFlowCycles(void) { if (!frozen) tick_cycles+=180u; return tick_cycles; }
void FakeFlowRcc(unsigned what) { ++clocks; if (what==3u) ++reset_n; }
void HAL_GPIO_WritePin(GPIO_TypeDef *port,uint16_t pin,uint32_t state)
{ CHECK(port==GPIOE && pin==GPIO_PIN_3); cs=(uint16_t)state; }
void HAL_GPIO_Init(GPIO_TypeDef *port,GPIO_InitTypeDef *g)
{
    ++config_n;
    if (port==GPIOE) CHECK(g->Pin==GPIO_PIN_3 && g->Mode==GPIO_MODE_OUTPUT_PP && cs==GPIO_PIN_SET);
    else if (port==GPIOB) CHECK(g->Pin==GPIO_PIN_13 && g->Mode==GPIO_MODE_AF_PP && g->Alternate==5u);
    else CHECK(port==GPIOC && g->Pin==(GPIO_PIN_2|GPIO_PIN_3) && g->Mode==GPIO_MODE_AF_PP);
}
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h)
{
    CHECK(h->Instance==SPI2 && h->Init.Mode==SPI_MODE_MASTER && h->Init.DataSize==8u);
    CHECK(h->Init.BaudRatePrescaler==64u && h->Init.CLKPolarity==1u && h->Init.CLKPhase==2u);
    CHECK(h->Init.NSS==SPI_NSS_SOFT && h->Init.FirstBit==SPI_FIRSTBIT_MSB);
    BspFlowSpi_MspInit(h); return spi_fail ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h,uint8_t *tx,uint8_t *rx,uint16_t size,uint32_t timeout)
{
    CHECK(h->Instance==SPI2 && timeout==1u && size<=12u && cs==GPIO_PIN_RESET); ++exchanges;
    memset(rx,0x49,size); (void)tx; return spi_fail ? HAL_TIMEOUT : HAL_OK;
}
int main(void)
{
    FlowIo io=BspFlowSpi_Io(); uint8_t byte; uint32_t at;
    CHECK(io.cycles_per_us==180u && io.init(io.ctx)); CHECK(config_n==3u && reset_n==1u);
    at=tick_cycles; CHECK(io.delay_us(io.ctx,200u)); CHECK((uint32_t)(tick_cycles-at)>=36000u);
    CHECK(Pmw_Read(&io,0x00,&byte,1u) && byte==0x49u && cs==GPIO_PIN_SET && exchanges==2u);
    spi_fail=1; CHECK(!Pmw_Read(&io,0x00,&byte,1u) && cs==GPIO_PIN_SET);
    CHECK(!io.init(io.ctx)); spi_fail=0; CHECK(io.init(io.ctx) && reset_n==3u);
    frozen=1; CHECK(!io.init(io.ctx)); CHECK(!io.delay_us(io.ctx,50u));
    CHECK(cs==GPIO_PIN_SET && clocks>0u);
    printf("FLOW SPI PASS %u checks\n",checks); return 0;
}
