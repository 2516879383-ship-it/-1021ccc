#include "qr_app.h"
#include "app_runtime.h"
#include "./usart/bsp_qr_uart.h"
#include "./usart/bsp_motor_uart.h"
#include "./motor/motor_demo_config.h"

void QrApp_Init(QrApp *a, MotorTransport motor, uint8_t ready, uint32_t now)
{
    MotorService_Init(&a->motor, motor, MOTOR_DEMO_ADDRESS, now);
    QrService_Init(&a->qr, ready, now); QrUi_Init(&a->ui);
}
void QrApp_Process(QrApp *a, uint32_t now)
{
    int32_t x, y;
    uint8_t down;
    BspMotorUart_Pump(&a->motor, now);
    MotorService_Process(&a->motor, now); /* No Submit or MotorDemo_Process here. */
    AppTouch_Process(now); AppTouch_GetState(&x, &y, &down);
    now = AppClock_Now();
    if (QrUi_Touch(&a->ui, x, y, down) == QR_UI_RESCAN)
        BspQrUart_BeginSession(&a->qr, now);
    BspQrUart_Pump(&a->qr, now);
    now = AppClock_Now(); /* Capture IRQs/touch I2C can advance SysTick during a step. */
    QrService_Process(&a->qr, now);
    QrUi_Process(&a->ui, &a->qr, &a->motor.data, now);
}
