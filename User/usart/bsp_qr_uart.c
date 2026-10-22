/*
 * 扫码 USART3 适配：PB10/PB11，循环 DMA 接收→带捕获时间戳的软件队列→前台扫码服务。
 * 保留捕获时刻而非出队时刻，保证分帧超时和新会话边界不被主循环积压掩盖。
 */
#include "bsp_qr_uart.h"

#define DMA_SIZE 256u
#define QUEUE_SIZE 512u
#define UART_RX_ERRORS (UART_FLAG_PE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_ORE)
static UART_HandleTypeDef qr_uart;
static DMA_HandleTypeDef qr_dma;
/* Keil default RW/ZI region is 0x20000000 SRAM (DMA accessible), never CCM. */
static uint8_t qr_dma_bytes[DMA_SIZE], queue[QUEUE_SIZE];
static uint32_t captured_at[QUEUE_SIZE];
static volatile uint16_t write_at, read_at;
static uint16_t dma_at;
static volatile uint8_t restart_rx, queue_fault;
static uint8_t configured, receiving, retry_wait;
static volatile uint32_t uart_errors, overflow_events, raw_bytes, last_byte_ms;
static uint32_t seen_errors, seen_overflows, retry_started;

void BspQrUart_Error(void)
{
    if (!restart_rx) ++uart_errors;
    restart_rx = 1u;
}
/*
 * IRQ 中搬运新增 DMA 数据，同时记录捕获时间；UART 错误时不交付，
 * 队列溢出后标记整段损坏，待前台清理并通知解析器重新同步。
 */
void BspQrUart_RxEvent(void)
{
    uint16_t head, next;
    uint32_t at = HAL_GetTick();
    if (qr_uart.Instance->SR & UART_RX_ERRORS) BspQrUart_Error();
    if (restart_rx) return;
    head = (uint16_t)(DMA_SIZE - __HAL_DMA_GET_COUNTER(&qr_dma));
    if (head == DMA_SIZE) head = 0u;
    while (dma_at != head) {
        ++raw_bytes; last_byte_ms = at;
        next = (uint16_t)((write_at + 1u) & (QUEUE_SIZE - 1u));
        if (!queue_fault && next == read_at) { queue_fault = 1u; ++overflow_events; }
        if (!queue_fault) {
            queue[write_at] = qr_dma_bytes[dma_at]; captured_at[write_at] = at;
            __DMB(); write_at = next;
        }
        dma_at = (uint16_t)((dma_at + 1u) & (DMA_SIZE - 1u));
    }
}
/*
 * 配置 USART3 和 DMA1 Stream1 接收；二者同抢占级，电机 UART5 使用更高优先级。
 */
void BspQrUart_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_USART3_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
    gpio.Pin = GPIO_PIN_10 | GPIO_PIN_11; gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_PULLUP; gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = GPIO_AF7_USART3;
    HAL_GPIO_Init(GPIOB, &gpio);
    qr_dma.Instance = DMA1_Stream1; qr_dma.Init.Channel = DMA_CHANNEL_4;
    qr_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    qr_dma.Init.PeriphInc = DMA_PINC_DISABLE; qr_dma.Init.MemInc = DMA_MINC_ENABLE;
    qr_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE; qr_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    qr_dma.Init.Mode = DMA_CIRCULAR; qr_dma.Init.Priority = DMA_PRIORITY_MEDIUM;
    qr_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&qr_dma) != HAL_OK) { ++uart_errors; return; }
    __HAL_LINKDMA(uart, hdmarx, qr_dma);
    /* Same QR preemption level serializes its two producers; motor uses priority 1. */
    HAL_NVIC_SetPriority(USART3_IRQn, 2u, 0u); HAL_NVIC_EnableIRQ(USART3_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 2u, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);
}
/*
 * 重置接收游标并启动循环 DMA，额外检查 DMA 状态以识别旧 HAL 的假成功。
 */
static uint8_t start_rx(void)
{
    HAL_StatusTypeDef result;
    dma_at = 0u; read_at = write_at = 0u; queue_fault = 0u;
    __HAL_UART_CLEAR_OREFLAG(&qr_uart);
    result = HAL_UART_Receive_DMA(&qr_uart, qr_dma_bytes, DMA_SIZE);
    /* HAL 1.7.3 does not propagate HAL_DMA_Start_IT's return value. */
    if (result != HAL_OK || qr_dma.State != HAL_DMA_STATE_BUSY) return 0u;
    __HAL_UART_ENABLE_IT(&qr_uart, UART_IT_IDLE);
    return 1u;
}
/*
 * 按 qr_config.h 配置扫码 UART，尝试启动 DMA 并向应用报告当前接收可用性。
 */
uint8_t BspQrUart_Init(void)
{
    qr_uart.Instance = USART3; qr_uart.Init.BaudRate = QR_UART_BAUDRATE;
    qr_uart.Init.WordLength = QR_UART_WORD_LENGTH; qr_uart.Init.StopBits = QR_UART_STOP_BITS;
    qr_uart.Init.Parity = QR_UART_PARITY; qr_uart.Init.Mode = UART_MODE_TX_RX;
    qr_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE; qr_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    configured = (uint8_t)(HAL_UART_Init(&qr_uart) == HAL_OK && qr_uart.hdmarx != 0);
    if (configured) receiving = start_rx();
    if (!configured || !receiving) BspQrUart_Error();
    return receiving;
}
/*
 * 前台中止并重建本扫码通道；失败至少间隔 100 ms 重试，
 * 过程不关闭 SysTick 和电机中断，避免扫码恢复挤占其它模块的中断服务。
 */
static void recover(uint32_t now)
{
    if (!configured || (retry_wait && (uint32_t)(now - retry_started) < 100u)) return;
    retry_wait = 0u;
    receiving = 0u;
    HAL_NVIC_DisableIRQ(USART3_IRQn); HAL_NVIC_DisableIRQ(DMA1_Stream1_IRQn);
    /* SysTick and motor IRQs stay enabled during HAL's bounded abort wait. */
    HAL_UART_Abort(&qr_uart);
    restart_rx = 0u;
    /* Reinitialize this stream through HAL, including an interrupted async abort. */
    if (HAL_DMA_DeInit(&qr_dma) == HAL_OK && HAL_DMA_Init(&qr_dma) == HAL_OK)
        receiving = start_rx();
    if (!receiving) { BspQrUart_Error(); retry_wait = 1u; retry_started = now; }
    HAL_NVIC_ClearPendingIRQ(USART3_IRQn); HAL_NVIC_ClearPendingIRQ(DMA1_Stream1_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn); HAL_NVIC_EnableIRQ(USART3_IRQn);
}
/*
 * 先在临界区丢弃已经进入 DMA/队列的旧字节，再开启服务新会话。
 * 不重启正常 DMA；解析器另等行尾或静默边界，避免把旧行后缀当新码。
 */
void BspQrUart_BeginSession(QrService *s, uint32_t now)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    /* Discard all bytes already in DMA/SW queue, without restarting the live DMA. */
    BspQrUart_RxEvent(); read_at = write_at;
    __set_PRIMASK(mask);
    QrService_BeginSession(s, now);
}
/*
 * 前台按预算消费字节及捕获时间；先处理错误/溢出事件、清掉积压并损坏半帧，
 * 再交给 QrService，最后发布原始流量统计。
 */
void BspQrUart_Pump(QrService *s, uint32_t now)
{
    uint8_t b;
    uint32_t at, mask, errors, overflows, budget = QUEUE_SIZE;
    if (restart_rx) recover(now);
    QrService_SetUartReady(s, (uint8_t)(receiving && !restart_rx));
    while (budget--) {
        mask = __get_PRIMASK(); __disable_irq();
        errors = uart_errors; overflows = overflow_events;
        if (seen_errors != errors || seen_overflows != overflows) {
            read_at = write_at; queue_fault = 0u;
            __set_PRIMASK(mask);
            if (seen_errors != errors) QrService_TransportError(s, QR_ERROR_UART, errors - seen_errors);
            if (seen_overflows != overflows) QrService_TransportError(s, QR_ERROR_OVERFLOW, overflows - seen_overflows);
            seen_errors = errors; seen_overflows = overflows;
            continue;
        }
        if (read_at == write_at) { __set_PRIMASK(mask); break; }
        __DMB(); b = queue[read_at]; at = captured_at[read_at];
        read_at = (uint16_t)((read_at + 1u) & (QUEUE_SIZE - 1u));
        __set_PRIMASK(mask);
        QrService_Receive(s, &b, 1u, at);
    }
    s->data.raw_bytes = raw_bytes; s->data.last_byte_ms = last_byte_ms;
}
/*
 * 先检查接收错误，再清 IDLE；错误只登记并关闭相应 RX 中断，
 * 把 DMA 恢复留给主循环，正常路径交 HAL 后搬运 IDLE 数据。
 */
void USART3_IRQHandler(void)
{
    /* Inspect errors before clearing IDLE: the SR/DR read also clears UART errors. */
    if (qr_uart.Instance->SR & UART_RX_ERRORS) {
        BspQrUart_Error();
        /* Keep DMA abort/restart in the main loop. Avoid HAL's async UART-error
           abort racing the foreground recovery on this old HAL release. */
        __HAL_UART_DISABLE_IT(&qr_uart, UART_IT_IDLE);
        __HAL_UART_DISABLE_IT(&qr_uart, UART_IT_ERR);
        __HAL_UART_DISABLE_IT(&qr_uart, UART_IT_PE);
        __HAL_UART_CLEAR_OREFLAG(&qr_uart);
        return;
    }
    HAL_UART_IRQHandler(&qr_uart);
    if (__HAL_UART_GET_FLAG(&qr_uart, UART_FLAG_IDLE) &&
        __HAL_UART_GET_IT_SOURCE(&qr_uart, UART_IT_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&qr_uart); BspQrUart_RxEvent();
    }
}
void DMA1_Stream1_IRQHandler(void) { HAL_DMA_IRQHandler(&qr_dma); }
