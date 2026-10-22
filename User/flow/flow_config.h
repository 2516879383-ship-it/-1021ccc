#ifndef FLOW_CONFIG_H
#define FLOW_CONFIG_H
/* Verified V1 headers: PB13/J7-54, PC2/J7-16, PC3/J7-52, PE3/J7-49.
   SPI2 /64 from 45 MHz APB1 = 703125 Hz. No DMA or IRQ allocation. */
#define FLOW_SAMPLE_MS 10u
#define FLOW_STALE_MS 100u
#define FLOW_ID_CHECK_MS 500u
#define FLOW_INVALID_RESTART_MS 2000u
#define FLOW_RETRY_MIN_MS 1000u
#define FLOW_RETRY_MAX_MS 8000u
/* Conservative commissioning policy, NOT a calibrated accuracy guarantee. */
#ifndef FLOW_MIN_SQUAL
#define FLOW_MIN_SQUAL 25u
#endif
#ifndef FLOW_SWAP_XY
#define FLOW_SWAP_XY 0
#endif
#ifndef FLOW_X_SIGN
#define FLOW_X_SIGN 1
#endif
#ifndef FLOW_Y_SIGN
#define FLOW_Y_SIGN 1
#endif
#if (FLOW_X_SIGN != 1 && FLOW_X_SIGN != -1) || (FLOW_Y_SIGN != 1 && FLOW_Y_SIGN != -1)
#error Flow axis signs must be +1 or -1
#endif
/* HAL SPI timeout is quantized by the 1 ms SysTick (HAL aborts when the tick
   count changes during a call). A 1 ms timeout can abort a correct ~0.15 ms
   transfer that straddles a tick edge; 2 ms cannot false-timeout any transfer
   shorter than 1 ms, while a genuinely stuck bus is still bounded. */
#define FLOW_SPI_TIMEOUT_MS 2u
#endif
