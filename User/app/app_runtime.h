#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H
#include <stdint.h>
void AppTouch_Notify(void);
void AppTouch_Process(uint32_t now_ms);
void AppTouch_Report(int32_t id, int32_t x, int32_t y, uint8_t pressed);
void AppTouch_GetState(int32_t *x, int32_t *y, uint8_t *pressed);
#endif
