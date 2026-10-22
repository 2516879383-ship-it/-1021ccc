/*
 * 扫码 PB12 触发输出：高电平为空闲，按配置周期输出低脉冲。
 * 初始化先写高再切输出；运行期用毫秒期限切相位，不用阻塞延时。
 */
#include "bsp_qr_trigger.h"
#include "stm32f4xx_hal.h"
#include "./qr/qr_config.h"

#if QR_TRIG_LOW_MS >= QR_TRIG_PERIOD_MS
#error "QR_TRIG_LOW_MS must be shorter than QR_TRIG_PERIOD_MS"
#endif

#define QR_TRIG_GAP_MS (QR_TRIG_PERIOD_MS - QR_TRIG_LOW_MS)

static uint8_t trig_low;
static uint32_t phase_ms;

void BspQrTrigger_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /* Write idle high before switching the pin to output, so reset/init
       cannot emit a spurious low pulse. */
    HAL_GPIO_WritePin(QR_TRIG_PORT, QR_TRIG_PIN, GPIO_PIN_SET);
    gpio.Pin = QR_TRIG_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(QR_TRIG_PORT, &gpio);
    trig_low = 0u;
    phase_ms = 0u; /* first Poll starts the first low pulse immediately */
}

/*
 * 每轮最多切换一次高/低相位，并从本次实际切换时刻计时；
 * 主循环延迟会延后脉冲相位，不会在恢复时密集补发多次触发。
 */
void BspQrTrigger_Poll(uint32_t now_ms)
{
    if (trig_low) {
        if ((uint32_t)(now_ms - phase_ms) >= QR_TRIG_LOW_MS) {
            HAL_GPIO_WritePin(QR_TRIG_PORT, QR_TRIG_PIN, GPIO_PIN_SET);
            trig_low = 0u;
            phase_ms = now_ms;
        }
    } else if ((uint32_t)(now_ms - phase_ms) >= QR_TRIG_GAP_MS) {
        HAL_GPIO_WritePin(QR_TRIG_PORT, QR_TRIG_PIN, GPIO_PIN_RESET);
        trig_low = 1u;
        phase_ms = now_ms;
    }
}
