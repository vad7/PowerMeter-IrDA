/*
 * Debug to RAM
 * Written by vad7@yahoo.com
 *
*/

#include "debug_ram.h"
#include "bios/ets.h"
#include "sdk/mem_manager.h"
#include "sdk/rom2ram.h"
#include "sdk/app_main.h"
#include "tcp_srv_conn.h"
#include "web_srv.h"

uint32 Debug_RAM_size = 4096;
uint8 *Debug_RAM_addr = NULL;
uint32 Debug_RAM_len = 0;
uint8  Debug_level = 0;
//uint8  Save_system_set_os_print = 0xFF;

void dbg_printf_out(char c)
{
	if(Debug_RAM_addr != NULL && Debug_RAM_len < Debug_RAM_size) {
		Debug_RAM_addr[Debug_RAM_len++] = c;
	}
}
// Write debug info to RAM buffer
void dbg_printf(const char *format, ...) {
	if(Debug_RAM_addr == NULL) return;
	va_list args;
	va_start(args, format);
	ets_vprintf(dbg_printf_out, ((uint32)format >> 30)? rom_strcpy(print_mem_buf, (void *)format, sizeof(print_mem_buf)-1) : format, args);
	va_end(args);
}
// Start/Stop debug, Set buffer size if size > 0
// 0 - stop, 1 - start tiny: dbg_printf(), 2 - start all: dbg_printf() + os_printf()
void ICACHE_FLASH_ATTR dbg_set(uint8 level, uint32 size)
{
//	if(Save_system_set_os_print == 0xFF) Save_system_set_os_print = system_get_os_print();
	if(level == 0) {
		if(Debug_RAM_addr != NULL) {
			os_free(Debug_RAM_addr);
			Debug_RAM_addr = NULL;
			Debug_RAM_len = 0;
//			system_set_os_print(Save_system_set_os_print);
		}
		if(size) Debug_RAM_size = size;
	} else {
		if(Debug_RAM_addr == NULL) Debug_RAM_addr = os_malloc(Debug_RAM_size);
		if(Debug_RAM_addr != NULL) {
			os_memset(Debug_RAM_addr, 0, Debug_RAM_size);
			Debug_RAM_len = 0;
			if(level == 2) {
				ets_install_putc1(dbg_printf_out);
				system_set_os_print(1);
//			} else {
//				system_set_os_print(Save_system_set_os_print);
			}
		}
	}
	Debug_level = level;
}

// Send text file from RAM.
// web_conn->udata_start - start pos
void ICACHE_FLASH_ATTR dbg_tcp_send(void * ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)((TCP_SERV_CONN *)ts_conn)->linkd;
	if(Debug_RAM_addr == NULL) return;
	if(CheckSCB(SCB_RETRYCB)==0) { // first run
		web_conn->udata_start = 0;
	}
	uint32 len = web_conn->msgbufsize - web_conn->msgbuflen;
	if(len > Debug_RAM_len - web_conn->udata_start) len = Debug_RAM_len - web_conn->udata_start;
	os_memcpy(web_conn->msgbuf + web_conn->msgbuflen, Debug_RAM_addr + web_conn->udata_start, len);
	web_conn->msgbuflen += len;
	if(len < Debug_RAM_len - web_conn->udata_start) {
		web_conn->udata_start += len;
		SetSCB(SCB_RETRYCB);
		SetNextFunSCB((web_func_cb)dbg_tcp_send);
		return;
	}
	ClrSCB(SCB_RETRYCB);
}
