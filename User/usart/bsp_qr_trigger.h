#ifndef BSP_QR_TRIGGER_H
#define BSP_QR_TRIGGER_H
#include <stdint.h>
/* XM1605 TRIG pin 12, active low. Hardware needs the manual's drive circuit:
   10k pull-up to 3.3V on the module pin, 100R series, optional 100nF/TVS. */
void BspQrTrigger_Init(void);
/* Non-blocking: drive the periodic 800/200 ms trigger waveform from the
   main loop (period and low time are configured in qr_config.h). */
void BspQrTrigger_Poll(uint32_t now_ms);
#endif
