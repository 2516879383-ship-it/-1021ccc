#include "bsp_motor_uart.h"
#include <string.h>

#define DMA_RX_SIZE 256u
#define RX_QUEUE_SIZE 512u
static UART_HandleTypeDef motor_uart;
static DMA_HandleTypeDef rx_dma, tx_dma;
/* Default linker places these in 0x20000000 SRAM, not CCM/SDRAM. */
static uint8_t dma_rx[DMA_RX_SIZE], rx_queue[RX_QUEUE_SIZE], tx_bytes[16];
static volatile uint16_t rx_write, rx_read;
static uint16_t dma_read;
static volatile uint8_t restart_rx;
static uint8_t uart_ready;
static volatile uint8_t tx_in_flight;
static uint32_t tx_started_ms;
static volatile uint32_t uart_errors, overflows;

static void drain_dma(void)
{
    uint16_t head = (uint16_t)(DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(&rx_dma));
    uint16_t next;
    if (head == DMA_RX_SIZE) head = 0u;
    while (dma_read != head) {
        next = (uint16_t)((rx_write + 1u) & (RX_QUEUE_SIZE - 1u));
        if (next == rx_read) ++overflows;
        else {
            rx_queue[rx_write] = dma_rx[dma_read];
            __DMB(); rx_write = next;
        }
        dma_read = (uint16_t)((dma_read + 1u) & (DMA_RX_SIZE - 1u));
    }
}

void BspMotorUart_MspInit(UART_HandleTypeDef *uart)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_UART5_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE(); __HAL_RCC_DMA1_CLK_ENABLE();
    gpio.Mode = GPIO_MODE_AF_PP; gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH; gpio.Alternate = GPIO_AF8_UART5;
    gpio.Pin = GPIO_PIN_12; HAL_GPIO_Init(GPIOC, &gpio);
    gpio.Pin = GPIO_PIN_2; HAL_GPIO_Init(GPIOD, &gpio);
    rx_dma.Instance = DMA1_Stream0; rx_dma.Init.Channel = DMA_CHANNEL_4;
    rx_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    rx_dma.Init.PeriphInc = DMA_PINC_DISABLE; rx_dma.Init.MemInc = DMA_MINC_ENABLE;
    rx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    rx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    rx_dma.Init.Mode = DMA_CIRCULAR; rx_dma.Init.Priority = DMA_PRIORITY_HIGH;
    rx_dma.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    if (HAL_DMA_Init(&rx_dma) != HAL_OK) { ++uart_errors; return; }
    __HAL_LINKDMA(uart, hdmarx, rx_dma);
    tx_dma.Instance = DMA1_Stream7; tx_dma.Init = rx_dma.Init;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH; tx_dma.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&tx_dma) != HAL_OK) { ++uart_errors; return; }
    __HAL_LINKDMA(uart, hdmatx, tx_dma);
    /* Equal preemption priority serializes the UART/DMA producer. */
    HAL_NVIC_SetPriority(UART5_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(UART5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

static int busy(void *context)
{
    (void)context;
    return !uart_ready || motor_uart.gState != HAL_UART_STATE_READY;
}

static int send(void *context, const uint8_t *data, uint8_t length)
{
    (void)context;
    if (busy(0) || length > sizeof(tx_bytes)) return 0;
    memcpy(tx_bytes, data, length);
    tx_started_ms = HAL_GetTick();
    tx_in_flight = 1u;
    if (HAL_UART_Transmit_DMA(&motor_uart, tx_bytes, length) == HAL_OK) return 1;
    tx_in_flight = 0u; ++uart_errors; restart_rx = 1u; return 0;
}

MotorTransport BspMotorUart_Init(void)
{
    MotorTransport transport;
    motor_uart.Instance = UART5;
    motor_uart.Init.BaudRate = 115200u;
    motor_uart.Init.WordLength = UART_WORDLENGTH_8B;
    motor_uart.Init.StopBits = UART_STOPBITS_1;
    motor_uart.Init.Parity = UART_PARITY_NONE;
    motor_uart.Init.Mode = UART_MODE_TX_RX;
    motor_uart.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    motor_uart.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&motor_uart) != HAL_OK) ++uart_errors;
    uart_ready = (uint8_t)(!uart_errors && motor_uart.hdmarx && motor_uart.hdmatx);
    if (uart_ready && HAL_UART_Receive_DMA(&motor_uart, dma_rx, DMA_RX_SIZE) != HAL_OK) {
        ++uart_errors; restart_rx = 1u;
    }
    __HAL_UART_CLEAR_IDLEFLAG(&motor_uart);
    __HAL_UART_ENABLE_IT(&motor_uart, UART_IT_IDLE);
    transport.context = 0; transport.send = send; transport.busy = busy;
    return transport;
}

void BspMotorUart_Pump(MotorService *service, uint32_t now_ms)
{
    static uint32_t seen_errors, seen_overflows;
    uint8_t bytes[64];
    uint32_t count, mask, errors, dropped;
    if (tx_in_flight && (uint32_t)(now_ms - tx_started_ms) >= 20u) {
        ++uart_errors; restart_rx = 1u;
    }
    if (restart_rx && uart_ready) {
        /* HAL abort may use its millisecond timeout: leave SysTick enabled. */
        HAL_NVIC_DisableIRQ(UART5_IRQn);
        HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
        HAL_NVIC_DisableIRQ(DMA1_Stream7_IRQn);
        restart_rx = 0u;
        HAL_UART_AbortTransmit(&motor_uart); HAL_UART_AbortReceive(&motor_uart);
        tx_in_flight = 0u; dma_read = 0u; rx_read = rx_write;
        __HAL_UART_CLEAR_OREFLAG(&motor_uart);
        if (HAL_UART_Receive_DMA(&motor_uart, dma_rx, DMA_RX_SIZE) != HAL_OK) restart_rx = 1u;
        __HAL_UART_ENABLE_IT(&motor_uart, UART_IT_IDLE);
        HAL_NVIC_ClearPendingIRQ(UART5_IRQn);
        HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
        HAL_NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
        HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
        HAL_NVIC_EnableIRQ(UART5_IRQn);
    }
    errors = uart_errors; dropped = overflows;
    if (seen_errors != errors || seen_overflows != dropped) {
        MotorService_TransportError(service,
            seen_overflows != dropped ? MOTOR_ERROR_RX_OVERFLOW : MOTOR_ERROR_UART, now_ms);
        mask = __get_PRIMASK(); __disable_irq(); rx_read = rx_write; __set_PRIMASK(mask);
        seen_errors = errors; seen_overflows = dropped;
    }
    do {
        count = 0u;
        while (rx_read != rx_write && count < sizeof(bytes)) {
            __DMB(); bytes[count++] = rx_queue[rx_read];
            rx_read = (uint16_t)((rx_read + 1u) & (RX_QUEUE_SIZE - 1u));
        }
        if (count) MotorService_Receive(service, bytes, count, now_ms);
    } while (count);
}

void UART5_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&motor_uart, UART_FLAG_IDLE) &&
        __HAL_UART_GET_IT_SOURCE(&motor_uart, UART_IT_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&motor_uart); drain_dma();
    }
    HAL_UART_IRQHandler(&motor_uart);
}
void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&rx_dma); }
void DMA1_Stream7_IRQHandler(void) { HAL_DMA_IRQHandler(&tx_dma); }
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *uart)
{ if (uart->Instance == UART5) drain_dma(); }
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{ if (uart->Instance == UART5) drain_dma(); }
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{ if (uart->Instance == UART5) { ++uart_errors; restart_rx = 1u; } }
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{ if (uart->Instance == UART5) tx_in_flight = 0u; }
