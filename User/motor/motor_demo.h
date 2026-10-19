#ifndef MOTOR_DEMO_H
#define MOTOR_DEMO_H
#include "motor_service.h"

typedef enum {
    DEMO_BOOT, DEMO_WAIT_LINK, DEMO_SEND_ENABLE, DEMO_ENABLE_ACK, DEMO_ENABLE_CONFIRM,
    DEMO_SEND_SPEED, DEMO_SPEED_ACK, DEMO_SPEED_RUN,
    DEMO_SEND_STOP, DEMO_STOP_ACK, DEMO_WAIT_ZERO,
    DEMO_SEND_MOVE, DEMO_MOVE_ACK, DEMO_WAIT_POSITION,
    DEMO_SEND_DISABLE, DEMO_DISABLE_ACK, DEMO_DISABLE_CONFIRM, DEMO_DONE,
    DEMO_FAULT_SEND_STOP, DEMO_FAULT_STOP_ACK, DEMO_FAULT_SEND_DISABLE,
    DEMO_FAULT_DISABLE_ACK, DEMO_FAULT_VERIFY, DEMO_FAULT
} MotorDemoState;

typedef struct {
    MotorDemoState state;
    uint32_t boot_ms, state_ms, expected_command_seq, observed_speed_seq, observed_status_seq;
    uint32_t observed_position_seq;
    int64_t position_before;
    uint8_t zero_samples, cleanup_confirmed, position_started, position_changed;
    MotorError fault;
} MotorDemo;

void MotorDemo_Init(MotorDemo *demo, uint32_t now_ms);
void MotorDemo_Process(MotorDemo *demo, MotorService *service, uint32_t now_ms);
const char *MotorDemo_StateName(const MotorDemo *demo);
#endif
