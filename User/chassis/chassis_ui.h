#ifndef CHASSIS_UI_H
#define CHASSIS_UI_H
#include "chassis.h"
void ChassisUi_Line(char out[43], unsigned row, uint8_t detail,
                    const Chassis *chassis, uint32_t now_ms);
#endif
