#ifndef BSP_FLOW_SPI_H
#define BSP_FLOW_SPI_H
#include "pmw3901.h"
#include "stm32f4xx_hal.h"
FlowIo BspFlowSpi_Io(void);
void BspFlowSpi_MspInit(SPI_HandleTypeDef *spi);
#endif
