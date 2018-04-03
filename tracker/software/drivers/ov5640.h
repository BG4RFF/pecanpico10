/**
  * This is the OV2640 driver
  */

#ifndef __OV5640_H__
#define __OV5640_H__

#include "ch.h"
#include "hal.h"
#include "types.h"

#define OV5640_I2C_ADR		    0x3C

#define OV5640_USE_DMA_DBM      TRUE
#define DMA_SEGMENT_SIZE        1024
#define DMA_FIFO_BURST_ALIGN    32

uint32_t OV5640_Snapshot2RAM(uint8_t* buffer, uint32_t size, resolution_t resolution);
uint32_t OV5640_Capture(uint8_t* buffer, uint32_t size);
void OV5640_InitGPIO(void);
void OV5640_TransmitConfig(void);
void OV5640_SetResolution(resolution_t res);
void OV5640_init(void);
void OV5640_deinit(void);
bool OV5640_isAvailable(void);
void OV5640_setLightIntensity(void);
uint32_t OV5640_getLastLightIntensity(void);
uint8_t OV5640_hasError(void);

#endif

