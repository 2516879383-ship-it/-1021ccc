#include "qr_app.h"
#include "app_runtime.h"
#include "./usart/bsp_qr_uart.h"
#include "./usart/bsp_qr_trigger.h"
#include "./usart/bsp_motor_uart.h"
#include "./motor/motor_test_config.h"
#include <string.h>

static void manual_action(QrApp *a, QrUiAction action, uint32_t now)
{
    uint8_t address = (uint8_t)(a->ui.selected + 1u);
    if (action == QR_UI_MODE) MotorBus_SetManual(&a->motor, (uint8_t)!a->motor.manual, now);
    else if (action == QR_UI_STOP_ALL) MotorBus_Stop(&a->motor, MOTOR_ALL, now);
    else if (action == QR_UI_STOP_WHEELS) MotorBus_Stop(&a->motor, MOTOR_WHEELS, now);
    else if (action == QR_UI_STOP_AXIS) MotorBus_Stop(&a->motor, (uint8_t)(1u << a->ui.selected), now);
    else if (action == QR_UI_RUN && a->motor.manual) {
#if MOTOR_TEST_CONFIGURED
        MotorAction m;
#endif
        if (a->ui.test_action == 0u) { (void)MotorBus_Enable(&a->motor, address, 1u, now); return; }
#if MOTOR_TEST_CONFIGURED
        memset(&m, 0, sizeof(m));
        m.lease_ms = MOTOR_TEST_LEASE_MS; m.timeout_ms = MOTOR_TEST_POSITION_TIMEOUT_MS;
        m.pulses_per_rev = MOTOR_TEST_PULSES_PER_REV;
        if (a->ui.test_action == 1u) {
            m.command = Emm_VelocityEx(address, MOTOR_TEST_DIRECTION, MOTOR_TEST_RPM, MOTOR_TEST_ACCELERATION, 0u);
            (void)MotorBus_Submit(&a->motor, &m, now);
        } else if (a->ui.test_action == 2u) {
            if (address < 5u) { a->motor.message = "POSITION: SELECT AXIS 5..7"; return; }
            m.command = Emm_MoveEx(address, MOTOR_TEST_DIRECTION, MOTOR_TEST_RPM, MOTOR_TEST_ACCELERATION,
                MOTOR_TEST_PULSES, MOTOR_TEST_POSITION_MODE, 0u);
            (void)MotorBus_Submit(&a->motor, &m, now);
        } else {
            const int32_t rpm[4] = MOTOR_TEST_WHEEL_RPM;
            if (a->ui.test_action == 3u) (void)MotorBus_Wheels(&a->motor, rpm, MOTOR_TEST_ACCELERATION, MOTOR_TEST_LEASE_MS, now);
            else {
                MotorAction group[4];
                unsigned i;
                for (i = 0u; i < 4u; ++i) {
                    group[i] = m;
                    group[i].command = Emm_VelocityEx((uint8_t)(i + 1u), (uint8_t)(rpm[i] < 0),
                        (uint16_t)(rpm[i] < 0 ? -rpm[i] : rpm[i]), MOTOR_TEST_ACCELERATION, 1u);
                }
                (void)MotorBus_Sync(&a->motor, group, 4u, now);
            }
        }
#else
        a->motor.message = "CONFIGURE motor_test_config.h";
#endif
    }
}

void QrApp_Init(QrApp *a, MotorTransport motor, uint8_t ready, uint32_t now)
{
    MotorBus_Init(&a->motor, motor, now);
    QrService_Init(&a->qr, ready, now); QrUi_Init(&a->ui);
}
void QrApp_Process(QrApp *a, uint32_t now)
{
    int32_t x, y;
    uint8_t down;
    QrUiAction action;
    BspMotorUart_PumpBus(&a->motor, now);
    MotorBus_Process(&a->motor, now);
    AppTouch_Process(now); AppTouch_GetState(&x, &y, &down);
    now = AppClock_Now();
    action = QrUi_Touch(&a->ui, x, y, down);
    if (action == QR_UI_RESCAN)
        BspQrUart_BeginSession(&a->qr, now);
    else manual_action(a, action, now);
    BspQrTrigger_Poll(now);
    BspQrUart_Pump(&a->qr, now);
    now = AppClock_Now(); /* Capture IRQs/touch I2C can advance SysTick during a step. */
    QrService_Process(&a->qr, now);
    QrUi_Process(&a->ui, &a->qr, &a->motor, now);
}
