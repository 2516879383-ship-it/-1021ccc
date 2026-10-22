#include "usart/bsp_motor_uart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"UART5 FAIL %d: %s\n",__LINE__,#x); exit(1); } } while (0)
USART_TypeDef fake_usart1, fake_usart3, fake_uart5;
DMA_Stream_TypeDef fake_stream0, fake_stream1, fake_stream7;
GPIO_TypeDef fake_gpiob, fake_gpioc, fake_gpiod;
static UART_HandleTypeDef *uart;
static uint8_t *rx, *tx;
static uint16_t rx_size, tx_size;
static uint32_t tick;
static unsigned aborts, qr_events, qr_errors, gpio, irq, init_fail, dma_fail, rx_fail;
static HAL_StatusTypeDef tx_result;
void UART5_IRQHandler(void);
static unsigned rx_starts, rx_fake, tx_fake, tx_aborts, hal_errors, old_busy;
uint32_t __get_PRIMASK(void) { return 0u; }
void __disable_irq(void) {}
void __set_PRIMASK(uint32_t v) { (void)v; }
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u)
{ uart=u; BspMotorUart_MspInit(u); u->gState=u->RxState=HAL_UART_STATE_READY; return init_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{ d->State=old_busy && d->Instance==DMA1_Stream0 ? HAL_DMA_STATE_BUSY : HAL_DMA_STATE_READY;
  d->Instance->CR=0u; return dma_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *d)
{ d->State=HAL_DMA_STATE_READY; d->Instance->CR=0u; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{ ++rx_starts; rx=b; rx_size=n; u->hdmarx->Instance->count=n;
  u->RxState=HAL_UART_STATE_BUSY_RX; u->Instance->CR3|=USART_CR3_DMAR;
  __HAL_UART_ENABLE_IT(u,UART_IT_PE); __HAL_UART_ENABLE_IT(u,UART_IT_ERR);
  if(!rx_fake && !rx_fail) { u->hdmarx->State=HAL_DMA_STATE_BUSY; u->hdmarx->Instance->CR|=DMA_SxCR_EN; }
  if(rx_fake>=2u) u->hdmarx->State=HAL_DMA_STATE_BUSY;
  if(rx_fake>=3u) u->hdmarx->Instance->CR|=DMA_SxCR_EN;
  if(rx_fake==3u) u->Instance->CR3&=~USART_CR3_DMAR;
  if(rx_fake==4u) u->RxState=HAL_UART_STATE_READY;
  u->ErrorCode=rx_fake==5u ? 1u : HAL_UART_ERROR_NONE;
  return rx_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{ if (tx_result!=HAL_OK) return tx_result; tx=b; tx_size=n; u->gState=HAL_UART_STATE_BUSY_TX;
  u->Instance->CR3|=USART_CR3_DMAT;
  if(!tx_fake) { u->hdmatx->State=HAL_DMA_STATE_BUSY; u->hdmatx->Instance->CR|=DMA_SxCR_EN; }
  return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ ++aborts; ++tx_aborts; u->gState=HAL_UART_STATE_READY; u->hdmatx->State=HAL_DMA_STATE_READY;
  u->hdmatx->Instance->CR=0u; u->Instance->CR3&=~USART_CR3_DMAT; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u)
{ ++aborts; u->RxState=HAL_UART_STATE_READY; u->hdmarx->State=HAL_DMA_STATE_READY;
  u->hdmarx->Instance->CR=0u; u->Instance->CR3&=~USART_CR3_DMAR; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g)
{ CHECK(g->Alternate==GPIO_AF8_UART5 && ((p==GPIOC && g->Pin==GPIO_PIN_12) || (p==GPIOD && g->Pin==GPIO_PIN_2))); ++gpio; }
void HAL_NVIC_SetPriority(unsigned i,unsigned p,unsigned sub)
{ (void)i; CHECK(p==1u && sub==0u); ++irq; }
void HAL_NVIC_EnableIRQ(unsigned i) { (void)i; }
void HAL_NVIC_DisableIRQ(unsigned i) { (void)i; }
void HAL_NVIC_ClearPendingIRQ(unsigned i) { (void)i; }
void HAL_UART_IRQHandler(UART_HandleTypeDef *u)
{
  if((u->Instance->SR & (UART_FLAG_PE|UART_FLAG_FE|UART_FLAG_NE|UART_FLAG_ORE)) &&
     (__HAL_UART_GET_IT_SOURCE(u,UART_IT_PE) || __HAL_UART_GET_IT_SOURCE(u,UART_IT_ERR))) {
    ++hal_errors; HAL_UART_ErrorCallback(u); __HAL_UART_CLEAR_OREFLAG(u); return;
  }
  if((u->Instance->SR & UART_FLAG_TC) && __HAL_UART_GET_IT_SOURCE(u,UART_IT_TC)) {
    u->Instance->SR&=~UART_FLAG_TC; __HAL_UART_DISABLE_IT(u,UART_IT_TC);
    u->gState=HAL_UART_STATE_READY; u->hdmatx->State=HAL_DMA_STATE_READY;
    u->hdmatx->Instance->CR=0u; HAL_UART_TxCpltCallback(u);
  }
}
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *d) { (void)d; }
void BspQrUart_RxEvent(void) { ++qr_events; }
void BspQrUart_Error(void) { ++qr_errors; }
static void inject(unsigned count)
{
    unsigned i;
    const uint8_t b[4]={1,EMM_STATUS,3,0x6B};
    for(i=0;i<count;++i) {
        rx[rx_size-fake_stream0.count]=b[i%4u]; --fake_stream0.count;
        if(fake_stream0.count==rx_size/2u) HAL_UART_RxHalfCpltCallback(uart);
        if(!fake_stream0.count) { fake_stream0.count=rx_size; HAL_UART_RxCpltCallback(uart); }
    }
    BspMotorUart_RxEvent();
}
int main(int argc,char **argv)
{
    MotorTransport t; MotorBus bus; uint8_t bytes[128], saved[128];
    UART_HandleTypeDef qr={0}, other={0}; unsigned i;
    if(argc>1) {
        init_fail=(unsigned)!strcmp(argv[1],"init"); dma_fail=(unsigned)!strcmp(argv[1],"dma");
        rx_fail=(unsigned)!strcmp(argv[1],"receive");
        rx_fake=(unsigned)!strcmp(argv[1],"start");
    }
    t=BspMotorUart_Init(); MotorBus_Init(&bus,t,0u);
    if(argc>1 && !strcmp(argv[1],"review")) {
        const uint32_t faults[]={UART_FLAG_PE,UART_FLAG_FE,UART_FLAG_NE,UART_FLAG_ORE};
        uint32_t frames,errors; unsigned k,starts;
        for(k=0;k<12u;++k) {
            tick=200u*k; frames=bus.rx_frames; errors=bus.transport_errors;
            /* A prefix queued before the error must not survive the recovery. */
            bus.parser.bytes[0]=1u; bus.parser.used=1u;
            memcpy(rx,(uint8_t[]){1,EMM_STATUS,3,0x6B},4u); fake_stream0.count=rx_size-4u;
            fake_uart5.SR=(k<8u ? UART_FLAG_IDLE : 0u)|faults[k%4u];
            if(k>=4u) HAL_UART_RxHalfCpltCallback(uart); /* DMA before UART */
            UART5_IRQHandler(); BspMotorUart_PumpBus(&bus,tick);
            CHECK(bus.transport_errors>errors && bus.rx_frames==frames && !bus.parser.used);
            CHECK(!t.busy(0)); inject(4u); BspMotorUart_PumpBus(&bus,tick);
            CHECK(bus.rx_frames==frames+1u);
        }
        CHECK(t.send(0,(uint8_t[]){1,EMM_STOP,0x98,0,0x6B},5u));
        starts=tx_aborts; HAL_UART_ErrorCallback(uart); BspMotorUart_PumpBus(&bus,++tick);
        CHECK(tx_aborts==starts && t.busy(0)); /* RX error must not cut TX */
        __HAL_UART_ENABLE_IT(uart,UART_IT_TC); fake_uart5.SR|=UART_FLAG_TC; UART5_IRQHandler();
        CHECK(!t.busy(0));
        CHECK(t.send(0,(uint8_t[]){1,EMM_STOP,0x98,0,0x6B},5u));
        starts=tx_aborts; __HAL_UART_ENABLE_IT(uart,UART_IT_TC);
        fake_uart5.SR=UART_FLAG_ORE|UART_FLAG_IDLE|UART_FLAG_TC;
        UART5_IRQHandler(); BspMotorUart_PumpBus(&bus,++tick);
        CHECK(tx_aborts==starts && !t.busy(0)); /* simultaneous RX error and TX TC */
        rx_fake=1u; HAL_UART_ErrorCallback(uart); BspMotorUart_PumpBus(&bus,++tick);
        CHECK(t.busy(0)); starts=rx_starts;
        for(i=0;i<99u;++i) BspMotorUart_PumpBus(&bus,tick+i);
        CHECK(rx_starts==starts); /* bounded restart, including another false OK */
        tick+=100u; BspMotorUart_PumpBus(&bus,tick); CHECK(rx_starts==starts+1u && t.busy(0));
        rx_fake=0u; tick+=100u; BspMotorUart_PumpBus(&bus,tick); CHECK(!t.busy(0));
        for(k=2u;k<=5u;++k) {
            rx_fake=k; HAL_UART_ErrorCallback(uart); BspMotorUart_PumpBus(&bus,++tick);
            CHECK(t.busy(0)); rx_fake=0u; tick+=100u;
            BspMotorUart_PumpBus(&bus,tick); CHECK(!t.busy(0));
        }
        old_busy=1u; HAL_UART_ErrorCallback(uart); starts=rx_starts;
        BspMotorUart_PumpBus(&bus,++tick); CHECK(t.busy(0) && rx_starts==starts);
        old_busy=0u; tick+=100u; BspMotorUart_PumpBus(&bus,tick); CHECK(!t.busy(0));
        uart->hdmatx->State=HAL_DMA_STATE_READY; uart->hdmatx->Instance->CR=0u; tx_fake=1u;
        CHECK(!t.send(0,(uint8_t[]){1,EMM_STOP,0x98,0,0x6B},5u));
        printf("PASS: UART review errors=%lu starts=%u HAL-visible=%u\n",(unsigned long)bus.transport_errors,rx_starts,hal_errors);
        return 0;
    }
    if(init_fail || dma_fail || rx_fail || rx_fake) {
        unsigned starts;
        CHECK(t.busy(t.context)); BspMotorUart_PumpBus(&bus,0); CHECK(bus.transport_errors);
        starts=rx_starts;
        for(i=1u;i<100u;++i) BspMotorUart_PumpBus(&bus,i);
        CHECK(t.busy(0) && rx_starts==starts);
        init_fail=dma_fail=rx_fail=rx_fake=0u; tick=100u; BspMotorUart_PumpBus(&bus,tick);
        CHECK(rx!=0 && !t.busy(0)); inject(4u); BspMotorUart_PumpBus(&bus,tick); CHECK(bus.rx_frames==1u);
        printf("PASS: UART5 initial failure/retry/recovery (%s)\n",argv[1]); return 0;
    }
    CHECK(gpio==2u && irq==3u && uart->hdmatx->Instance==DMA1_Stream7 && rx_size==256u);
    for(i=0;i<128u;++i) bytes[i]=(uint8_t)i;
    memcpy(saved,bytes,sizeof(bytes));
    tx_result=HAL_BUSY; CHECK(!t.send(t.context,bytes,128u));
    BspMotorUart_PumpBus(&bus,0u); CHECK(!bus.transport_errors);
    tx_result=HAL_OK; CHECK(t.send(t.context,bytes,128u) && tx_size==128u);
    memset(bytes,0,sizeof(bytes)); CHECK(!memcmp(tx,saved,128u));
    CHECK(!t.send(t.context,bytes,37u)); CHECK(!memcmp(tx,saved,128u));
    tick=20u; BspMotorUart_PumpBus(&bus,tick); CHECK(!aborts);
    uart->gState=HAL_UART_STATE_READY; uart->hdmatx->State=HAL_DMA_STATE_READY;
    uart->hdmatx->Instance->CR=0u; HAL_UART_TxCpltCallback(uart);
    tick=40u; BspMotorUart_PumpBus(&bus,tick); CHECK(!aborts && !bus.transport_errors);
    CHECK(!t.send(t.context,bytes,0u) && !t.send(t.context,bytes,129u));
    inject(192u); BspMotorUart_PumpBus(&bus,tick); CHECK(bus.rx_frames==32u);
    BspMotorUart_PumpBus(&bus,tick); CHECK(bus.rx_frames==48u);
    qr.Instance=USART3; other.Instance=USART1;
    HAL_UART_RxCpltCallback(&qr); HAL_UART_ErrorCallback(&qr); HAL_UART_ErrorCallback(&other);
    CHECK(qr_events==1u && qr_errors==1u && !bus.transport_errors);
    inject(800u); BspMotorUart_PumpBus(&bus,tick); CHECK(bus.transport_errors==1u && !bus.nodes[0].data.valid);
    HAL_UART_ErrorCallback(uart); BspMotorUart_PumpBus(&bus,41u); CHECK(aborts && bus.transport_errors==2u);
    tick=50u; CHECK(t.send(t.context,bytes,128u));
    tick=81u; BspMotorUart_PumpBus(&bus,tick); CHECK(bus.transport_errors==2u);
    tick=82u; BspMotorUart_PumpBus(&bus,tick); CHECK(bus.transport_errors==3u);
    CHECK(!t.busy(t.context));
    printf("PASS: %u real UART5 BSP checks (DMA lifetime, length watchdog, budget, overflow, callbacks)\n",checks);
    return 0;
}
