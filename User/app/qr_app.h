#ifndef QR_APP_H
#define QR_APP_H
#include "qr_ui.h"
#include "./motor/motor_bus.h"
typedef struct {
    MotorBus motor; QrService qr; QrUi ui;
    uint8_t boot_phase, boot_index, boot_waiting, boot_started, boot_stopping;
    uint32_t boot_due, boot_deadline;
} QrApp;
void QrApp_Init(QrApp *app, MotorTransport motor, uint8_t qr_ready, uint32_t now_ms);
void QrApp_Process(QrApp *app, uint32_t now_ms);
#endif
