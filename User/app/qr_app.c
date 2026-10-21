#include "qr_app.h"
#include "app_runtime.h"
#include "./flow/bsp_flow_spi.h"
#include "./gyro/bsp_gyro_bus.h"
#include "./usart/bsp_qr_uart.h"
#include "./usart/bsp_qr_trigger.h"
#include "./usart/bsp_motor_uart.h"
#include "./motor/motor_test_config.h"
#include <string.h>

enum { BOOT_DONE, BOOT_READY, BOOT_ENABLE, BOOT_START, BOOT_STOP };
static int reached(uint32_t now, uint32_t at) { return (int32_t)(now - at) >= 0; }
static void boot_cancel(QrApp *a, uint32_t now)
{
    if (a->boot_phase == BOOT_DONE) return;
    a->boot_phase = BOOT_DONE;
    MotorBus_Stop(&a->motor, MOTOR_WHEELS, now);
    a->motor.message = "BOOT 1-4 ABORTED / STOP VERIFY"; a->ui.redraw = 1u;
}
static void boot_process(QrApp *a, uint32_t now)
{
    MotorNode *n = &a->motor.nodes[a->boot_index];
    unsigned i;
    if (a->boot_phase == BOOT_DONE) return;
    for (i = 0u; i < 4u; ++i) if ((a->boot_started & (1u << i)) &&
        !(a->boot_stopping & (1u << i)) && !a->motor.nodes[i].motion) break;
    if (i < 4u || reached(now, a->boot_deadline) ||
        (a->boot_waiting && (n->action == MOTOR_UNKNOWN || n->action == MOTOR_REJECTED ||
         n->action == MOTOR_CANCELLED || (a->boot_phase != BOOT_STOP && n->action == MOTOR_STOP_PENDING)))) {
        boot_cancel(a, now); a->motor.manual = 0u; return;
    }
    if (a->boot_phase == BOOT_READY) {
        for (i = 0u; i < 4u; ++i) {
            const MotorNode *node = &a->motor.nodes[i];
            if (!node->data.online || !MotorBus_Fresh(node, MOTOR_VALID_STATUS, now) ||
                !MotorBus_Fresh(node, MOTOR_VALID_SPEED, now) || node->data.speed_rpm != 0 ||
                (node->data.status_flags & 0x0Cu)) return;
        }
        MotorBus_SetManual(&a->motor, 1u, now); a->ui.redraw = 1u;
        a->boot_phase = BOOT_ENABLE; a->boot_deadline = now + 3000u;
    }
    if (a->boot_phase == BOOT_ENABLE) {
        if (!a->boot_waiting) {
            if (MotorBus_Enable(&a->motor, (uint8_t)(a->boot_index + 1u), 1u, now)) {
                a->boot_waiting = 1u; a->motor.message = "BOOT 1-4 ENABLING";
            }
        } else if (n->action == MOTOR_COMPLETE) {
            a->boot_waiting = 0u; a->boot_deadline = now + 3000u;
            if (++a->boot_index == 4u) {
                a->boot_index = 0u; a->boot_phase = BOOT_START;
                a->boot_due = now + MOTOR_BOOT_STEP_MS;
            }
        }
    } else if (a->boot_phase == BOOT_START) {
        if (!a->boot_waiting && reached(now, a->boot_due)) {
            MotorAction m;
            memset(&m, 0, sizeof(m)); m.lease_ms = 5000u;
            m.command = Emm_VelocityEx((uint8_t)(a->boot_index + 1u), MOTOR_TEST_DIRECTION,
                MOTOR_BOOT_RPM, MOTOR_TEST_ACCELERATION, 0u);
            if (MotorBus_Submit(&a->motor, &m, now)) {
                a->boot_waiting = 1u; a->motor.message = "BOOT 1-4 STARTING";
            }
        } else if (a->boot_waiting && n->motion) {
            /* Schedule from actual transmission; an ACK is not proof of rotation. */
            a->boot_started |= (uint8_t)(1u << a->boot_index);
            a->boot_waiting = 0u; a->boot_due = now + MOTOR_BOOT_STEP_MS;
            a->boot_deadline = a->boot_due + 1000u;
            if (++a->boot_index == 4u) { a->boot_index = 0u; a->boot_phase = BOOT_STOP; }
        }
    } else if (a->boot_phase == BOOT_STOP) {
        if (!a->boot_waiting && reached(now, a->boot_due)) {
            uint8_t mask = (uint8_t)(1u << a->boot_index);
            a->boot_stopping |= mask; MotorBus_Stop(&a->motor, mask, now);
            a->boot_waiting = 1u; a->boot_due = now + MOTOR_BOOT_STEP_MS;
            a->boot_deadline = now + MOTOR_STOP_TIMEOUT_MS;
            a->motor.message = "BOOT 1-4 STOPPING / VERIFY";
        } else if (a->boot_waiting && n->action == MOTOR_STOPPED && reached(now, a->boot_due)) {
            a->boot_waiting = 0u; a->boot_deadline = now + MOTOR_STOP_TIMEOUT_MS;
            if (++a->boot_index == 4u) {
                a->boot_index = 0u; a->boot_phase = BOOT_DONE;
                /* All four stops verified; avoid SetManual(0), which also stops 5..7. */
                a->motor.manual = 0u; a->ui.redraw = 1u;
                a->motor.message = "BOOT 1-4 DONE / READ ONLY";
            }
        }
    }
}

static void manual_action(QrApp *a, QrUiAction action, uint32_t now)
{
    uint8_t address = (uint8_t)(a->ui.selected + 1u);
    if (action >= QR_UI_MODE && action <= QR_UI_STOP_ALL) boot_cancel(a, now);
    if (action == QR_UI_MODE) MotorBus_SetManual(&a->motor, (uint8_t)!a->motor.manual, now);
    else if (action == QR_UI_STOP_ALL) MotorBus_Stop(&a->motor, MOTOR_ALL, now);
    else if (action == QR_UI_STOP_WHEELS) MotorBus_Stop(&a->motor, MOTOR_WHEELS, now);
    else if (action == QR_UI_STOP_AXIS) MotorBus_Stop(&a->motor, (uint8_t)(1u << a->ui.selected), now);
    else if (action == QR_UI_RUN && a->motor.manual) {
        if (a->ui.test_action == 0u) { (void)MotorBus_Enable(&a->motor, address, 1u, now); return; }
#if MOTOR_TEST_CONFIGURED || MOTOR_BENCH_JOG_ENABLED
        {
            MotorAction m;
            memset(&m, 0, sizeof(m));
            m.lease_ms = MOTOR_TEST_LEASE_MS; m.timeout_ms = MOTOR_TEST_POSITION_TIMEOUT_MS;
            m.pulses_per_rev = MOTOR_TEST_PULSES_PER_REV;
#if MOTOR_TEST_CONFIGURED
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
            /* Bench jog: an explicit MANUAL + RUN ONCE with a short low-speed
               lease remains available while MOTOR_TEST_CONFIGURED=0. Position,
               0xAA wheel group and sync stay gated by MOTOR_TEST_CONFIGURED. */
            if (a->ui.test_action == 1u) {
                m.command = Emm_VelocityEx(address, MOTOR_TEST_DIRECTION, MOTOR_TEST_RPM,
                    MOTOR_TEST_ACCELERATION, 0u);
                (void)MotorBus_Submit(&a->motor, &m, now);
            } else {
                a->motor.message = "CONFIGURE: POSITION/GROUP/SYNC";
            }
#endif
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
    Flow_Init(&a->flow, BspFlowSpi_Io(), now);
    Gyro_Init(&a->gyro, BspGyroBus_Io(), now);
    a->boot_phase = MOTOR_BOOT_SEQUENCE_ENABLED ? BOOT_READY : BOOT_DONE;
    a->boot_index = a->boot_waiting = a->boot_started = a->boot_stopping = 0u;
    a->boot_due = now; a->boot_deadline = now + 10000u;
    if (a->boot_phase != BOOT_DONE) a->motor.message = "BOOT 1-4 WAIT FEEDBACK";
}
static uint8_t motor_active(const QrApp *a)
{
    unsigned i;
    if (a->boot_phase!=BOOT_DONE || a->motor.queue_count || a->motor.wheels_pending ||
        a->motor.sync_state==MOTOR_SYNC_PREPARE || a->motor.sync_state==MOTOR_SYNC_TRIGGER ||
        a->motor.sync_state==MOTOR_SYNC_VERIFY) return 1u;
    for (i=0u;i<MOTOR_COUNT;++i) {
        const MotorNode *n=&a->motor.nodes[i];
        if (n->motion || n->action==MOTOR_QUEUED || n->action==MOTOR_SENT ||
            n->action==MOTOR_ACCEPTED || n->action==MOTOR_EXECUTING || n->action==MOTOR_STOP_PENDING ||
            ((n->data.valid&MOTOR_VALID_SPEED) && n->data.speed_rpm!=0)) return 1u;
    }
    return 0u;
}
void QrApp_Process(QrApp *a, uint32_t now)
{
    int32_t x, y;
    FlowSnapshot flow;
    GyroSnapshot gyro;
    uint8_t down;
    QrUiAction action;
    BspMotorUart_PumpBus(&a->motor, now);
    MotorBus_Process(&a->motor, now);
    boot_process(a, now);
    Gyro_Process(&a->gyro, AppClock_Now(), motor_active(a));
    AppTouch_Process(now); AppTouch_GetState(&x, &y, &down);
    now = AppClock_Now();
    action = QrUi_Touch(&a->ui, x, y, down);
    if (action == QR_UI_RESCAN)
        BspQrUart_BeginSession(&a->qr, now);
    else if (action == QR_UI_FLOW_CLEAR) Flow_ResetTotals(&a->flow);
    else if (action == QR_UI_GYRO_CAL) {
        (void)Gyro_RequestCalibration(&a->gyro, now, motor_active(a)); a->ui.redraw=1u;
    } else if (action == QR_UI_GYRO_ZERO) {
        (void)Gyro_ZeroAngle(&a->gyro, now); a->ui.redraw=1u;
    } else if (action==QR_UI_MODE || action==QR_UI_RUN || action==QR_UI_STOP_AXIS ||
        action==QR_UI_STOP_WHEELS || action==QR_UI_STOP_ALL) manual_action(a, action, now);
    BspQrTrigger_Poll(now);
    BspQrUart_Pump(&a->qr, now);
    now = AppClock_Now(); /* Capture IRQs/touch I2C can advance SysTick during a step. */
    QrService_Process(&a->qr, now);
    Flow_Process(&a->flow, now);
    now = AppClock_Now();
    Gyro_Process(&a->gyro, now, motor_active(a));
    now = AppClock_Now();
    Flow_GetSnapshot(&a->flow, now, &flow);
    Gyro_GetSnapshot(&a->gyro, now, &gyro);
    QrUi_Process(&a->ui, &a->qr, &a->motor, &flow, &gyro, now);
    Gyro_Process(&a->gyro, AppClock_Now(), motor_active(a));
}
