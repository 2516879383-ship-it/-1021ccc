#ifndef QR_UI_H
#define QR_UI_H
#include "./qr/qr_service.h"
#include "./motor/motor_bus.h"
typedef enum { QR_UI_NONE, QR_UI_RESCAN, QR_UI_MODE, QR_UI_RUN,
               QR_UI_STOP_AXIS, QR_UI_STOP_WHEELS, QR_UI_STOP_ALL } QrUiAction;
typedef struct {
    char previous[9][43];
    uint32_t last_draw;
    uint8_t motor_page, was_down, redraw, selected, detail, test_action;
} QrUi;
void QrUi_Init(QrUi *ui);
QrUiAction QrUi_Touch(QrUi *ui, int32_t x, int32_t y, uint8_t down);
void QrUi_Process(QrUi *ui, const QrService *qr, const MotorBus *motor, uint32_t now_ms);
#endif
