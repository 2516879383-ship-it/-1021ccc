#ifndef QR_CONFIG_H
#define QR_CONFIG_H
/* Bring-up values, NOT measurements of the attached module. */
#define QR_UART_BAUDRATE 9600u
/* HAL tokens are only expanded by bsp_qr_uart.c, keeping parsers HAL independent.
   For 8 data bits + parity, STM32 HAL needs UART_WORDLENGTH_9B. */
#define QR_UART_WORD_LENGTH UART_WORDLENGTH_8B
#define QR_UART_PARITY UART_PARITY_NONE
#define QR_UART_STOP_BITS UART_STOPBITS_1
#define QR_WAIT_TIMEOUT_MS 10000u
#define QR_FRAME_TIMEOUT_MS 250u
#define QR_SESSION_GAP_MS 250u
#define QR_RECORD_CAPACITY 64u
#endif
