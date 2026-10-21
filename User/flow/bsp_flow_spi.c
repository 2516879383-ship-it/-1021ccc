#include "bsp_flow_spi.h"
#include "flow_config.h"
#include <string.h>
/* HAL SPI MSP is owned by bsp_spi_flash.c, which dispatches SPI2 here. */
static SPI_HandleTypeDef spi;
#ifndef BSP_FLOW_CYCLES
#define BSP_FLOW_CYCLES() (DWT->CYCCNT)
#endif
static uint32_t cycles(void *ctx) { (void)ctx; return BSP_FLOW_CYCLES(); }
static int delay_us(void *ctx,uint32_t us)
{
    uint32_t start=cycles(ctx), limit=us*(SystemCoreClock/1000000u), spins=100000u;
    while ((uint32_t)(cycles(ctx)-start)<limit) { if (--spins==0u) return 0; }
    return 1;
}
static void select_cs(void *ctx,uint8_t high)
{ (void)ctx; HAL_GPIO_WritePin(GPIOE,GPIO_PIN_3,high ? GPIO_PIN_SET : GPIO_PIN_RESET); }
void BspFlowSpi_MspInit(SPI_HandleTypeDef *h)
{
    GPIO_InitTypeDef g;
    if (h->Instance!=SPI2) return;
    __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE(); __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_SPI2_CLK_ENABLE();
    memset(&g,0,sizeof(g));
    /* Set ODR before changing CS into an output. */
    HAL_GPIO_WritePin(GPIOE,GPIO_PIN_3,GPIO_PIN_SET);
    g.Pin=GPIO_PIN_3; g.Mode=GPIO_MODE_OUTPUT_PP; g.Pull=GPIO_PULLUP; g.Speed=GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOE,&g);
    g.Pin=GPIO_PIN_13; g.Mode=GPIO_MODE_AF_PP; g.Alternate=GPIO_AF5_SPI2;
    HAL_GPIO_Init(GPIOB,&g);
    g.Pin=GPIO_PIN_2|GPIO_PIN_3; HAL_GPIO_Init(GPIOC,&g);
}
static int init(void *ctx)
{
    uint32_t before; unsigned i;
    (void)ctx;
    __HAL_RCC_SPI2_FORCE_RESET(); __HAL_RCC_SPI2_RELEASE_RESET();
    memset(&spi,0,sizeof(spi)); spi.Instance=SPI2;
    spi.Init.Mode=SPI_MODE_MASTER; spi.Init.Direction=SPI_DIRECTION_2LINES;
    spi.Init.DataSize=SPI_DATASIZE_8BIT; spi.Init.CLKPolarity=SPI_POLARITY_HIGH;
    spi.Init.CLKPhase=SPI_PHASE_2EDGE; spi.Init.NSS=SPI_NSS_SOFT;
    spi.Init.BaudRatePrescaler=SPI_BAUDRATEPRESCALER_64; spi.Init.FirstBit=SPI_FIRSTBIT_MSB;
    spi.Init.TIMode=SPI_TIMODE_DISABLE; spi.Init.CRCCalculation=SPI_CRCCALCULATION_DISABLE;
    spi.Init.CRCPolynomial=7u;
    if (HAL_SPI_Init(&spi)!=HAL_OK) return 0;
    select_cs(0,1u);
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; /* Never reset a shared cycle counter. */
    before=cycles(0);
    for (i=0u;i<64u;++i) { if (cycles(0)!=before) return 1; }
    return 0; /* Debug lock/timebase failure must not hang the foreground. */
}
static int exchange(void *ctx,const uint8_t *tx,uint8_t *rx,size_t size)
{
    (void)ctx;
    if (size==0u || size>12u) return 0;
    return HAL_SPI_TransmitReceive(&spi,(uint8_t *)tx,rx,(uint16_t)size,FLOW_SPI_TIMEOUT_MS)==HAL_OK;
}
FlowIo BspFlowSpi_Io(void)
{
    FlowIo io={0,init,select_cs,exchange,delay_us,cycles,0};
    io.cycles_per_us=SystemCoreClock/1000000u;
    return io;
}
