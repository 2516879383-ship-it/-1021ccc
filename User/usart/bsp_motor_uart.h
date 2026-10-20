#ifndef BSP_MOTOR_UART_H
#define BSP_MOTOR_UART_H
#include "stm32f4xx_hal.h"
#include "./motor/motor_service.h"
MotorTransport BspMotorUart_Init(void);
void BspMotorUart_MspInit(UART_HandleTypeDef *uart);
void BspMotorUart_Pump(MotorService *service, uint32_t now_ms);
void BspMotorUart_RxEvent(void);
void BspMotorUart_Error(void);
void BspMotorUart_TxComplete(void);
#endif
