#ifndef CHASSIS_CONFIG_H
#define CHASSIS_CONFIG_H
#include <stdint.h>

/* Measured vehicle parameters ONLY. Zero/false means unconfigured, never a
   guessed vehicle dimension. Test fixtures supply their own ChassisConfig. */
#define CHASSIS_MECHANICAL_CONFIRMED 0u
#define CHASSIS_FEEDBACK_CONFIRMED 0u
#define CHASSIS_WHEEL_DIAMETER_MM 0.0f
#define CHASSIS_LENGTH_MM 0.0f
#define CHASSIS_WIDTH_MM 0.0f
#define CHASSIS_GEAR_RATIO 0.0f /* motor revolutions / wheel revolution */
#define CHASSIS_MOTOR_SIGNS {0, 0, 0, 0} /* FL FR RL RR; forward contribution */
#define CHASSIS_FLOW_X_MM_PER_COUNT 0.0f
#define CHASSIS_FLOW_Y_MM_PER_COUNT 0.0f
/* These use the already axis-mapped FlowSnapshot coordinates. The feedback
   confirmation flag also confirms the measured mounting angle and offsets. */
#define CHASSIS_FLOW_MOUNT_DEG 0.0f
#define CHASSIS_FLOW_OFFSET_X_MM 0.0f
#define CHASSIS_FLOW_OFFSET_Y_MM 0.0f

typedef struct {
    float wheel_diameter_mm, length_mm, width_mm, gear_ratio;
    float max_rpm, max_speed_mm_s, max_rate_deg_s;
    float acceleration_mm_s2, acceleration_deg_s2;
    float flow_x_mm_count, flow_y_mm_count, flow_mount_deg;
    float flow_offset_x_mm, flow_offset_y_mm;
    float position_kp, heading_kp, rate_damping;
    float position_window_mm, angle_window_deg, rest_rate_deg_s;
    uint32_t control_ms, publish_ms, input_lease_ms, stable_ms, between_ms;
    int8_t motor_sign[4];
    uint8_t mechanical_confirmed, feedback_confirmed, driver_acceleration;
} ChassisConfig;

enum { CHASSIS_CFG_MECHANICAL=1u, CHASSIS_CFG_GEOMETRY=2u,
       CHASSIS_CFG_DIRECTION=4u, CHASSIS_CFG_LIMITS=8u,
       CHASSIS_CFG_FEEDBACK=16u, CHASSIS_CFG_FLOW=32u };
void Chassis_DefaultConfig(ChassisConfig *out);
uint32_t Chassis_ConfigIssues(const ChassisConfig *config, uint8_t closed_loop);
#endif
