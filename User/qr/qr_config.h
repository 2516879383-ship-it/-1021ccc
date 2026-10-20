#ifndef QR_CONFIG_H
#define QR_CONFIG_H
/* Bring-up values, NOT measurements of the attached module. */
#define QR_UART_BAUDRATE 9600u
/* HAL tokens are only expanded by bsp_qr_uart.c / bsp_qr_trigger.c, keeping
   parsers HAL independent.
   For 8 data bits + parity, STM32 HAL needs UART_WORDLENGTH_9B. */
#define QR_UART_WORD_LENGTH UART_WORDLENGTH_8B
#define QR_UART_PARITY UART_PARITY_NONE
#define QR_UART_STOP_BITS UART_STOPBITS_1
#define QR_WAIT_TIMEOUT_MS 10000u
#define QR_FRAME_TIMEOUT_MS 250u
#define QR_SESSION_GAP_MS 250u
#define QR_RECORD_CAPACITY 64u

/* MCU-driven XM1605 TRIG (pin 12, active low). PB12 is on header J7 pin 14
   and is not used by the current LCD/SDRAM/touch/USART BSPs.
   Default waveform: 80 ms low (capture window) + 20 ms high per 100 ms.
   The module must be in manual/level-trigger mode; in continuous mode TRIG
   means pause/resume, not "scan once". Tune these when doing physical tests. */
#define QR_TRIG_PORT GPIOB
#define QR_TRIG_PIN GPIO_PIN_12
#define QR_TRIG_PERIOD_MS 1000u
#define QR_TRIG_LOW_MS 800u
#endif
