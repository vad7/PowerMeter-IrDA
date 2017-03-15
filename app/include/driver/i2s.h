#ifndef _I2S__H_
#define _I2S__H_

#include "c_types.h"
#include "user_config.h"

#ifdef I2S_CLOCK_OUT
#ifndef USE_I2S
#define USE_I2S
#endif

void ICACHE_FLASH_ATTR i2s_clock_out(void);

#endif

#if USE_I2S_DMA
#define I2SDMABUFCNT (8)//(14)			//Number of buffers in the I2S circular buffer (>3)
#define I2SDMABUFLEN (64)//(32*2)		//Length of one buffer, in 32-bit words (>2)

long underrunCnt; // DMA underrun counter

void ICACHE_FLASH_ATTR i2sInit(int rate, int lockBitcount, uint32 sample);
void i2sSetRate(int rate, int lockBitcount);
bool i2sPushSample(unsigned int sample);
bool ICACHE_FLASH_ATTR i2s_is_full();
bool ICACHE_FLASH_ATTR i2s_is_empty();
#endif
#endif
