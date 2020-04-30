/*
 * Power Meter
 *
 * Count pulses from meter.
 *
 * Written by vad7
 */

#ifndef _power_meter_
#define _power_meter_

#include "sntp.h"
#include "debug_ram.h"

#define FRAM_MAX_BLOCK_AT_ONCE 	64		// FARM SPI read/write is limited to 64 bytes at one time
#define TIME_STEP_SEC			60 		// 1 minute
#define IOT_INI_MAX_FILE_SIZE	1024
#define CONNECTION_LOST_FLAG_MAX 60 	// *period
uint8_t connection_lost_flag;

//#define SENSOR_PIN				3
//#define SENSOR_TASK_PRIO		USER_TASK_PRIO_2 // Hi prio, _0,1 - may be used
//uint8_t SENSOR_FRONT_EDGE;
//uint8_t SENSOR_BACK_EDGE;

typedef struct {
	uint32 	Fram_Size;			// 32768
	uint16  fram_freq;			// i2c = 400 kHz / spi = 20 MHz
	uint16	page_refresh_time;	// ms
	char	csv_delimiter; 		// ','
	char	csv_delimiter_dec;	// '.'
	char	csv_delimiter_total;// ';'
	char	csv_delimiter_total_dec;// ','
	uint16 	unused;				//PulsesPer0_01KWt; 	// 10
	uint16  request_period;		// sec
	int16_t	TimeAdjust;			// -+ sec
	uint16	TimeMaxMismatch;	// sec, between meter and SNTP time to correct time, 0 - dont.
	uint16	TimeT1Start;		// hh,mm
	uint16	TimeT1End;			// hh,mm. if TimeT1Start != TimeT1End != 0 - Dual tariffs used
	uint32  pwmt_response_timeout; // us
	uint32  pwmt_read_timeout;  // us
	uint32  pwmt_delay_after_err;  // us
	uint8	Pass[2][6];
	uint8	iot_cloud_enable;	// use "protect/iot_cloud.ini" to send data to iot cloud
	uint8	pwmt_address;		// power meter network address, 0 - group
	uint16  Tariffs[2];			// T1, T2. *100
	uint8	pwmt_on_error_repeat_cnt;
	uint16  SNTP_update_delay_min; // 0 - don't do it
	char 	sntp_server[20];
} CFG_GLO;
CFG_GLO __attribute__((aligned(4))) cfg_glo;

typedef struct {
	struct {
		uint32 PowerCnt;	// not processed count
		uint32 TotalCnt;	// saved value
		uint32 PtrCurrent;	// ArrayOfCntsElement size array
		time_t LastTime;
	} ByMin;
	uint32 LastTotal;		// 0.000 kwt*h
	uint32 LastTotal_T1;	// 0.000 kwt*h
	uint32 PtrCurrentByDay;	// StartArrayByDay index
	uint16 LastDay;			// day+1 (time_t/SECSPERDAY)
} FRAM_STORE;
FRAM_STORE __attribute__((aligned(4))) fram_store;
#define ArrayByDayStart		STORE_FLASH_ADDR // sector rounded, array[2] of (NewTotal-LastTotal), kwt*h
#define ArrayByDaySize		65536 // 8192 days
typedef int32 ArrayByDayElement[2]; // 8 bytes { uint32 delta TAll; uint32 delta T1 }
#define ArrayOfCntsStart	(((ArrayByDayStart + ArrayByDaySize) + (flashchip_sector_size - 1)) & ~(flashchip_sector_size - 1)) // until end of flash
uint32  ArrayOfCntsSize;
#define ArrayOfCntsElement	2 // uint16

uint32 LastCnt;				// Last cnt
uint32 LastCnt_Previous;
uint32 KWT_Previous;
// Cookies:
uint32 Web_ChartMaxDays; 	// ~ChartMaxDays~
uint32 Web_ChMD;			// ~ChMD~, Chart max days for historyall.htm
uint8  Web_ShowBy; 			// ~ShowBy~ : 0 - all, 1 - by day, 2 - by hour
//
void user_initialize(uint8 index) ICACHE_FLASH_ATTR;
void FRAM_Store_Init(void) ICACHE_FLASH_ATTR;
bool write_power_meter_cfg(void) ICACHE_FLASH_ATTR;
void power_meter_clear_all_data(uint8 mask) ICACHE_FLASH_ATTR;
uint8_t iot_cloud_init(void) ICACHE_FLASH_ATTR;
void iot_data_clear(void) ICACHE_FLASH_ATTR;
void iot_cloud_send(uint8 fwork) ICACHE_FLASH_ATTR;

void uart_wait_tx_fifo_empty(void) ICACHE_FLASH_ATTR;
void _localtime(time_t * tim_p, struct tm * res) ICACHE_FLASH_ATTR;

// GPIO_PIN_INTR_NEGEDGE - down
// GPIO_PIN_INTR_POSEDGE - up
// GPIO_PIN_INTR_ANYEDGE - both
// GPIO_PIN_INTR_LOLEVEL - low level
// GPIO_PIN_INTR_HILEVEL - high level
// GPIO_PIN_INTR_DISABLE - disable interrupt

#endif
