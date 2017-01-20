/*
 * Debug to RAM
 * Written by vad7@yahoo.com
 *
*/

#include "c_types.h"

#define DEBUG_RAM_BUF_MAX	32768
extern uint32 Debug_RAM_size;
extern uint8 *Debug_RAM_addr;
extern uint32 Debug_RAM_len;
extern uint8  Debug_level;
extern char print_mem_buf[1024];
void dbg_printf_out(char c);
void dbg_printf(const char *format, ...);
void dbg_set(uint8 level, uint32 size) ICACHE_FLASH_ATTR;
void dbg_tcp_send(void * ts_conn) ICACHE_FLASH_ATTR;
