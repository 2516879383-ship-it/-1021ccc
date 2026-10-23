#ifndef CHASSIS_KINEMATICS_H
#define CHASSIS_KINEMATICS_H
#include "chassis_config.h"
typedef struct { float vx, vy, omega; } ChassisVelocity; /* mm/s, mm/s, deg/s */
typedef struct {
    ChassisVelocity limited;
    float rpm[4], scale;
    int32_t quantized[4];
} ChassisWheels;
int Chassis_Finite(double value);
int Chassis_Solve(const ChassisConfig *config, ChassisVelocity input, ChassisWheels *out);
#endif
