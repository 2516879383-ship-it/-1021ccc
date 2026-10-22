/*
 * 应用编排层：串联七电机总线、扫码、触摸、光流、陀螺与统一界面。
 * 本层负责把按键意图分发给服务，并在耗时步骤之间重新读取时钟、服务陀螺；
 * 协议解析、硬件恢复和绘图细节分别由对应模块维护。
 */
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
/*
 * 结束本次自动启动流程并请求四轮停止；停止仍由总线后续发送、重试和反馈确认。
 */
static void boot_cancel(QrApp *a, uint32_t now)
{
    if (a->boot_phase == BOOT_DONE) return;
    a->boot_phase = BOOT_DONE;
    MotorBus_Stop(&a->motor, MOTOR_WHEELS, now);
    a->motor.message = "BOOT 1-4 ABORTED / STOP VERIFY"; a->ui.redraw = 1u;
}
/*
 * 四轮上电测试状态机：等待四轴新鲜健康零速反馈，再逐轴使能、逐轴启动、
 * 逐轴停止并等待确认。步骤有期限，任一异常转取消；本流程只驱动地址 1～4。
 * 启动分支以命令已提交为节拍依据，实际运动状态仍需总线反馈确认。
 */
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

/*
 * 把界面动作转换为总线请求：手动/只读切换、各范围 STOP、单轴或四轮测试。
 * 运行测试前按配置检查动作类型；手动干预先取消上电测试，避免两套流程同时发起动作。
 */
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

/*
 * 建立所有服务及 UI 的初始状态，启动光流/陀螺的分阶段初始化，
 * 并按 MOTOR_BOOT_SEQUENCE_ENABLED 决定是否进入四轮上电测试。
 */
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
/*
 * 给陀螺校准提供拒绝原因：活动、未知、未确认停止或无已知阻碍。
 * 新鲜度使用当前时间；任何轴未确认 STOP 都优先拦截。配置范围内已发现/命令的轴
 * 若反馈不足则视为未知，从未出现的节点可缺省；返回 NONE 本身不能证明实物静止。
 */
static uint8_t motor_active(const QrApp *a, uint32_t now)
{
    unsigned i;
    uint8_t condition = GYRO_MOTOR_NONE;
    if (a->boot_phase!=BOOT_DONE || a->motor.queue_count || a->motor.wheels_pending ||
        a->motor.sync_state==MOTOR_SYNC_PREPARE || a->motor.sync_state==MOTOR_SYNC_TRIGGER ||
        a->motor.sync_state==MOTOR_SYNC_VERIFY) condition = GYRO_MOTOR_ACTIVE;
    for (i=0u;i<MOTOR_COUNT;++i) {
        const MotorNode *n=&a->motor.nodes[i];
        if (n->stop_unconfirmed || n->action==MOTOR_STOP_PENDING) return GYRO_MOTOR_STOP;
        if (!(MOTOR_CAL_AXIS_MASK & (1u << i))) continue;
        if (n->motion || n->action==MOTOR_QUEUED || n->action==MOTOR_SENT ||
            n->action==MOTOR_ACCEPTED || n->action==MOTOR_EXECUTING ||
            (MotorBus_Fresh(n,MOTOR_VALID_SPEED,now) && n->data.speed_rpm!=0)) {
            if (condition != GYRO_MOTOR_UNKNOWN) condition = GYRO_MOTOR_ACTIVE;
        } else if (n->action==MOTOR_UNKNOWN ||
            ((n->data.rx_count || n->data.valid || n->request_id || n->stop_sent) &&
             (!n->data.online || !MotorBus_Fresh(n,MOTOR_VALID_SPEED,now) ||
              !MotorBus_Fresh(n,MOTOR_VALID_STATUS,now) || (n->data.status_flags & 0x0Cu))))
            condition = GYRO_MOTOR_UNKNOWN;
    }
    return condition;
}
/*
 * 主循环的一轮：接收电机字节并推进事务/上电测试，处理触摸动作，
 * 更新扫码触发与解码、光流和陀螺，最后提交快照并推进一个绘图片段。
 * 在触摸、传感器、绘图前后刷新 AppClock，避免沿用已过时的时间；
 * 多次调用 Gyro_Process 由其内部轮询间隔限频，不会把同一样本重复积分。
 */
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
    now = AppClock_Now(); Gyro_Process(&a->gyro, now, motor_active(a, now));
    AppTouch_Process(now); AppTouch_GetState(&x, &y, &down);
    now = AppClock_Now();
    action = QrUi_Touch(&a->ui, x, y, down);
    if (action == QR_UI_RESCAN)
        BspQrUart_BeginSession(&a->qr, now);
    else if (action == QR_UI_FLOW_CLEAR) Flow_ResetTotals(&a->flow);
    else if (action == QR_UI_GYRO_CAL) {
        (void)Gyro_RequestCalibration(&a->gyro, now, motor_active(a, now));
    } else if (action == QR_UI_GYRO_ZERO) {
        (void)Gyro_ZeroAngle(&a->gyro, now);
    } else if (action==QR_UI_MODE || action==QR_UI_RUN || action==QR_UI_STOP_AXIS ||
        action==QR_UI_STOP_WHEELS || action==QR_UI_STOP_ALL) manual_action(a, action, now);
    BspQrTrigger_Poll(now);
    BspQrUart_Pump(&a->qr, now);
    now = AppClock_Now(); /* Capture IRQs/touch I2C can advance SysTick during a step. */
    QrService_Process(&a->qr, now);
    Flow_Process(&a->flow, now);
    now = AppClock_Now();
    Gyro_Process(&a->gyro, now, motor_active(a, now));
    now = AppClock_Now();
    Flow_GetSnapshot(&a->flow, now, &flow);
    Gyro_GetSnapshot(&a->gyro, now, &gyro);
    QrUi_Process(&a->ui, &a->qr, &a->motor, &flow, &gyro, now);
    if (AppClock_Now() - now > a->ui.max_step_ms) a->ui.max_step_ms = AppClock_Now() - now;
    now = AppClock_Now(); Gyro_Process(&a->gyro, now, motor_active(a, now));
}
