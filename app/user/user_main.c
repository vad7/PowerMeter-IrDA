/******************************************************************************
 * PV` FileName: user_main.c
 *******************************************************************************/

#include "user_config.h"
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "user_interface.h"
#include "tcp_srv_conn.h"
#include "flash_eep.h"
#include "wifi.h"
#include "hw/spi_register.h"
#include "hw/pin_mux_register.h"
#include "sdk/rom2ram.h"
#include "web_iohw.h"
#include "tcp2uart.h"
#include "webfs.h"
#include "sdk/libmain.h"

#ifdef USE_WEB
#include "web_srv.h"
#endif

#ifdef USE_NETBIOS
#include "netbios.h"
#endif

#ifdef USE_SNTP
#include "sntp.h"
#endif

//#ifdef USE_MODBUS
//#include "modbustcp.h"
#ifdef USE_RS485DRV
#include "driver/rs485drv.h"
#include "mdbtab.h"
#endif
//#endif

#include "power_meter.h"

#ifdef USE_WEB
extern uint8 web_fini(const uint8 * fname);
static const uint8 sysinifname[] ICACHE_RODATA_ATTR = "protect/init.ini";
#endif
#ifdef BUILD_FOR_OTA_512k
#include "wifi_events.h"
	os_timer_t check_wifi_timer DATA_IRAM_ATTR;
void ICACHE_FLASH_ATTR check_wifi_timer_func(void) {
	if(wifi_station_get_connect_status() != STATION_GOT_IP) return; // st connected?
	if(!flg_open_all_service) {// some problem with WiFi here
		wifi_station_disconnect();
		wifi_station_connect();
	}
}
#endif

void ICACHE_FLASH_ATTR init_done_cb(void)
{
#if DEBUGSOO > 0
	os_printf("\nSDK Init - Ok\nHeap size: %d bytes\n", system_get_free_heap_size());
	os_printf("Flash ID: %08x, size: %u, SDK size: %u\n", spi_flash_get_id(), spi_flash_real_size(), flashchip->chip_size);
	os_printf("PERIPHS_IO_MUX = %X\n\n",  READ_PERI_REG(PERIPHS_IO_MUX));
	#ifndef BUILD_FOR_OTA_512k
    os_printf("Curr cfg size: %d b\n", current_cfg_length());
	#endif

	struct ets_store_wifi_hdr whd;
	spi_flash_read(((flashchip->chip_size/flashchip->sector_size)-1)*flashchip->sector_size, &whd, sizeof(whd));
	os_printf("Last sec rw count: %u\n\n", whd.wr_cnt);
#endif
	//
	user_initialize(2); // init FRAM, timer/tasks
	//ets_set_idle_cb(user_idle, NULL); // do not use sleep mode!
	//
#ifdef USE_WEB
	web_fini(sysinifname);
#endif
	switch(system_get_rst_info()->reason) {
	case REASON_SOFT_RESTART:
	case REASON_DEEP_SLEEP_AWAKE:
		break;
	default:
		New_WiFi_config(WIFI_MASK_ALL);
		break;
	}
#ifdef USE_RS485DRV
	rs485_drv_start();
	init_mdbtab();
#endif
#ifdef BUILD_FOR_OTA_512k
	ets_timer_disarm(&check_wifi_timer);
	os_timer_setfn(&check_wifi_timer, (os_timer_func_t *)check_wifi_timer_func, NULL);
	ets_timer_arm_new(&check_wifi_timer, 5000, 1, 1); // repeat msec
#endif
}

extern uint32 _lit4_start[]; // addr start BSS in IRAM
extern uint32 _lit4_end[]; // addr end BSS in IRAM
/******************************************************************************
 * FunctionName : user_init
 * Description  : entry of user application, init user function here
 * Parameters   : none
 * Returns      : none
 *******************************************************************************/
void ICACHE_FLASH_ATTR user_init(void) {
	sys_read_cfg();
#if DEBUGSOO == 0
	#ifdef DEBUG_TO_RAM
		ets_install_putc1(dbg_printf_out);
		if(!syscfg.cfg.b.debug_print_enable) system_set_os_print(0);
	#else
		system_set_os_print(0);
	#endif
#else
	if(!syscfg.cfg.b.debug_print_enable) system_set_os_print(0);
#endif
	//GPIO0_MUX = VAL_MUX_GPIO0_SDK_DEF;
	GPIO4_MUX = VAL_MUX_GPIO4_SDK_DEF;
	GPIO5_MUX = VAL_MUX_GPIO5_SDK_DEF;
	GPIO12_MUX = VAL_MUX_GPIO12_SDK_DEF;
	GPIO13_MUX = VAL_MUX_GPIO13_SDK_DEF;
	GPIO14_MUX = VAL_MUX_GPIO14_SDK_DEF;
	GPIO15_MUX = VAL_MUX_GPIO15_SDK_DEF;
	// vad7
	//power_meter_init();
	//
	//uart_init(); // in tcp2uart.h
	system_timer_reinit();
#if (DEBUGSOO > 0 && defined(USE_WEB))
	os_printf("\nSimple WEB version: " WEB_SVERSION "\n");
#endif
#ifdef USE_GPIO3_AS_CFG_RESET
	if(syscfg.cfg.b.pin_clear_cfg_enable) test_pin_clr_wifi_config(); // сброс настроек, если замкнут пин RX
#endif
	set_cpu_clk(); // select cpu frequency 80 or 160 MHz
#ifdef USE_GDBSTUB
extern void gdbstub_init(void);
	gdbstub_init();
#endif
#if DEBUGSOO > 0
	if(eraminfo.size > 0) os_printf("Found free IRAM: base: %p, size: %d bytes\n", eraminfo.base,  eraminfo.size);
	os_printf("System memory:\n");
    system_print_meminfo();
    os_printf("bssi  : 0x%x ~ 0x%x, len: %d\n", &_lit4_start, &_lit4_end, (uint32)(&_lit4_end) - (uint32)(&_lit4_start));
    os_printf("free  : 0x%x ~ 0x%x, len: %d\n", (uint32)(&_lit4_end), (uint32)(eraminfo.base) + eraminfo.size, (uint32)(eraminfo.base) + eraminfo.size - (uint32)(&_lit4_end));

    os_printf("Start 'heap' size: %d bytes\n", system_get_free_heap_size());
#endif
#if DEBUGSOO > 0
	os_printf("Set CPU CLK: %u MHz\n", ets_get_cpu_frequency());
#endif
	Setup_WiFi();
    WEBFSInit(); // файловая система

	system_deep_sleep_set_option(0);
	system_init_done_cb(init_done_cb);
}

#if DEF_SDK_VERSION >= 2000
/******************************************************************************
 * FunctionName : user_rf_cal_sector_set
 * Description  : SDK just reversed 4 sectors, used for rf init data and paramters.
 *                We add this function to force users to set rf cal sector, since
 *                we don't know which sector is free in user's application.
 *                sector map for last several sectors : ABCCC
 *                A : rf cal
 *                B : rf init data
 *                C : sdk parameters
 * Parameters   : none
 * Returns      : rf cal sector
*******************************************************************************/
uint32 ICACHE_FLASH_ATTR
user_rf_cal_sector_set(void)
{
    return fsec_rf_cal; // всегда 'песочница' для SDK в первых 512k Flash
}
#endif // DEF_SDK_VERSION >= 2000
