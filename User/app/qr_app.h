#ifndef QR_APP_H
#define QR_APP_H
/*
 * 应用聚合状态：一个 QrApp 长期持有所有服务、统一 UI 和四轮上电测试状态。
 * Init 只在启动时执行，Process 从主循环反复调用；服务之间通过此编排层协作。
 */
#include "qr_ui.h"
#include "./motor/motor_bus.h"
#include "./gyro/gyro_service.h"
typedef struct {
    MotorBus motor; QrService qr; QrUi ui; FlowService flow; GyroService gyro;
    uint8_t boot_phase, boot_index, boot_waiting, boot_started, boot_stopping;
    uint32_t boot_due, boot_deadline;
} QrApp;
void QrApp_Init(QrApp *app, MotorTransport motor, uint8_t qr_ready, uint32_t now_ms);
void QrApp_Process(QrApp *app, uint32_t now_ms);
#endif
