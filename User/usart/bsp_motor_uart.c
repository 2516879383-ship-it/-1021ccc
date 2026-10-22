#include "bsp_motor_uart.h"
#include <string.h>

#define DMA_RX_SIZE 256u
#define RX_QUEUE_SIZE 512u
#define UART_RX_ERRORS (UART_FLAG_PE | UART_FLAG_FE | UART_FLAG_NE | UART_FLAG_ORE)
#define RECOVER_MS 100u
static UART_HandleTypeDef motor_uart;
static DMA_HandleTypeDef rx_dma, tx_dma;
/* Default linker places these in 0x20000000 SRAM, not CCM/SDRAM. */
static uint8_t dma_rx[DMA_RX_SIZE], rx_queue[RX_QUEUE_SIZE], tx_bytes[EMM_COMMAND_CAPACITY];
static volatile uint16_t rx_write, rx_read;
static uint16_t dma_read;
static volatile uint8_t restart_rx;
static uint8_t uart_ready, retry_wait, msp_failed, tx_fault;
static volatile uint8_t receiving;
static uint32_t retry_started, seen_errors, seen_overflows;
static volatile uint8_t tx_in_flight;
static uint32_t tx_started_ms, tx_timeout_ms;
static volatile uint32_t uart_errors, overflows;

static void quarantine(void)
{
    receiving = 0u; restart_rx = 1u;
    __HAL_UART_DISABLE_IT(&motor_uart, UART_IT_IDLE);
    __HAL_UART_DISABLE_IT(&motor_uart, UART_IT_ERR);
    __HAL_UART_DISABLE_IT(&motor_uart, UART_IT_PE);
}
void BspMotorUart_Error(void)
{
    if (!restart_rx) ++uart_errors;
    quarantine();
}

static void drain_dma(void)
{
    uint16_t head, next;
    /* DMA half/full may run before UART5 IRQ for the same receive error. */
    if (motor_uart.Instance->SR & UART_RX_ERRORS) BspMotorUart_Error();
    if (restart_rx || !receiving) return;
    head = (uint16_t)(DMA_RX_SIZE - __HAL_DMA_GET_COUNTER(&rx_dma));
    if (head == DMA_RX_SIZE) head = 0u;
    while (dma_read != head) {
        next = (uint16_t)((rx_write + 1u) & (RX_QUEUE_SIZE - 1u));
        if (next == rx_read) { ++overflows; quarantine(); return; }
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
    if (HAL_DMA_Init(&rx_dma) != HAL_OK) { msp_failed = 1u; return; }
    __HAL_LINKDMA(uart, hdmarx, rx_dma);
    tx_dma.Instance = DMA1_Stream7; tx_dma.Init = rx_dma.Init;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH; tx_dma.Init.Mode = DMA_NORMAL;
    if (HAL_DMA_Init(&tx_dma) != HAL_OK) { msp_failed = 1u; return; }
    __HAL_LINKDMA(uart, hdmatx, tx_dma);
    /* Equal preemption priority serializes the UART/DMA producer. */
    HAL_NVIC_SetPriority(UART5_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(UART5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream7_IRQn, 1u, 0u); HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
}

static int busy(void *context)
{
    (void)context;
    return !uart_ready || !receiving || restart_rx || tx_fault || tx_in_flight ||
        motor_uart.gState != HAL_UART_STATE_READY;
}

static int send(void *context, const uint8_t *data, uint8_t length)
{
    HAL_StatusTypeDef result;
    uint32_t mask;
    (void)context;
    if (busy(0) || !data || !length || length > sizeof(tx_bytes)) return 0;
    memcpy(tx_bytes, data, length);
    /* Keep callbacks from changing DMA state during the old HAL start check. */
    mask = __get_PRIMASK(); __disable_irq();
    if (motor_uart.Instance->SR & UART_RX_ERRORS) BspMotorUart_Error();
    if (busy(0)) { __set_PRIMASK(mask); return 0; }
    if (tx_dma.State != HAL_DMA_STATE_READY || (tx_dma.Instance->CR & DMA_SxCR_EN)) {
        tx_fault = 1u; BspMotorUart_Error(); __set_PRIMASK(mask); return 0;
    }
    tx_started_ms = HAL_GetTick();
    tx_timeout_ms = 20u + ((uint32_t)length * 10000u + 115199u) / 115200u;
    tx_in_flight = 1u;
    result = HAL_UART_Transmit_DMA(&motor_uart, tx_bytes, length);
    if (result == HAL_OK && tx_dma.State == HAL_DMA_STATE_BUSY &&
        (tx_dma.Instance->CR & DMA_SxCR_EN) && (motor_uart.Instance->CR3 & USART_CR3_DMAT)) {
        __set_PRIMASK(mask); return 1;
    }
    __set_PRIMASK(mask);
    tx_in_flight = 0u;
    if (result != HAL_BUSY) { tx_fault = 1u; BspMotorUart_Error(); }
    return 0;
}

static uint8_t start_rx(void)
{
    HAL_StatusTypeDef result;
    dma_read = 0u; rx_read = rx_write = 0u;
    /* Never accept an old BUSY stream as evidence of this start succeeding. */
    if (rx_dma.State != HAL_DMA_STATE_READY || (rx_dma.Instance->CR & DMA_SxCR_EN)) return 0u;
    __HAL_UART_CLEAR_OREFLAG(&motor_uart);
    result = HAL_UART_Receive_DMA(&motor_uart, dma_rx, DMA_RX_SIZE);
    if (result != HAL_OK || rx_dma.State != HAL_DMA_STATE_BUSY ||
        !(rx_dma.Instance->CR & DMA_SxCR_EN) || !(motor_uart.Instance->CR3 & USART_CR3_DMAR) ||
        motor_uart.RxState != HAL_UART_STATE_BUSY_RX || motor_uart.ErrorCode != HAL_UART_ERROR_NONE) return 0u;
    __HAL_UART_ENABLE_IT(&motor_uart, UART_IT_IDLE);
    return 1u;
}
static void configure(void)
{
    msp_failed = 0u;
    motor_uart.gState = HAL_UART_STATE_RESET; /* rerun MSP after partial init failure */
    uart_ready = (uint8_t)(HAL_UART_Init(&motor_uart) == HAL_OK && !msp_failed &&
        motor_uart.hdmarx && motor_uart.hdmatx);
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
    configure();
    if (uart_ready) receiving = start_rx();
    if (!receiving) BspMotorUart_Error();
    transport.context = 0; transport.send = send; transport.busy = busy;
    return transport;
}

static void recover(uint32_t now)
{
    uint8_t ok = 1u;
    if (retry_wait && (uint32_t)(now - retry_started) < RECOVER_MS) return;
    receiving = 0u;
    /* RX-only faults leave an in-flight STOP/control DMA buffer untouched.
       SysTick remains enabled during HAL's bounded abort waits. */
    HAL_NVIC_DisableIRQ(UART5_IRQn); HAL_NVIC_DisableIRQ(DMA1_Stream0_IRQn);
    if (!uart_ready) configure();
    if (uart_ready) {
        if (tx_fault) {
            HAL_NVIC_DisableIRQ(DMA1_Stream7_IRQn);
            ok = (uint8_t)(HAL_UART_AbortTransmit(&motor_uart) == HAL_OK &&
                HAL_DMA_DeInit(&tx_dma) == HAL_OK && HAL_DMA_Init(&tx_dma) == HAL_OK);
            if (ok) { tx_fault = 0u; tx_in_flight = 0u; }
            HAL_NVIC_ClearPendingIRQ(DMA1_Stream7_IRQn); HAL_NVIC_EnableIRQ(DMA1_Stream7_IRQn);
        }
        if (HAL_UART_AbortReceive(&motor_uart) != HAL_OK || HAL_DMA_DeInit(&rx_dma) != HAL_OK ||
            HAL_DMA_Init(&rx_dma) != HAL_OK) ok = 0u;
        if (ok) receiving = start_rx();
    }
    restart_rx = (uint8_t)!receiving;
    retry_wait = restart_rx; retry_started = now;
    if (restart_rx) { ++uart_errors; quarantine(); }
    HAL_NVIC_ClearPendingIRQ(UART5_IRQn); HAL_NVIC_ClearPendingIRQ(DMA1_Stream0_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn); HAL_NVIC_EnableIRQ(UART5_IRQn);
}
static void pump(MotorService *service, MotorBus *bus, uint32_t now_ms)
{
    uint8_t bytes[64];
    uint32_t count, mask, errors, dropped, budget = 128u;
    if (tx_in_flight && (uint32_t)(now_ms - tx_started_ms) >= tx_timeout_ms) {
        tx_fault = 1u; BspMotorUart_Error();
    }
    if (restart_rx) recover(now_ms);
    errors = uart_errors; dropped = overflows;
    if (seen_errors != errors || seen_overflows != dropped) {
        MotorError error = seen_overflows != dropped ? MOTOR_ERROR_RX_OVERFLOW : MOTOR_ERROR_UART;
        if (bus) MotorBus_TransportError(bus, error, now_ms);
        else MotorService_TransportError(service, error, now_ms);
        mask = __get_PRIMASK(); __disable_irq(); rx_read = rx_write; __set_PRIMASK(mask);
        seen_errors = errors; seen_overflows = dropped;
    }
    if (!receiving || restart_rx) return;
    do {
        mask = __get_PRIMASK(); __disable_irq();
        if (restart_rx || seen_errors != uart_errors || seen_overflows != overflows) {
            __set_PRIMASK(mask); break;
        }
        count = 0u;
        while (rx_read != rx_write && count < sizeof(bytes)) {
            __DMB(); bytes[count++] = rx_queue[rx_read];
            rx_read = (uint16_t)((rx_read + 1u) & (RX_QUEUE_SIZE - 1u));
        }
        __set_PRIMASK(mask);
        if (count) {
            if (bus) MotorBus_Receive(bus, bytes, count, now_ms);
            else MotorService_Receive(service, bytes, count, now_ms);
            budget -= count;
        }
    } while (count && budget);
}
void BspMotorUart_Pump(MotorService *service, uint32_t now_ms) { pump(service, 0, now_ms); }
void BspMotorUart_PumpBus(MotorBus *bus, uint32_t now_ms) { pump(0, bus, now_ms); }

void UART5_IRQHandler(void)
{
    /* SR/DR IDLE clearing also clears PE/FE/NE/ORE in this STM32 HAL. */
    if (motor_uart.Instance->SR & UART_RX_ERRORS) {
        BspMotorUart_Error(); __HAL_UART_CLEAR_OREFLAG(&motor_uart);
    }
    HAL_UART_IRQHandler(&motor_uart); /* still service TX TC on an RX fault */
    if (__HAL_UART_GET_FLAG(&motor_uart, UART_FLAG_IDLE) &&
        __HAL_UART_GET_IT_SOURCE(&motor_uart, UART_IT_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(&motor_uart); drain_dma();
    }
}
void DMA1_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&rx_dma); }
void DMA1_Stream7_IRQHandler(void) { HAL_DMA_IRQHandler(&tx_dma); }
void BspMotorUart_RxEvent(void) { drain_dma(); }
void BspMotorUart_TxComplete(void) { tx_in_flight = 0u; }
