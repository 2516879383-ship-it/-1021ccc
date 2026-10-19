#ifndef MOTOR_TYPES_H
#define MOTOR_TYPES_H
#include <stdint.h>

#define MOTOR_VALID_POSITION 1u
#define MOTOR_VALID_SPEED    2u
#define MOTOR_VALID_STATUS   4u
#define MOTOR_DATA_STALE_MS  500u

typedef enum {
    MOTOR_ERROR_NONE, MOTOR_ERROR_TIMEOUT, MOTOR_ERROR_REJECTED,
    MOTOR_ERROR_FORMAT, MOTOR_ERROR_RX_OVERFLOW, MOTOR_ERROR_UART,
    MOTOR_ERROR_MOTION_TIMEOUT, MOTOR_ERROR_STALL
} MotorError;

typedef struct {
    uint8_t address, online, valid, status_flags;
    int64_t position_counts;
    int32_t speed_rpm;
    uint32_t position_ms, speed_ms, status_ms, last_rx_ms;
    uint32_t position_seq, speed_seq, status_seq;
    uint32_t tx_count, rx_count, bad_frames, timeouts;
    uint32_t command_seq;
    uint8_t command_code, command_result;
    MotorError last_error;
} MotorSnapshot;
#endif
