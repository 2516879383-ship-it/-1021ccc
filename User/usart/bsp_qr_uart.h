#ifndef BSP_QR_UART_H
#define BSP_QR_UART_H
#include "stm32f4xx_hal.h"
#include "./qr/qr_service.h"
uint8_t BspQrUart_Init(void);
void BspQrUart_MspInit(UART_HandleTypeDef *uart);
void BspQrUart_RxEvent(void);
void BspQrUart_Error(void);
void BspQrUart_Pump(QrService *service, uint32_t now_ms);
void BspQrUart_BeginSession(QrService *service, uint32_t now_ms);
#endif
