#include "app_runtime.h"
#include "stm32f4xx_hal.h"
#include "./touch/gt9xx.h"

static volatile uint8_t touch_pending;
static uint32_t last_touch_ms;
static int32_t touch_x, touch_y;
static uint8_t touch_down;
static uint8_t touch_mask, touch_owner;

uint32_t AppClock_Now(void) { return HAL_GetTick(); }

void AppTouch_Notify(void) { touch_pending = 1u; }

void AppTouch_Process(uint32_t now_ms)
{
    uint32_t mask;
    if ((!touch_pending && !touch_down) || (uint32_t)(now_ms - last_touch_ms) < 20u) return;
    mask = __get_PRIMASK();
    __disable_irq();
    touch_pending = 0u;
    __set_PRIMASK(mask);
    last_touch_ms = now_ms;
    GTP_TouchProcess();
}

void AppTouch_Report(int32_t id, int32_t x, int32_t y, uint8_t pressed)
{
    uint8_t bit;
    if (id < 0 || id >= 5) return;
    bit = (uint8_t)(1u << id);
    if (pressed) {
        if (!touch_mask) touch_owner = (uint8_t)id;
        touch_mask |= bit;
        if (touch_owner == (uint8_t)id) { touch_x = x; touch_y = y; }
    } else touch_mask &= (uint8_t)~bit;
    /* A second finger cannot manufacture another press while one is held. */
    touch_down = (uint8_t)(touch_mask != 0u);
}

void AppTouch_GetState(int32_t *x, int32_t *y, uint8_t *pressed)
{
    *x = touch_x; *y = touch_y; *pressed = touch_down;
}
