/*
 * Power Meter
 *
 * Count pulses from meter.
 *
 * Written by vad7
 */

#include "user_config.h"
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "user_interface.h"
#include "web_iohw.h"
#include "flash_eep.h"
#include "webfs.h"
#include "sdk/libmain.h"
#include "driver/eeprom.h"
#include "hw/gpio_register.h"
#include "wifi_events.h"
#include "power_meter.h"
#include "iot_cloud.h"

#define GPIO_Tasks 		1
#define GPIO_Int_Signal 1
ETSEvent GPIO_TaskQueue[GPIO_Tasks] DATA_IRAM_ATTR;

uint32 PowerCnt = 0;
uint32 PowerCntTime = 0;
uint8  FRAM_Status = 1; 	// 0 - Ok, 1 - Not initialized, 2 - Error
uint8  Sensor_Edge; 		// 0 - Front pulse edge, 1 - Back
uint8  FRAM_STORE_Readed	= 0;
uint8  user_idle_func_working = 0;

void ICACHE_FLASH_ATTR fram_init(void) {
	eeprom_init(cfg_glo.fram_freq);
}

void NextPtrCurrent(uint8 cnt)
{
	fram_store.PtrCurrent += cnt;
	if(fram_store.PtrCurrent >= cfg_glo.Fram_Size - StartArrayOfCnts)
		fram_store.PtrCurrent -= cfg_glo.Fram_Size - StartArrayOfCnts;
}

void ICACHE_FLASH_ATTR update_cnts(time_t time) // 1 minute passed
{
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
	uint32 SaveCurrentPtr = fram_store.PtrCurrent;
	CNT_CURRENT SaveCntCurrent = CntCurrent;
	uint8 to_save = 2; // bytes
	uint8 to_add = 0;  // bytes after write
	uint32 pcnt = 0;
	if(time - fram_store.LastTime > 60 * 60 * 24 * 30) { // run: first time or after a long time (30 days)
		#if DEBUGSOO > 2
			os_printf("%u - %u > 30 days\n", time, fram_store.LastTime);
		#endif
		fram_store.LastTime = time;
		*(uint32 *)&CntCurrent = 0;
		SaveCntCurrent = CntCurrent;
		to_save = 4;
	} else {
		//ets_intr_lock();
		pcnt = fram_store.PowerCnt;
		if(pcnt > 1) {
			time_t tt = time - fram_store.LastTime;
			if(tt >= TIME_STEP_SEC * 2) pcnt /= tt / TIME_STEP_SEC;
			if(pcnt > 255) pcnt = 255;
		}
		fram_store.PowerCnt -= pcnt;
		//ets_intr_unlock();
		#if DEBUGSOO > 2
			os_printf("NPCur: %u\n", pcnt);
		#endif
	}
	if(pcnt == 0) { // new zero
		if(CntCurrent.Cnt1) { // Cnt1 filled, something going wrong
			#if DEBUGSOO > 2
				os_printf("Suddenly Cnt1 = %d, %u\n", time, fram_store.LastTime);
			#else
				dbg_printf("Cnt1=%d>0", CntCurrent.Cnt1);
			#endif
			*(uint32 *)&CntCurrent = 0;
			SaveCntCurrent = CntCurrent;
			to_save = 4;
		}
		if(CntCurrent.Cnt2 == 255) { // overflow
			// next packed
			NextPtrCurrent(2);
			*(uint32 *)&CntCurrent = 0;
			CntCurrent.Cnt2 = 1;
		} else {
			CntCurrent.Cnt2++;
		}
	} else {
		if(CntCurrent.Cnt2) { // current packed
			if(CntCurrent.Cnt2 == 1) { // if current 0,1 - write 0,n
				*(uint32 *)&CntCurrent = 0;
				CntCurrent.Cnt2 = pcnt;
				to_save = 4;
				to_add = 2;
			} else {
				NextPtrCurrent(2);
				*(uint32 *)&CntCurrent = 0;
				CntCurrent.Cnt1 = pcnt;
				to_save = 3;
				to_add = 1;
			}
		} else { // 0,0
			CntCurrent.Cnt1 = pcnt;
			to_save = 3;
			to_add = 1;
		}
	}
	if(to_save) {
		NextPtrCurrent(0); // set ptr = 0 if ptr garbage
		uint32 cnt = cfg_glo.Fram_Size - (StartArrayOfCnts + fram_store.PtrCurrent);
		if(cnt > to_save) cnt = to_save;
		if(eeprom_write_block(StartArrayOfCnts + fram_store.PtrCurrent, (uint8 *)&CntCurrent, cnt)) {
			#if DEBUGSOO > 4
		   		os_printf("EW curr\n");
			#else
		   		dbg_printf("EW c\n");
			#endif
		   	goto xError;
		}
		if(cnt < to_save) { // overflow
			if(eeprom_write_block(StartArrayOfCnts, ((uint8 *)&CntCurrent) + cnt, to_save - cnt)) {
				#if DEBUGSOO > 4
			   		os_printf("EW curr2\n");
				#else
			   		dbg_printf("EW c2\n");
				#endif
			   	goto xError;
			}
		}
	}
	NextPtrCurrent(to_add);
	if(to_add) *(uint32 *)&CntCurrent = 0; // clear for next
	fram_store.TotalCnt += pcnt;
	fram_store.LastTime += TIME_STEP_SEC;  // seconds
	uint8_t now_T1 = 0;
	if(cfg_glo.TimeT1Start || cfg_glo.TimeT1End) { // Multi tariffs
		now_T1 = check_add_CntT1(&fram_store.LastTime, &fram_store.TotalCntT1, pcnt);
	}
	WDT_FEED = WDT_FEED_MAGIC; // WDT
	if(eeprom_write_block(0, (uint8 *)&fram_store, sizeof(fram_store))) {
		#if DEBUGSOO > 4
	   		os_printf("EW f_s\n");
		#else
	   		dbg_printf("EW f_s\n");
	   	#endif
   		fram_store.TotalCnt -= pcnt;
   		if(now_T1) fram_store.TotalCntT1 -= pcnt;
   		fram_store.LastTime -= TIME_STEP_SEC;  // seconds
xError:
		CntCurrent = SaveCntCurrent;
		fram_store.PtrCurrent = SaveCurrentPtr;
		fram_store.PowerCnt += pcnt;
		FRAM_Status = 2;
	   	return;
	}
	FRAM_Status = 0;
	LastCnt = pcnt;
	iot_cloud_send(1);
}

uint8_t ICACHE_FLASH_ATTR check_add_CntT1(time_t *LastTime, uint32 *CntT1, uint32 add)
{
	struct tm tm;
	_localtime(LastTime, &tm);
	uint16 st, end, tt = tm.tm_hour * 60 + tm.tm_min; // min
	st = (cfg_glo.TimeT1Start / 100) * 60 + cfg_glo.TimeT1Start % 100 + 1; 	// +1 - to correct for inclusive value
	end = (cfg_glo.TimeT1End / 100) * 60 + cfg_glo.TimeT1End % 100; 		// not included
	if((end > st && tt >= st && tt <= end) || (end < st && (tt >= st || tt <= end))) {
		*CntT1 += add;
		return 1;
	}
	return 0;
}

#if DEBUGSOO > 4
uint8 print_i2c_page = 0;
#endif

void user_idle(void) // idle function for ets_run
// ICACHE_FLASH_ATTR
{
	//FRAM_speed_test();
	time_t time = get_sntp_localtime();
	if(time && (time - fram_store.LastTime >= TIME_STEP_SEC)) { // Passed 1 min
		ets_set_idle_cb(NULL, NULL);
		user_idle_func_working = 1;
		ets_intr_unlock();
		update_cnts(time);
		ets_set_idle_cb(user_idle, NULL);
		user_idle_func_working = 0;
		if(wifi_station_get_connect_status() == STATION_GOT_IP && !flg_open_all_service) {// some problem with WiFi here
			dbg_printf("WiFi trouble\n");
			wifi_station_connect();
		}
	}
	if(Sensor_Edge && system_get_time() - PowerCntTime > 500000) { // us
		// some problem here, after few sec - normal state of edge = 0
		#if DEBUGSOO > 4
			os_printf("RESET EDGE\n");
		#endif
		dbg_printf("Rst-edge\n");
		gpio_pin_intr_state_set(SENSOR_PIN, SENSOR_FRONT_EDGE);
		Sensor_Edge = 0;
	}
#if DEBUGSOO > 4
	else if(print_i2c_page) { // 1..n
		ets_set_idle_cb(NULL, NULL);
		ets_intr_unlock();
		#define READSIZE 64
		uint8 *buf = os_malloc(READSIZE);
		if(buf == NULL) {
			os_printf("Error malloc %d bytes", READSIZE);
		} else {
			uint16 pos = (print_i2c_page-1) * READSIZE;
			os_printf("Read fram: %6d", pos);
			uint32 mt = system_get_time();
			if(eeprom_read_block(pos, buf, READSIZE) == 0) {
				os_printf(" - Error!");
				goto x1;
			}
			mt = system_get_time() - mt;
			os_printf(", time: %6d us: ", mt);
			for(pos = 0; pos < READSIZE; pos++) {
				os_printf("%02x ", buf[pos]);
			}
x1:			os_printf("\n");
			if(++print_i2c_page > cfg_glo.Fram_Size / READSIZE) print_i2c_page = 0;
			os_free(buf);
		}
		ets_set_idle_cb(user_idle, NULL);
	}
#endif

}

void GPIO_Task_NewData(os_event_t *e)
{
    switch(e->sig) {
    	case GPIO_Int_Signal:
    		if(e->par == SENSOR_PIN) { // new data
#if DEBUGSOO > 2
   				os_printf("*T: %u, n=%d\n", PowerCntTime, PowerCnt);
#endif
   				// update current PowerCnt
   				if(FRAM_Status == 1) {
   					FRAM_Store_Init();
   					if(FRAM_Status) goto xEnd;
   				} else if(FRAM_Status == 2){
xRepeat:   			fram_init();
   				}
   				ets_intr_lock();
   				fram_store.PowerCnt += PowerCnt;
   				PowerCnt = 0;
   				ets_intr_unlock();
   				dbg_printf("pcnt=%u\n",fram_store.PowerCnt);
  				WDT_FEED = WDT_FEED_MAGIC; // WDT
   				if(eeprom_write_block(0 + (uint8 *)&fram_store.PowerCnt - (uint8 *)&fram_store, (uint8 *)&fram_store.PowerCnt, sizeof(fram_store.PowerCnt))) {
					#if DEBUGSOO > 4
  						os_printf("EW PrCnt %d\n", FRAM_Status);
					#else
  						dbg_printf("EW pcnt %d\n", FRAM_Status);
					#endif
   					if(FRAM_Status == 0) {
   	   					FRAM_Status = 2;
   						goto xRepeat;
   					}
   				} else {
   					FRAM_Status = 0;
   				}
xEnd: 			if(user_idle_func_working == 0 && ets_idle_cb == NULL) {
   	   				ets_set_idle_cb(user_idle, NULL); // sometimes idle func may be reset
					#if DEBUGSOO > 2
						os_printf("* user_idle was reseted, re-arm!\n");
					#endif
   				}
    		}
    		break;
    }
}

// Do not use ICACHE* attr here!
static void gpio_int_handler(void)
{
	uint32 gpio_status = GPIO_STATUS;
	GPIO_STATUS_W1TC = gpio_status;
	if(gpio_status & (1<<SENSOR_PIN)) {
		uint32 tm = system_get_time();
#if DEBUGSOO > 4
		__wrap_os_printf_plus("*%u,%d*\n", tm, Sensor_Edge);
#endif
		dbg_printf("*%d* %u", Sensor_Edge, tm - PowerCntTime);
		if(tm - PowerCntTime > cfg_glo.Debouncing_Timeout) { // skip if interval less than x us
			PowerCntTime = tm;
			if(!Sensor_Edge) { // Front edge
				PowerCnt++;
				dbg_printf(" =%u", PowerCnt);
				if(FRAM_Status) dbg_printf(" (%d)", FRAM_Status);
				system_os_post(SENSOR_TASK_PRIO, GPIO_Int_Signal, SENSOR_PIN);
			}
		    Sensor_Edge ^= 1; // next edge
		}
	    gpio_pin_intr_state_set(SENSOR_PIN, Sensor_Edge ? SENSOR_BACK_EDGE : SENSOR_FRONT_EDGE);
	    dbg_printf("\n");
	}
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
	if(fram_store.LastTime == 0xFFFFFFFF) { // new memory or error
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
		// write begin marker - 0,0
		if(eeprom_write_block(cfg_glo.Fram_Size - 2, (uint8 *)&fram_store, 4)) {
			#if DEBUGSOO > 2
				os_printf("EW init f_s\n");
			#endif
			return;
		}
#endif
	} else if(fram_store.LastTime) { // LastTime must be filled to read CntCurrent
		uint8 cnt = cfg_glo.Fram_Size - (StartArrayOfCnts + fram_store.PtrCurrent);
		if(cnt > 2) cnt = 2;
		if(eeprom_read_block(StartArrayOfCnts + fram_store.PtrCurrent, (uint8 *)&CntCurrent, cnt)) {
			#if DEBUGSOO > 4
				os_printf("ER curr\n");
			#endif
			return;
		} else if(cnt < 2) {
			if(eeprom_read_block(StartArrayOfCnts, &CntCurrent.Cnt2, 1)) {
				#if DEBUGSOO > 4
					os_printf("ER curr2\n");
				#endif
				return;
			}
		}
	}
	FRAM_Status = 0;
	iot_cloud_init();
	#if DEBUGSOO > 4
		struct tm tm;
		_localtime(&fram_store.LastTime, &tm);
		os_printf("PCnt= %u, TCnt= %u, Ptr= %u, LTime= %u (", fram_store.PowerCnt, fram_store.TotalCnt, fram_store.PtrCurrent, fram_store.LastTime);
		os_printf("%04d-%02d-%02d %02d:%02d:%02d)\n", 1900+tm.tm_year, 1+tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
		uint8 *buf = os_malloc(20);
		if(buf != NULL) {
			uint8 cnt = fram_store.PtrCurrent < 20 ? fram_store.PtrCurrent : 20;
			if(fram_store.PtrCurrent) {
				if(eeprom_read_block(StartArrayOfCnts + fram_store.PtrCurrent - cnt, buf, cnt)) {
					os_printf("ER 20\n");
				} else {
					uint8 i;
					for(i = 0; i < cnt; i++) {
						os_printf("%d ", buf[i]);
					}
					os_printf("\n");
				}
			}
			os_free(buf);
		}
		os_printf("Cnt1= %u, Cnt2= %u\n", CntCurrent.Cnt1, CntCurrent.Cnt2);
	#endif
}

void ICACHE_FLASH_ATTR user_initialize(uint8 index)
{
	if(index & 1) {
		Debug_RAM_addr = NULL;
		if(flash_read_cfg(&cfg_glo, ID_CFG_METER, sizeof(cfg_glo)) <= 0) {
			// defaults
			os_memset(&cfg_glo, 0, sizeof(cfg_glo));
			cfg_glo.Fram_Size = FRAM_SIZE_DEFAULT;
			cfg_glo.PulsesPer0_01KWt = DEFAULT_PULSES_PER_0_01_KWT;
			cfg_glo.csv_delimiter = ',';
			cfg_glo.fram_freq = 400;
			cfg_glo.Debouncing_Timeout = 1000; // us
		}
		if(cfg_glo.ReverseSensorPulse) {
			SENSOR_FRONT_EDGE = GPIO_PIN_INTR_POSEDGE;
			SENSOR_BACK_EDGE  = GPIO_PIN_INTR_NEGEDGE;
		} else {
			SENSOR_FRONT_EDGE = GPIO_PIN_INTR_NEGEDGE;
			SENSOR_BACK_EDGE  = GPIO_PIN_INTR_POSEDGE;
		}
		#if DEBUGSOO > 3
			os_printf("FSize=%u, Pulses=%u, ", cfg_glo.Fram_Size, cfg_glo.PulsesPer0_01KWt);
		#endif
		*(uint32 *)&CntCurrent = 0;
		iot_data_first = NULL;
		LastCnt = 0;
		LastCnt_Previous = -1;
		sntp_time_adjust = cfg_glo.TimeAdjust;

		ets_isr_mask(1 << ETS_GPIO_INUM); // запрет прерываний GPIOs
		// setup interrupt and os_task
		uint32 pins_mask = (1<<SENSOR_PIN);
		gpio_output_set(0,0,0, pins_mask); // настроить GPIOx на ввод
		//GPIO_ENABLE_W1TC = pins_mask; // GPIO OUTPUT DISABLE отключить вывод в портах
		set_gpiox_mux_func_ioport(SENSOR_PIN); // установить функцию GPIOx в режим порта i/o
		SET_PIN_PULLUP_DIS(SENSOR_PIN);
		ets_isr_attach(ETS_GPIO_INUM, gpio_int_handler, NULL);
		gpio_pin_intr_state_set(SENSOR_PIN, SENSOR_FRONT_EDGE);
		Sensor_Edge = 0; // Front
		GPIO_STATUS_W1TC = pins_mask; // разрешить прерывания GPIOs
		ets_isr_unmask(1 << ETS_GPIO_INUM);
	}
	if(index & 2) {
		system_os_task(GPIO_Task_NewData, SENSOR_TASK_PRIO, GPIO_TaskQueue, GPIO_Tasks);
		FRAM_Store_Init();
		#if DEBUGSOO > 4
			os_printf("PCnt = %d\n", PowerCnt);
			uint8 p3 = GPIO_INPUT_GET(GPIO_ID_PIN(3));
			os_printf("Systime: %d, io3=%x\n", system_get_time(), p3);
			if(p3 == 0) {
				uart_wait_tx_fifo_empty();
				#define blocklen 64
				uint8 *buf = os_zalloc(blocklen);
				if(buf == NULL) {
					os_printf("Err malloc!\n");
				} else {
					wifi_set_opmode_current(WIFI_DISABLED);
					ets_isr_mask(0xFFFFFFFF); // mask all interrupts
//					uint32 mt = system_get_time();
//					power_meter_clear_all_data();
//					mt = system_get_time() - mt;
//					os_printf("Clear time: %d us\n", mt);
					//i2c_init();
					WDT_FEED = WDT_FEED_MAGIC; // WDT
					uint16 i, j;
					for(i = 0; i < cfg_glo.Fram_Size / blocklen; i++) {
		//				if(eeprom_write_block(i * blocklen, buf, blocklen) == 0) {
		//					os_printf("EW Bl: %d\n", i);
		//					break;
		//				}
		//				WDT_FEED = WDT_FEED_MAGIC; // WDT
		//				break;
		//				continue;
						uint32 mt = system_get_time();
						if(eeprom_read_block(i * blocklen, buf, blocklen)) {
							os_printf("ER Bl: %d\n", i);
							break;
						}
						mt = system_get_time() - mt;
						WDT_FEED = WDT_FEED_MAGIC; // WDT
						os_printf("%3d: ", i);
						for(j = 0; j < blocklen; j++) {
							os_printf("%02x ", buf[j]);
							WDT_FEED = WDT_FEED_MAGIC; // WDT
						}
						os_printf(" = time: %d\n", mt);
					}
					//os_free(buf);
					os_printf("\nHalt\n");
					while(1) WDT_FEED = WDT_FEED_MAGIC; // halt
				}
			}
		#endif
	}

//	os_printf("-------- read cfg  ------------\n");
//	char buf[50];
//	uint32 faddr = FMEMORY_SCFG_BASE_ADDR;
//	fobj_head fobj;
//	do {
//		if(flash_read(faddr, &fobj, fobj_head_size)) break;
//		if(fobj.x == fobj_x_free && fobj.n.size > MAX_FOBJ_SIZE) break;
//		if(flash_read(faddr + fobj_head_size, buf, align(fobj.n.size))) break;
//		buf[fobj.n.size] = 0;
//		os_printf("Read id: %d, len: %d = %s\n", fobj.n.id, fobj.n.size, buf);
//		faddr += align(fobj.n.size + fobj_head_size);
//	} while(faddr < FMEMORY_SCFG_BASE_ADDR + FMEMORY_SCFG_BANK_SIZE - align(fobj_head_size));
//	os_printf("--------- read cfg END --------\n");
}

bool ICACHE_FLASH_ATTR write_power_meter_cfg(void) {
	return flash_save_cfg(&cfg_glo, ID_CFG_METER, sizeof(cfg_glo));
}

void ICACHE_FLASH_ATTR power_meter_clear_all_data(void)
{
	#define buflen (2 + StartArrayOfCnts + sizeof(CNT_CURRENT))
#if DEBUGSOO > 0
	os_printf("* Clear all FRAM data!\n");
#endif
	//wifi_set_opmode_current(WIFI_DISABLED);
	//ets_isr_mask(0xFFFFFFFF); // mask all interrupts
	uint8 *buf = os_zalloc(buflen);
	if(buf != NULL) {
		WDT_FEED = WDT_FEED_MAGIC; // WDT
		if(eeprom_write_block(cfg_glo.Fram_Size - 2, buf, buflen)) { // wrap from end to 0
			#if DEBUGSOO > 0
				os_printf("Error i2c write\n");
			#endif
		}
		os_free(buf);
		os_memset(&fram_store, 0, sizeof(fram_store));
		*(uint32 *)&CntCurrent = 0;
	}
}

/*
void ICACHE_FLASH_ATTR FRAM_speed_test(void)
{
	#define eblen 1024
	#define Test_KB 32

	uint8 *buf = os_malloc(eblen);
	if(buf == NULL) {
		os_printf("Error malloc some bytes!\n");
	} else {
		os_printf("Test %dKB FRAM\n", Test_KB);
		uart_wait_tx_fifo_empty();
		wifi_set_opmode_current(WIFI_DISABLED);
		ets_isr_mask(0xFFFFFFFF); // mask all interrupts
		i2c_Init(0);
		//os_memset(buf, 0, eblen);
		WDT_FEED = WDT_FEED_MAGIC; // WDT
		uint16 i;
		uint32 mt = system_get_time();
		for(i = 0; i < Test_KB; i++) {
			if(eeprom_read_block(i * eblen, buf, eblen)) {
				os_printf("Error read block: %d\n", i);
				break;
			}
			WDT_FEED = WDT_FEED_MAGIC; // WDT
		}
		mt = system_get_time() - mt;
		os_printf("Reading time: %d us\n", mt);
//		for(i = 0; i < eblen; i++) {
//			os_printf("%x ", buf[i]);
//		}
//		os_printf("\n");
		mt = system_get_time(); //get_mac_time();
		uint32 pos = 0x2000 + (mt & 0xFFF);
		spi_flash_read(pos, buf, eblen);
		for(i = 0; i < Test_KB; i++) {
			if(eeprom_write_block(i * eblen, buf, eblen)) {
				os_printf("Error write block: %d\n", i);
				break;
			}
			WDT_FEED = WDT_FEED_MAGIC; // WDT
		}
		mt = system_get_time() - mt; //get_mac_time();
		os_printf("Write time: %d us\n", mt);
		uint8 *buf2 = os_malloc(eblen);
		if(buf2 == NULL) {
			os_printf("Error malloc some bytes! 2\n");
		} else {
			mt = system_get_time();
			uint8 eq = 0;
			for(i = 0; i < Test_KB; i++) {
				if(eeprom_read_block(i * eblen, buf2, eblen)) {
					os_printf("Error read block: %d\n", i);
					break;
				}
				WDT_FEED = WDT_FEED_MAGIC; // WDT
				eq = os_memcmp(buf, buf2, eblen) == 0;
				if(!eq) break;
			}
			mt = system_get_time() - mt;
			os_printf("Compare: %d us - %s\n", mt, eq ? "ok!" : "not equal!");
			uart_wait_tx_fifo_empty();
			os_free(buf2);
		}
		os_free(buf);
		while(1) WDT_FEED = WDT_FEED_MAGIC; // WDT
	}
}
*/
