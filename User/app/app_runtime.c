/*
 * 运行期公共适配：提供 HAL 毫秒时钟，并把触摸 EXTI 通知转成前台 I2C 扫描。
 * 中断仅设置待处理标志；前台维护多指状态，向 UI 发布一次连续按住状态。
 */
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

/*
 * 有中断通知或手指仍按住时，最多每 20 ms 扫描一次触摸。
 * 仅在取走通知标志时短暂关中断，实际 I2C 读取在恢复中断后执行。
 */
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

/*
 * 收集驱动上报的触点：首个按下的触点负责坐标，任意触点未松开都保持 down。
 * 这样第二根手指不会在第一次按住期间制造新的按下边沿。
 */
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
