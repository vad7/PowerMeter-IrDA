/*
 * Power Meter
 *
 * Count pulses from meter.
 *
 * Written by vad7
 */

#include "user_config.h"
#include "c_types.h"
#ifndef BUILD_FOR_OTA_512k
#include "stdlib.h"
#include "hw/esp8266.h"
#include "bios.h"
#include "sdk/add_func.h"
#include "user_interface.h"
#include "web_iohw.h"
#include "flash_eep.h"
#include "webfs.h"
#include "sdk/libmain.h"
#include "driver/eeprom.h"
#include "hw/gpio_register.h"
#include "wifi.h"
#include "wifi_events.h"
#include "power_meter.h"
#include "iot_cloud.h"
#include "mercury.h"

#define GPIO_Tasks 		1
#define GPIO_Int_Signal 1
ETSEvent GPIO_TaskQueue[GPIO_Tasks] DATA_IRAM_ATTR;
os_timer_t update_cnt_timer DATA_IRAM_ATTR;

uint32 PowerCnt = 0;
uint32 PowerCntTime = 0;
uint8  FRAM_Status = 1; 	// 0 - Ok, 1 - Not initialized, 2 - Error
uint8  Sensor_Edge; 		// 0 - Front pulse edge, 1 - Back
uint8  FRAM_STORE_Readed	= 0;

void ICACHE_FLASH_ATTR fram_init(void) {
	eeprom_init(cfg_glo.fram_freq);
}

void ICACHE_FLASH_ATTR update_cnts(time_t time) // 1 minute passed
{
	uint32 pcnt = 0;

	union {
		uint32 u32;
		uint16 u16[2];
	} arrcnt;

	if(time > fram_store.ByMin.LastTime + 200 * SECSPERDAY) { // run: first time or after a long time (200 days)
		#if DEBUGSOO > 2
			os_printf("%u - %u > 200 days\n", time, fram_store.ByMin.LastTime);
		#endif
		fram_store.ByMin.LastTime = time;
		fram_store.ByMin.TotalCnt += fram_store.ByMin.PowerCnt;
		fram_store.ByMin.PowerCnt = 0;
		return;
	} else {
		pcnt = fram_store.ByMin.PowerCnt;
		if(pcnt > 1) {
			time_t tt = time - fram_store.ByMin.LastTime;
			if(tt >= TIME_STEP_SEC * 2) pcnt /= tt / TIME_STEP_SEC;
			if(pcnt > 65534) pcnt = 65534;
		}
		fram_store.ByMin.PowerCnt -= pcnt;
		#if DEBUGSOO > 2
			os_printf("NPCur: %u ", pcnt);
		#endif
	}
	uint32 addr = ArrayOfCntsStart + fram_store.ByMin.PtrCurrent;
	if(spi_flash_read_max(addr & ~3, (uint32_t *)&arrcnt, 4) != SPI_FLASH_RESULT_OK) return;
	arrcnt.u16[(addr & 3) / ArrayOfCntsElement] = pcnt;
	if(spi_flash_write(addr & ~3, (uint32 *)&arrcnt, 4) != SPI_FLASH_RESULT_OK) return;
	fram_store.ByMin.PtrCurrent += ArrayOfCntsElement;
	if(fram_store.ByMin.PtrCurrent >= ArrayOfCntsSize) fram_store.ByMin.PtrCurrent = 0;
	addr = ArrayOfCntsStart + fram_store.ByMin.PtrCurrent;
	if((addr & (flashchip_sector_size - 1)) == 0) {
		// new sector begins, need space for end marker - 0xFF
		if(spi_flash_erase_sector(addr / flashchip_sector_size) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError erase: %Xh\n", addr);
			#endif
	   		return;
		}
	}
	fram_store.ByMin.TotalCnt += pcnt;
	fram_store.ByMin.LastTime += TIME_STEP_SEC;  // seconds
	LastCnt = pcnt;
	#if DEBUGSOO > 2
		os_printf("NPCur: +%u = %u, %u", pcnt, fram_store.ByMin.TotalCnt, fram_store.ByMin.LastTime);
	#endif
}

void ICACHE_FLASH_ATTR update_cnt_timer_func(void) // repeat every 1 sec
{
	if(!(pwmt_cur.Time && (pwmt_cur.Time >= fram_store.ByMin.LastTime + TIME_STEP_SEC)) || FRAM_Status) return; // dont passed 1 min
	if(wifi_station_get_connect_status() == STATION_GOT_IP && !flg_open_all_service) {// some problems with wifi st here
		wifi_station_disconnect();
		wifi_set_opmode_current(WIFI_DISABLED);
		#ifdef DEBUG_TO_RAM
			dbg_printf("WiFiR %u\n", dbg_next_time());
		#endif
		wifi_set_opmode_current(wificonfig.b.mode);
		wifi_station_connect();
	}
	#if DEBUGSOO > 3
		os_printf("Start %u\n", system_get_time());
	#endif
	if(FRAM_Status) {
		if(FRAM_Status == 1) {
			FRAM_Store_Init();
			if(FRAM_Status) return;
		} else {
			#if DEBUGSOO > 4
				os_printf("FRAM Reinit\n");
			#endif
			fram_init();
		}
	}
	uint32 total = pwmt_cur.Total[0] + pwmt_cur.Total[1] + pwmt_cur.Total[2];
	uint32 pulses = (total - fram_store.ByMin.TotalCnt); // * cfg_glo.PulsesPer0_01KWt / 10;
	fram_store.ByMin.PowerCnt += pulses;
	//fram_store.ByMin.PreviousTotalW += pulses; // * 10 / cfg_glo.PulsesPer0_01KWt;
	#if DEBUGSOO > 3
		os_printf("New cnt: %u; TCnt=%u, PCur=%u, LT:%u-%u=%u\n", fram_store.ByMin.PowerCnt, fram_store.ByMin.TotalCnt, fram_store.ByMin.PtrCurrent, pwmt_cur.Time, fram_store.ByMin.LastTime, pwmt_cur.Time - fram_store.ByMin.LastTime);
		os_printf(" Total: %u", total);
		os_printf("ByDay: PCur=%u, LDay=%u\n", fram_store.PtrCurrentByDay, fram_store.LastDay);
	#endif
	uint16 maxloop = 400;
	do {
		update_cnts(pwmt_cur.Time);
	} while((pwmt_cur.Time >= fram_store.ByMin.LastTime + TIME_STEP_SEC) && maxloop--);

	uint16 save_size = sizeof(fram_store.ByMin);
	// By day array
	if(pwmt_arch.DayLastRead && pwmt_arch.DayLastRead > fram_store.LastDay) { // new day
		pwmt_time_was_corrected_today = 0;
		ArrayByDayElement fram_bydays_elem; // TAll, T1
		save_size = sizeof(fram_store);
		#if DEBUGSOO > 4
			os_printf("New day: %u != %u\n", fram_store.LastDay, pwmt_arch.DayLastRead);
		#endif
		if(pwmt_arch.DayLastRead - fram_store.LastDay > 365) { // Init - days more than 1 year
			fram_store.LastDay = pwmt_arch.DayLastRead - 1;
		}
		if(pwmt_arch.DayLastRead - fram_store.LastDay == 1) {
			fram_bydays_elem[0] = pwmt_arch.Yesterday;
			fram_bydays_elem[1] = pwmt_arch.Yesterday_T1;
			fram_store.LastTotal = total - pwmt_arch.Today;
			fram_store.LastTotal_T1 = pwmt_cur.Total_T1[0] + pwmt_cur.Total_T1[1] + pwmt_cur.Total_T1[2] - pwmt_arch.Today_T1;
		} else { // passed more than 1 day
			fram_bydays_elem[0] = 0; fram_bydays_elem[1] = 0;
		}
		fram_store.LastDay++;
		uint32 addr = ArrayByDayStart + fram_store.PtrCurrentByDay;
		if(spi_flash_write(addr, (uint32 *)fram_bydays_elem, sizeof(ArrayByDayElement)) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError write %Xh\n", addr);
			#endif
			return;
		}
		#if DEBUGSOO > 4
			os_printf("Stored(%u): %u, %u. ", fram_store.PtrCurrentByDay, fram_bydays_elem[0], fram_bydays_elem[1]);
		#endif
		fram_store.PtrCurrentByDay += sizeof(ArrayByDayElement);
		if(fram_store.PtrCurrentByDay >= ArrayByDaySize) fram_store.PtrCurrentByDay = 0;
		addr = ArrayByDayStart + fram_store.PtrCurrentByDay;
		if((addr & (flashchip_sector_size - 1)) == 0) {
			// new sector begins, need space for end marker - 0xFFFFFFFF
			if(spi_flash_erase_sector(addr / flashchip_sector_size) != SPI_FLASH_RESULT_OK) {
				#if DEBUGSOO > 4
					os_printf("\nError erase %Xh\n", addr);
				#endif
		   		return;
			}
		}
	}
	if(eeprom_write_block(0, (uint8 *)&fram_store, save_size)) {
		#if DEBUGSOO > 4
	   		os_printf("EW dAr %u\n", save_size);
		#endif
   		dbg_printf("EW dAr %u\n", save_size);
		FRAM_Status = 2;
		return;
	}
	#if DEBUGSOO > 4
		os_printf("End %u\n", system_get_time());
	#endif

	FRAM_Status = 0;
}

void ICACHE_FLASH_ATTR FRAM_Store_Init(void)
{
	fram_init();
	// restore workspace from FRAM
	if(eeprom_read_block(0, (uint8 *)&fram_store, sizeof(fram_store))) {
		#if DEBUGSOO > 4
			os_printf("ER f_s\n");
		#endif
		return;
	}
	if(fram_store.ByMin.LastTime == 0xFFFFFFFF) { // new memory or error
		os_memset(&fram_store, 0, sizeof(fram_store));
#if DEBUGSOO == 0
		return;
#else
		// clear FRAM
		if(eeprom_write_block(0, (uint8 *)&fram_store, sizeof(fram_store))) {
			#if DEBUGSOO > 2
				os_printf("EW init f_s\n");
			#endif
			return;
		}
#endif
	}
	FRAM_Status = 0;
	#if DEBUGSOO > 4
		struct tm tm;
		_localtime(&fram_store.ByMin.LastTime, &tm);
		os_printf("PCnt= %u, TCnt= %u, Ptr= %u, LTime= %u (", fram_store.ByMin.PowerCnt, fram_store.ByMin.TotalCnt, fram_store.ByMin.PtrCurrent, fram_store.ByMin.LastTime);
		os_printf("%04d-%02d-%02d %02d:%02d:%02d)\n", 1900+tm.tm_year, 1+tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
	#endif
}

void ICACHE_FLASH_ATTR user_initialize(uint8 index)
{
	if(index & 1) {
		Debug_RAM_addr = NULL;
		if(flash_read_cfg(&cfg_glo, ID_CFG_METER, sizeof(cfg_glo)) <= 0) {
			// defaults
			os_memset(&cfg_glo, 0, sizeof(cfg_glo));
			cfg_glo.Fram_Size = sizeof(fram_store); // 32768
			cfg_glo.fram_freq = 400;
			cfg_glo.csv_delimiter = ',';
			cfg_glo.csv_delimiter_dec = '.';
			cfg_glo.csv_delimiter_total = ';';
			cfg_glo.csv_delimiter_total_dec = ',';
			//cfg_glo.PulsesPer0_01KWt = 10; // 1000 per KWt
			cfg_glo.request_period = 30;
			cfg_glo.page_refresh_time = 30000;
			cfg_glo.TimeMaxMismatch = 0; //5; // sec
			cfg_glo.TimeT1Start = 700;
			cfg_glo.TimeT1End = 2300;
			cfg_glo.pwmt_read_timeout = 5000; // us
			cfg_glo.pwmt_response_timeout = 150000; // us
			cfg_glo.pwmt_delay_after_err = 100000000; // us
			uint8 j,i;
			for(j = 0; j < sizeof(cfg_glo.Pass) / sizeof(cfg_glo.Pass[0]); j++)
				for(i = 0; i < sizeof(cfg_glo.Pass[0]); i++) cfg_glo.Pass[j][i] = j+1; // A:.....2
		}
		ArrayOfCntsSize = spi_flash_real_size() - ArrayOfCntsStart;
		iot_data_first = NULL;
		LastCnt = 0;
		LastCnt_Previous = -1;
		sntp_time_adjust = cfg_glo.TimeAdjust;
		Web_ChartMaxDays = 31;
		Web_ChMD = 3;
		Web_ShowBy = 0;
		#if DEBUGSOO > 3
			os_printf("FSize=%u, CntSize=%u\n", cfg_glo.Fram_Size, ArrayOfCntsSize);
		#endif

//		ets_isr_mask(1 << ETS_GPIO_INUM); // запрет прерываний GPIOs
//		// setup interrupt and os_task
//		uint32 pins_mask = (1<<SENSOR_PIN);
//		gpio_output_set(0,0,0, pins_mask); // настроить GPIOx на ввод
//		//GPIO_ENABLE_W1TC = pins_mask; // GPIO OUTPUT DISABLE отключить вывод в портах
//		set_gpiox_mux_func_ioport(SENSOR_PIN); // установить функцию GPIOx в режим порта i/o
//		SET_PIN_PULLUP_DIS(SENSOR_PIN);
//		ets_isr_attach(ETS_GPIO_INUM, gpio_int_handler, NULL);
//		gpio_pin_intr_state_set(SENSOR_PIN, SENSOR_FRONT_EDGE);
//		Sensor_Edge = 0; // Front
//		GPIO_STATUS_W1TC = pins_mask; // разрешить прерывания GPIOs
//		ets_isr_unmask(1 << ETS_GPIO_INUM);
	}
	if(index & 2) {
		ets_timer_disarm(&update_cnt_timer);
		os_timer_setfn(&update_cnt_timer, (os_timer_func_t *)update_cnt_timer_func, NULL);
		ets_timer_arm_new(&update_cnt_timer, 1000, 1, 1); // repeat msec

		FRAM_Store_Init();
		irda_init();
		iot_cloud_init();
	}

}

bool ICACHE_FLASH_ATTR write_power_meter_cfg(void) {
	return flash_save_cfg(&cfg_glo, ID_CFG_METER, sizeof(cfg_glo));
}

// mask: 255 - all, 1 - eeprom, 2 - ArrayByDay, 4 - ArrayOfCnts
void ICACHE_FLASH_ATTR power_meter_clear_all_data(uint8 mask)
{
#if DEBUGSOO > 0
	os_printf("* Clear data %xh!\n", mask);
#endif
	//wifi_set_opmode_current(WIFI_DISABLED);
	//ets_isr_mask(0xFFFFFFFF); // mask all interrupts
	if(mask & 1) {
		os_memset(&fram_store, 0, sizeof(fram_store));
		if(eeprom_write_block(0, (uint8 *)&fram_store, sizeof(fram_store))) {
			#if DEBUGSOO > 0
				os_printf("Error clear FRAM\n");
			#endif
		}
	}
	if(mask & 2) {
		fram_store.LastDay = 0;
		fram_store.LastTotal = 0;
		fram_store.LastTotal_T1 = 0;
		fram_store.PtrCurrentByDay = 0;
		if(spi_flash_erase_sector(ArrayByDayStart / flashchip_sector_size) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError erase: %Xh\n", ArrayOfCntsStart);
			#endif
		}
		if(spi_flash_erase_sector((ArrayByDayStart + ArrayByDaySize) / flashchip_sector_size - 1) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError erase2: %Xh\n", ArrayOfCntsStart + ArrayByDaySize - 1);
			#endif
		}
	}
	if(mask & 4) {
		os_memset(&fram_store.ByMin, 0, sizeof(fram_store.ByMin));
		if(spi_flash_erase_sector(ArrayOfCntsStart / flashchip_sector_size) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError erase3: %Xh\n", ArrayOfCntsStart);
			#endif
		}
		if(spi_flash_erase_sector((ArrayOfCntsStart + ArrayOfCntsSize) / flashchip_sector_size - 1) != SPI_FLASH_RESULT_OK) {
			#if DEBUGSOO > 4
				os_printf("\nError erase4: %Xh\n", ArrayOfCntsStart + ArrayOfCntsSize - 1);
			#endif
		}
	}
#if DEBUGSOO > 0
	os_printf("\nClear ok!\n");
#endif
}

#else
void ICACHE_FLASH_ATTR user_initialize(uint8 index) {}
void ICACHE_FLASH_ATTR FRAM_Store_Init(void) {}
#endif // BUILD_FOR_OTA_512k
