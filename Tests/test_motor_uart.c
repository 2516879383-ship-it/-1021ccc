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
uint32_t __get_PRIMASK(void) { return 0u; }
void __disable_irq(void) {}
void __set_PRIMASK(uint32_t v) { (void)v; }
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u)
{ uart=u; BspMotorUart_MspInit(u); return init_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{ (void)d; return dma_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{ rx=b; rx_size=n; u->hdmarx->Instance->count=n; return rx_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{ if (tx_result!=HAL_OK) return tx_result; tx=b; tx_size=n; u->gState=HAL_UART_STATE_BUSY_TX; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *u)
{ ++aborts; u->gState=HAL_UART_STATE_READY; return HAL_OK; }
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *u) { (void)u; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *g)
{ CHECK(g->Alternate==GPIO_AF8_UART5 && ((p==GPIOC && g->Pin==GPIO_PIN_12) || (p==GPIOD && g->Pin==GPIO_PIN_2))); ++gpio; }
void HAL_NVIC_SetPriority(unsigned i,unsigned p,unsigned sub)
{ (void)i; CHECK(p==1u && sub==0u); ++irq; }
void HAL_NVIC_EnableIRQ(unsigned i) { (void)i; }
void HAL_NVIC_DisableIRQ(unsigned i) { (void)i; }
void HAL_NVIC_ClearPendingIRQ(unsigned i) { (void)i; }
void HAL_UART_IRQHandler(UART_HandleTypeDef *u) { (void)u; }
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
    }
    t=BspMotorUart_Init(); MotorBus_Init(&bus,t,0u);
    if(init_fail || dma_fail) { CHECK(t.busy(t.context)); BspMotorUart_PumpBus(&bus,0); CHECK(bus.transport_errors); puts("PASS: UART5 init failure"); return 0; }
    if(rx_fail) { BspMotorUart_PumpBus(&bus,0); CHECK(bus.transport_errors); rx_fail=0; BspMotorUart_PumpBus(&bus,1); CHECK(rx!=0); puts("PASS: UART5 RX failure/recovery"); return 0; }
    CHECK(gpio==2u && irq==3u && uart->hdmatx->Instance==DMA1_Stream7 && rx_size==256u);
    for(i=0;i<128u;++i) bytes[i]=(uint8_t)i;
    memcpy(saved,bytes,sizeof(bytes));
    tx_result=HAL_BUSY; CHECK(!t.send(t.context,bytes,128u));
    BspMotorUart_PumpBus(&bus,0u); CHECK(!bus.transport_errors);
    tx_result=HAL_OK; CHECK(t.send(t.context,bytes,128u) && tx_size==128u);
    memset(bytes,0,sizeof(bytes)); CHECK(!memcmp(tx,saved,128u));
    CHECK(!t.send(t.context,bytes,37u)); CHECK(!memcmp(tx,saved,128u));
    tick=20u; BspMotorUart_PumpBus(&bus,tick); CHECK(!aborts);
    uart->gState=HAL_UART_STATE_READY; HAL_UART_TxCpltCallback(uart);
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
