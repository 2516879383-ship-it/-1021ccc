#include "app/qr_app.h"
#include "app/app_runtime.h"
#include "usart/bsp_qr_uart.h"
#include "usart/bsp_qr_trigger.h"
#include "usart/bsp_motor_uart.h"
#include "lcd/bsp_lcd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static const char sample[] = "156+123+516+231";
static const char other[] = "123+312+321+123";
static void receive(QrService *s, const char *t, uint32_t at)
{ QrService_Receive(s, (const uint8_t *)t, strlen(t), at); }
static void test_validation(void)
{
    QrTaskCode c, saved;
    const char *bad[] = {"", "156+123+516+23", "156+123+516+231x", "x156+123+516+231",
        "156-123+516+231", "056+123+516+231", "176+123+516+231", "116+123+516+231",
        "156+113+516+231", "156+124+516+231", "156+123+516+230", "156+123+512+231",
        "156+123+566+231", "156+123+516+223", " 156+123+516+231", "156+123+516+231 "};
    uint8_t nonascii[15];
    size_t i;
    CHECK(QrTaskCode_Parse((const uint8_t *)sample, 15u, &c) == QR_CODE_OK);
    CHECK(!strcmp(c.text, sample) && c.colors[0][0] == 1u && c.colors[0][1] == 5u &&
          c.colors[0][2] == 6u && c.colors[1][0] == 5u && c.colors[1][1] == 1u && c.colors[1][2] == 6u);
    CHECK(c.positions[0][0] == 1u && c.positions[0][1] == 2u && c.positions[0][2] == 3u &&
          c.positions[1][0] == 2u && c.positions[1][1] == 3u && c.positions[1][2] == 1u);
    saved = c;
    for (i=0u; i<sizeof(bad)/sizeof(bad[0]); ++i) {
        CHECK(QrTaskCode_Parse((const uint8_t *)bad[i], strlen(bad[i]), &c) != QR_CODE_OK);
        CHECK(!strcmp(c.text, saved.text));
    }
    memcpy(nonascii, sample, 15u); nonascii[0] = 0xB1u;
    CHECK(QrTaskCode_Parse(nonascii, 15u, &c) != QR_CODE_OK);
    nonascii[0] = 0u;
    CHECK(QrTaskCode_Parse(nonascii, 15u, &c) != QR_CODE_OK);
    CHECK(QrTaskCode_Parse(0, 15u, &c) == QR_CODE_LENGTH);
    CHECK(QrTaskCode_Parse((const uint8_t *)other, 15u, &c) == QR_CODE_OK);
    /* Exhaust all distinct color triples and all permutations of each batch. */
    {
        unsigned a,b,d,p,q,r;
        char t[16];
        for(a=1;a<=6;++a) for(b=1;b<=6;++b) for(d=1;d<=6;++d) {
            if(a==b || a==d || b==d) continue;
            for(p=1;p<=3;++p) for(q=1;q<=3;++q) for(r=1;r<=3;++r) {
                if(p==q || p==r || q==r) continue;
                snprintf(t,sizeof(t),"%u%u%u+%u%u%u+%u%u%u+321",a,b,d,p,q,r,d,a,b);
                CHECK(QrTaskCode_Parse((const uint8_t *)t,15u,&c)==QR_CODE_OK);
            }
        }
    }
}
static void test_stream_sessions(void)
{
    QrService s;
    QrTaskCode event;
    size_t split;
    unsigned i;
    uint8_t ack[] = {2,0,0,1,0,0x33,0x31,'\r'};
    const char *endings[] = {"\r","\n","\r\n"};
    for(i=0;i<3;++i) for(split=0;split<=15u;++split) {
        QrService_Init(&s,1u,0u);
        QrService_Receive(&s,(const uint8_t *)sample,split,10u);
        QrService_Receive(&s,(const uint8_t *)sample+split,15u-split,30u);
        CHECK(!s.data.valid); /* 15 bytes and IDLE alone do not accept a prefix. */
        receive(&s,endings[i],40u);
        CHECK(s.data.valid && s.data.records==1u && s.data.accepted_seq==1u);
        CHECK(s.data.accepted_ms==40u && QrService_TakeAccepted(&s,&event));
        CHECK(!QrService_TakeAccepted(&s,0) && !strcmp(event.text,sample));
    }
    QrService_Init(&s,1u,0u);
    receive(&s,"156+123+516+231\r\n156+123+516+231\r123+312+321+123\n",1u);
    CHECK(s.data.records==3u && s.data.duplicates==1u && s.data.ignored==1u);
    CHECK(s.data.accepted_seq==1u && !strcmp(s.data.current.text,sample));
    receive(&s,"junk\r",2u); CHECK(s.data.valid && s.data.invalid_records==1u);
    CHECK(s.data.last_error==QR_ERROR_INVALID && !strcmp(s.data.history.text,sample));
    receive(&s,"old partial",3u);
    QrService_BeginSession(&s,4u);
    CHECK(!s.data.valid && !QrService_TakeAccepted(&s,0) && s.data.history_valid);
    receive(&s,"156+123+516+231\r",5u); CHECK(!s.data.valid); /* no new boundary yet */
    receive(&s,"156+123+516+231\r",6u);
    CHECK(s.data.valid && s.data.session==2u && s.data.accepted_seq==2u);
    QrService_BeginSession(&s,7u); QrService_Process(&s,257u);
    receive(&s,"156+123+516+231\r",258u); CHECK(s.data.valid && s.data.accepted_seq==3u);
    QrService_BeginSession(&s,300u); QrService_Process(&s,10300u);
    CHECK(!strcmp(QrService_StateName(&s),"WAIT TIMEOUT"));
    receive(&s,"INVALID\r",10301u); CHECK(!s.data.valid);
    receive(&s,"156+123+516+231\n",10302u); CHECK(s.data.valid);
    QrService_Init(&s,1u,UINT32_MAX-5000u); QrService_Process(&s,4999u);
    CHECK(s.data.timed_out); receive(&s,"156+123+516+231\r",5000u); CHECK(s.data.valid);
    QrService_Init(&s,1u,0u);
    receive(&s,"156+123",10u); QrService_Process(&s,260u);
    CHECK(s.data.frame_timeouts==1u && !s.data.valid);
    receive(&s,"+516+231\r156+123+516+231\r",300u); CHECK(s.data.valid && s.data.records==1u);
    QrService_Init(&s,1u,0u);
    for(i=0;i<100u;++i) receive(&s,"x",i);
    receive(&s,"156+123+516+231\r",101u);
    CHECK(!s.data.valid && s.data.overflows==1u);
    receive(&s,"156+123+516+231\r",102u); CHECK(s.data.valid);
    QrService_Init(&s,1u,0u);
    receive(&s,"noise156+123+516+231\n156+123+516+231junk\n",1u);
    QrService_Receive(&s,ack,sizeof(ack),2u); CHECK(!s.data.valid && s.data.invalid_records==3u);
    receive(&s,"156+123+516+231MORE\r",3u); CHECK(!s.data.valid);
    receive(&s,"156+123+516+231",10u);
    QrService_TransportError(&s,QR_ERROR_UART,2u);
    receive(&s,"\r156+123+516+231\r",11u); CHECK(s.data.valid && s.data.uart_errors==2u);
    QrService_TransportError(&s,QR_ERROR_OVERFLOW,1u);
    CHECK(s.data.valid && !strcmp(s.data.current.text,sample));
    QrService_Init(&s,0u,0u); receive(&s,"156+123+516+231\r",1u);
    CHECK(!s.data.valid && !strcmp(QrService_StateName(&s),"UART ERROR"));
}

/* Real BSP and callback dispatcher run against a deterministic HAL/DMA substitute. */
USART_TypeDef fake_usart1, fake_usart3, fake_uart5;
DMA_Stream_TypeDef fake_stream1;
GPIO_TypeDef fake_gpiob;
static UART_HandleTypeDef *uart;
static uint8_t *dma_memory;
static uint16_t dma_size;
static uint32_t tick, irq_mask;
static int init_fail, dma_init_fail, receive_fail, dma_start_fail;
static unsigned dma_event, motor_rx_events, motor_errors, motor_tx_events, motor_pumps, aborts;
static unsigned gpio_checked, priority_checked, trig_gpio_inits, trig_lows, trig_highs;
static uint32_t trig_level = GPIO_PIN_SET;
uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask=1u; }
void __set_PRIMASK(uint32_t mask) { irq_mask=mask; }
uint32_t HAL_GetTick(void) { return tick; }
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *u)
{ uart=u; BspQrUart_MspInit(u); return init_fail ? HAL_ERROR : HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *d)
{
    CHECK(d->Instance==DMA1_Stream1 && d->Init.Channel==DMA_CHANNEL_4 && d->Init.Mode==DMA_CIRCULAR);
    d->State=HAL_DMA_STATE_READY; return dma_init_fail ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Receive_DMA(UART_HandleTypeDef *u, uint8_t *b, uint16_t n)
{
    if(receive_fail) return HAL_ERROR;
    dma_memory=b; dma_size=n; u->hdmarx->Instance->count=n;
    if(!dma_start_fail) u->hdmarx->State=HAL_DMA_STATE_BUSY;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_UART_Abort(UART_HandleTypeDef *u)
{ ++aborts; if(u->hdmarx) u->hdmarx->State=HAL_DMA_STATE_READY; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_Abort(DMA_HandleTypeDef *d) { d->State=HAL_DMA_STATE_READY; return HAL_OK; }
HAL_StatusTypeDef HAL_DMA_DeInit(DMA_HandleTypeDef *d) { d->State=HAL_DMA_STATE_READY; return HAL_OK; }
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *g)
{
    if (g->Pin == (GPIO_PIN_10 | GPIO_PIN_11)) {
        CHECK(port==GPIOB && g->Mode==GPIO_MODE_AF_PP && g->Alternate==GPIO_AF7_USART3);
        ++gpio_checked;
    } else {
        CHECK(port==GPIOB && g->Pin==QR_TRIG_PIN && g->Mode==GPIO_MODE_OUTPUT_PP);
        ++trig_gpio_inits;
    }
}
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pin, uint32_t state)
{
    /* Called every trigger phase; validate without incrementing the check count. */
    if (port != GPIOB || pin != QR_TRIG_PIN) { fprintf(stderr,"FAIL: trigger GPIO write\n"); exit(1); }
    if (state == GPIO_PIN_RESET) ++trig_lows; else ++trig_highs;
    trig_level = state;
}
void HAL_NVIC_SetPriority(unsigned irq, unsigned preempt, unsigned sub)
{ CHECK((irq==USART3_IRQn || irq==DMA1_Stream1_IRQn) && preempt==2u && sub==0u); ++priority_checked; }
void HAL_NVIC_EnableIRQ(unsigned irq) { (void)irq; }
void HAL_NVIC_DisableIRQ(unsigned irq) { (void)irq; }
void HAL_NVIC_ClearPendingIRQ(unsigned irq) { (void)irq; }
void HAL_UART_IRQHandler(UART_HandleTypeDef *u)
{ if(u->Instance->SR & 15u) { HAL_UART_ErrorCallback(u); u->Instance->SR &= ~15u; } }
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *d)
{
    if(dma_event==1u) HAL_UART_RxHalfCpltCallback((UART_HandleTypeDef *)d->Parent);
    else HAL_UART_RxCpltCallback((UART_HandleTypeDef *)d->Parent);
}
void BspMotorUart_RxEvent(void) { ++motor_rx_events; }
void BspMotorUart_Error(void) { ++motor_errors; }
void BspMotorUart_TxComplete(void) { ++motor_tx_events; }
void BspMotorUart_Pump(MotorService *s, uint32_t now) { (void)s; (void)now; ++motor_pumps; }
extern void USART3_IRQHandler(void);
extern void DMA1_Stream1_IRQHandler(void);
static void inject(const char *b, uint32_t at, int idle)
{
    size_t i;
    tick=at;
    for(i=0;i<strlen(b);++i) {
        dma_memory[dma_size-fake_stream1.count]=(uint8_t)b[i];
        --fake_stream1.count;
        if(fake_stream1.count==dma_size/2u) { dma_event=1u; DMA1_Stream1_IRQHandler(); }
        if(!fake_stream1.count) { fake_stream1.count=dma_size; dma_event=2u; DMA1_Stream1_IRQHandler(); }
    }
    if(idle) { fake_usart3.SR |= UART_FLAG_IDLE; USART3_IRQHandler(); }
}

/* Pixel/text LCD substitute: validates bounds and saves actual font renderings. */
static uint8_t pixels[272][480];
static sFONT *font;
static uint32_t foreground, background;
static unsigned draws, clears, bad_layout;
static char lines[9][64];
static const unsigned ys[9]={8,32,60,92,114,136,158,180,202};
void LCD_SetFont(sFONT *f) { font=f; }
void LCD_SetBackColor(uint32_t c) { background=c; }
void LCD_SetTextColor(uint32_t c) { foreground=c; }
void LCD_Clear(uint32_t c) { ++clears; memset(pixels,c?255:0,sizeof(pixels)); memset(lines,0,sizeof(lines)); }
void LCD_FillRect(uint16_t x,uint16_t y,uint16_t w,uint16_t h)
{
    unsigned i,j;
    if((unsigned)x+w>480u || (unsigned)y+h>272u) { ++bad_layout; return; }
    for(i=y;i<(unsigned)y+h;++i) for(j=x;j<(unsigned)x+w;++j) pixels[i][j]=foreground?255u:0u;
}
void LCD_DisplayStringAt(uint16_t x,uint16_t y,uint8_t *text,unsigned mode)
{
    unsigned row,i,j,k,byte_width,offset;
    uint32_t bits;
    (void)mode; ++draws;
    if(!font || (unsigned)x+strlen((char *)text)*font->Width>480u || (unsigned)y+font->Height>272u) {
        ++bad_layout; return;
    }
    for(row=0;row<9u;++row) if(y==ys[row]) strcpy(lines[row],(char *)text);
    byte_width=(font->Width+7u)/8u;
    for(k=0;text[k];++k) for(i=0;i<font->Height;++i) {
        /* bsp_lcd.c DrawChar first paints 3 background rows, then glyph rows 0..H-4. */
        offset=((unsigned)text[k]-32u)*font->Height*byte_width+(i<3u?0u:i-3u)*byte_width;
        bits=0u;
        if(i>=3u) for(j=0;j<byte_width;++j) bits=(bits<<8)|font->table[offset+j];
        for(j=0;j<font->Width;++j) pixels[y+i][x+k*font->Width+j]=
            (bits & (1u << (byte_width*8u-1u-j))) ? (foreground?255u:0u) : (background?255u:0u);
    }
}
static void save_image(const char *name)
{
    FILE *f=fopen(name,"wb"); CHECK(f!=0);
    fprintf(f,"P5\n480 272\n255\n"); fwrite(pixels,1,sizeof(pixels),f); fclose(f);
}
static void test_bsp(void)
{
    QrService s;
    UART_HandleTypeDef m={0},debug={0};
    unsigned i;
    uint32_t seq;
    CHECK(BspQrUart_Init());
    CHECK(uart->Init.BaudRate==QR_UART_BAUDRATE && gpio_checked==1u && priority_checked==2u);
    /* MCU trigger pin: init high, then 800 ms low + 200 ms high every 1000 ms. */
    BspQrTrigger_Init();
    CHECK(trig_gpio_inits==1u && trig_level==GPIO_PIN_SET);
    BspQrTrigger_Poll(1000u); CHECK(trig_level==GPIO_PIN_RESET && trig_lows==1u);
    BspQrTrigger_Poll(1799u); CHECK(trig_level==GPIO_PIN_RESET);
    BspQrTrigger_Poll(1800u); CHECK(trig_level==GPIO_PIN_SET);
    BspQrTrigger_Poll(1999u); CHECK(trig_level==GPIO_PIN_SET);
    BspQrTrigger_Poll(2000u); CHECK(trig_level==GPIO_PIN_RESET && trig_lows==2u);
    m.Instance=UART5; debug.Instance=USART1;
    HAL_UART_RxHalfCpltCallback(&m); HAL_UART_RxCpltCallback(&m);
    HAL_UART_ErrorCallback(&m); HAL_UART_TxCpltCallback(&m);
    HAL_UART_RxHalfCpltCallback(&debug); HAL_UART_RxCpltCallback(&debug);
    HAL_UART_ErrorCallback(&debug); HAL_UART_TxCpltCallback(uart);
    CHECK(motor_rx_events==2u && motor_errors==1u && motor_tx_events==1u);
    QrService_Init(&s,1u,0u);
    inject("156+123",10u,1); BspQrUart_Pump(&s,10u); CHECK(!s.data.valid);
    inject("+516+231\r\n",20u,1); BspQrUart_Pump(&s,20u); CHECK(s.data.valid && s.data.records==1u);
    /* Cross half/full DMA callbacks repeatedly without IDLE. */
    for(i=0;i<100u;++i) inject("156+123+516+231\r\n",30u,0);
    inject("",31u,1); BspQrUart_Pump(&s,31u);
    CHECK(s.data.overflows==1u && s.data.valid); /* poisoned SW queue never publishes */
    BspQrUart_BeginSession(&s,40u);
    inject("\r156+123+516+231\r",41u,1); BspQrUart_Pump(&s,41u); CHECK(s.data.valid);
    seq=s.data.accepted_seq;
    BspQrUart_BeginSession(&s,50u);
    inject("156+123+516+231",400u,1); BspQrUart_Pump(&s,400u); CHECK(!s.data.valid);
    fake_usart3.SR=UART_FLAG_ORE|UART_FLAG_IDLE;
    tick=401u; USART3_IRQHandler(); BspQrUart_Pump(&s,401u);
    CHECK(s.data.uart_errors==1u && !s.data.valid && aborts==1u);
    inject("516+231\r156+123+516+231\r",402u,1); BspQrUart_Pump(&s,402u);
    CHECK(s.data.valid && s.data.accepted_seq==seq+1u);
    /* Discard an already complete result and a partial DMA record on rescan. */
    inject("123+312+321+123\r156+123",500u,1);
    BspQrUart_BeginSession(&s,501u); BspQrUart_Pump(&s,501u);
    CHECK(!s.data.valid && !QrService_TakeAccepted(&s,0));
    inject("+516+231\r156+123+516+231\r",502u,1); BspQrUart_Pump(&s,502u); CHECK(s.data.valid);
    /* Arrival times survive main-loop delay: a pre-gap partial cannot join a later tail. */
    BspQrUart_BeginSession(&s,600u); inject("\r156+123",601u,1);
    inject("+516+231\r",1000u,1); BspQrUart_Pump(&s,1001u);
    CHECK(!s.data.valid && s.data.frame_timeouts==1u);
    for(i=0;i<40u;++i) {
        inject("156+123+516+231\r\n",1100u+i,1); BspQrUart_Pump(&s,1100u+i);
    }
    CHECK(s.data.valid && s.data.records>=40u && s.data.overflows==1u);
    seq=s.data.accepted_seq;
    fake_usart3.SR=UART_FLAG_FE|UART_FLAG_IDLE; USART3_IRQHandler();
    receive_fail=1; BspQrUart_Pump(&s,1200u);
    CHECK(!s.data.uart_ready && s.data.valid && s.data.accepted_seq==seq);
    receive_fail=0; BspQrUart_Pump(&s,1300u);
    CHECK(s.data.uart_ready && s.data.valid);
    /* Long uptime and a retry deadline spanning SysTick wrap. */
    tick=UINT32_MAX-50u; fake_usart3.SR=UART_FLAG_NE; USART3_IRQHandler();
    receive_fail=1; BspQrUart_Pump(&s,tick); CHECK(!s.data.uart_ready);
    i=aborts;
    tick=25u; BspQrUart_Pump(&s,tick); CHECK(aborts==i);
    receive_fail=0; tick=50u; BspQrUart_Pump(&s,tick);
    CHECK(s.data.uart_ready && s.data.valid && aborts==i+1u);
}

static unsigned motor_sends, motion_sends;
static int motor_send(void *ctx,const uint8_t *b,uint8_t n)
{
    (void)ctx; (void)n; ++motor_sends;
    if(b[1]!=EMM_POSITION && b[1]!=EMM_SPEED && b[1]!=EMM_STATUS) ++motion_sends;
    return 1;
}
static int motor_busy(void *ctx) { (void)ctx; return 0; }
static int32_t touch_x,touch_y;
static uint8_t touch_down;
static unsigned touch_polls;
void GTP_TouchProcess(void) { ++touch_polls; }
static void app_step(QrApp *a,uint32_t now)
{ tick=now; AppTouch_Report(0,touch_x,touch_y,touch_down); QrApp_Process(a,now); }
static void click(QrApp *a,int32_t x,int32_t y,uint32_t now)
{ touch_x=x; touch_y=y; touch_down=1u; app_step(a,now); touch_down=0u; app_step(a,now+1u); }
static void test_app_ui(void)
{
    QrApp a;
    MotorTransport t={0,motor_send,motor_busy};
    uint32_t session,seq,now;
    unsigned previous_draws,previous_clears;
    QrApp_Init(&a,t,1u,2000u); app_step(&a,2000u);
    CHECK(!a.ui.motor_page && a.qr.data.session==1u && !strcmp(lines[2],"--"));
    CHECK(strstr(lines[1],"WAIT CODE")!=0); save_image("qr_wait.pgm");
    previous_draws=draws; app_step(&a,2050u); app_step(&a,2100u);
    CHECK(draws==previous_draws); /* unchanged rows do not redraw */
    app_step(&a,12000u); CHECK(strstr(lines[1],"WAIT TIMEOUT")!=0);
    inject("156+123+516+231\r\n",12001u,1); app_step(&a,12001u); app_step(&a,12101u);
    CHECK(a.qr.data.valid && !strcmp(lines[2],sample)); save_image("qr_locked.pgm");
    session=a.qr.data.session; seq=a.qr.data.accepted_seq;
    click(&a,300,245,12200u); CHECK(a.ui.motor_page && a.qr.data.session==session);
    CHECK(strstr(lines[0],"READ ONLY")!=0); save_image("qr_motor.pgm");
    inject("123+312+321+123\r",12202u,1); app_step(&a,12202u);
    CHECK(a.qr.data.ignored==1u && a.qr.data.accepted_seq==seq);
    click(&a,300,245,12300u); CHECK(!a.ui.motor_page && !strcmp(lines[2],sample));
    touch_x=20; touch_y=245; touch_down=1u;
    app_step(&a,12400u); CHECK(!a.qr.data.valid && !strcmp(lines[2],"--"));
    for(now=12401u;now<13000u;now+=20u) app_step(&a,now);
    CHECK(a.qr.data.session==session+1u); /* a long press only rescans once */
    AppTouch_Report(1,300,245,1u);
    touch_down=0u; app_step(&a,13000u);
    CHECK(a.qr.data.session==session+1u && a.ui.was_down); /* another finger holds the same gesture */
    AppTouch_Report(1,300,245,0u); app_step(&a,13000u);
    inject("156+123+516+231\r",13001u,1); app_step(&a,13001u); CHECK(a.qr.data.accepted_seq==seq+1u);
    inject("bad\r",13002u,1); app_step(&a,13102u);
    CHECK(a.qr.data.valid && strstr(lines[4],"INVALID CODE") && !strcmp(lines[2],sample));
    save_image("qr_invalid_locked.pgm");
    previous_clears=clears;
    /* Press outside, drag into RESCAN, and keep held: never creates a new click. */
    touch_x=0; touch_y=0; touch_down=1u; app_step(&a,13200u);
    touch_x=20; touch_y=245; app_step(&a,13201u);
    CHECK(a.qr.data.session==session+1u && clears==previous_clears);
    touch_down=0; app_step(&a,13202u);
    for(now=14000u;now<20000u;++now) app_step(&a,now);
    CHECK(motor_pumps>1000u && motor_sends>3u && motion_sends==0u && touch_polls>10u);
    a.qr.data.session=a.qr.data.raw_bytes=a.qr.data.records=a.qr.data.invalid_records=UINT32_MAX;
    a.qr.data.overflows=a.qr.data.uart_errors=a.qr.data.duplicates=a.qr.data.ignored=UINT32_MAX;
    a.qr.data.accepted_seq=a.qr.data.accepted_ms=a.qr.data.frame_timeouts=UINT32_MAX;
    a.ui.redraw=1u; app_step(&a,20000u);
    CHECK(bad_layout==0u); /* include maximum 32-bit counters and both font sizes */
    QrApp_Init(&a,t,0u,21000u); QrUi_Process(&a.ui,&a.qr,&a.motor.data,21000u);
    CHECK(strstr(lines[1],"UART ERROR")!=0 && !strcmp(lines[2],"--"));
    save_image("qr_uart_error.pgm");
    QrApp_Init(&a,t,1u,22000u); CHECK(!a.qr.data.valid && a.qr.data.session==1u);
    click(&a,300,245,22001u); CHECK(a.ui.motor_page);
    inject("156+123+516+231\r",22003u,1); app_step(&a,22003u);
    CHECK(a.qr.data.valid && a.qr.data.session==1u);
    click(&a,300,245,22100u); CHECK(!a.ui.motor_page && !strcmp(lines[2],sample));
    CHECK(trig_lows >= 5u); /* QrApp_Process keeps the trigger waveform running */
}
int main(int argc,char **argv)
{
    if(argc>1) {
        QrService s;
        if(!strcmp(argv[1],"init")) init_fail=1;
        if(!strcmp(argv[1],"dma")) dma_init_fail=1;
        if(!strcmp(argv[1],"receive")) receive_fail=1;
        if(!strcmp(argv[1],"start")) dma_start_fail=1;
        CHECK(!BspQrUart_Init()); QrService_Init(&s,0u,0u); BspQrUart_Pump(&s,0u);
        CHECK(!s.data.valid && !s.data.uart_ready && !strcmp(QrService_StateName(&s),"UART ERROR"));
        printf("PASS: QR startup failure (%s)\n",argv[1]); return 0;
    }
    test_validation(); test_stream_sessions(); test_bsp(); test_app_ui();
    printf("PASS: %u QR checks (parser, sessions, real BSP/callbacks, app, UI)\n",checks);
    return 0;
}
