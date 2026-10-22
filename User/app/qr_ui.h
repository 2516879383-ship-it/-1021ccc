#ifndef QR_UI_H
#define QR_UI_H
#include "./qr/qr_service.h"
#include "./motor/motor_bus.h"
#include "./flow/flow_service.h"
#include "./gyro/gyro_service.h"
typedef enum { QR_UI_NONE, QR_UI_RESCAN, QR_UI_MODE, QR_UI_RUN,
               QR_UI_STOP_AXIS, QR_UI_STOP_WHEELS, QR_UI_STOP_ALL, QR_UI_FLOW_CLEAR,
               QR_UI_GYRO_CAL, QR_UI_GYRO_ZERO } QrUiAction;
typedef struct {
    char previous[9][43];
    char target[9][43]; /* one coherent refresh snapshot, frozen until painted */
    uint32_t last_draw, max_step_ms;
    uint16_t clear_y;
    uint8_t drawing, row, offset, strip, button, job_flags;
    uint8_t flow_page, flow_detail;
    uint8_t gyro_page, gyro_detail;
    uint8_t motor_page, was_down, redraw, selected, detail, test_action;
} QrUi;
void QrUi_Init(QrUi *ui);
QrUiAction QrUi_Touch(QrUi *ui, int32_t x, int32_t y, uint8_t down);
void QrUi_Process(QrUi *ui, const QrService *qr, const MotorBus *motor, const FlowSnapshot *flow,
                  const GyroSnapshot *gyro, uint32_t now_ms);
#endif
