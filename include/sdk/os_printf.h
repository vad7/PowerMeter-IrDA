/******************************************************************************
 * FileName: os_printf.h
 * Description: Alternate SDK (libmain.a)
 * Author: PV`
 * (c) PV` 2015
*******************************************************************************/

#ifndef _INCLUDE_OS_PRINTF_H_
#define _INCLUDE_OS_PRINTF_H_


int __wrap_os_printf_plus(const char *format, ...);
//#define rom_printf __wrap_os_printf_plus
//int rom_printf(const char *format, ...) ICACHE_FLASH_ATTR;
void _sprintf_out(char c);
int ets_sprintf(char *str, const char *format, ...);

#endif /* _INCLUDE_OS_PRINTF_H_ */
