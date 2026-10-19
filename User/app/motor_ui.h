#ifndef MOTOR_UI_H
#define MOTOR_UI_H
#include "./motor/motor_types.h"
void MotorUi_Init(void);
void MotorUi_Process(const MotorSnapshot *data, const char *stage, uint32_t now_ms);
#endif
