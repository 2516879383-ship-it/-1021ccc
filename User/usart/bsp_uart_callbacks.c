#include "bsp_motor_uart.h"
#include "bsp_qr_uart.h"

/* Exactly one strong definition per HAL callback, with explicit instance routing. */
void HAL_UART_RxHalfCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == UART5) BspMotorUart_RxEvent();
    else if (uart->Instance == USART3) BspQrUart_RxEvent();
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == UART5) BspMotorUart_RxEvent();
    else if (uart->Instance == USART3) BspQrUart_RxEvent();
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
    if (uart->Instance == UART5) BspMotorUart_Error();
    else if (uart->Instance == USART3) BspQrUart_Error();
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{ if (uart->Instance == UART5) BspMotorUart_TxComplete(); }
