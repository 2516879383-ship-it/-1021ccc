#ifndef QR_UI_H
#define QR_UI_H
#include "./qr/qr_service.h"
#include "./motor/motor_types.h"
typedef enum { QR_UI_NONE, QR_UI_RESCAN } QrUiAction;
typedef struct {
    char previous[9][43];
    uint32_t last_draw;
    uint8_t motor_page, was_down, redraw;
} QrUi;
void QrUi_Init(QrUi *ui);
QrUiAction QrUi_Touch(QrUi *ui, int32_t x, int32_t y, uint8_t down);
void QrUi_Process(QrUi *ui, const QrService *qr, const MotorSnapshot *motor, uint32_t now_ms);
#endif
