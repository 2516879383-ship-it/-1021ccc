#ifndef CHASSIS_ODOMETRY_H
#define CHASSIS_ODOMETRY_H
#include "chassis_config.h"
#include "./flow/flow_service.h"
#include "./gyro/gyro_service.h"
typedef struct {
    uint32_t sequence, sample_ms;
    double angle;
} ChassisGyroPoint;
typedef struct {
    ChassisGyroPoint gyro[2];
    GyroSnapshot gyro_guard;
    FlowSnapshot flow_guard;
    double x, y, flow_angle;
    uint32_t generation;
    uint8_t gyro_count, baseline, valid;
} ChassisOdometry;
int Chassis_GyroValid(const GyroSnapshot *g, uint32_t now);
int Chassis_GyroContinuous(const GyroSnapshot *a, const GyroSnapshot *b);
void Chassis_OdometryProcess(ChassisOdometry *o, const ChassisConfig *c,
    const FlowSnapshot *f, const GyroSnapshot *g, uint32_t now);
#endif
