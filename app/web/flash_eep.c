/*
 * flash_eep.c
 *
 *  Created on: 19/01/2015
 *      Author: PV`
 *
 * Добавлено vad7
 * Режим 1 сектора (4096) - пишется в один сектор по кругу, когда закончилось место - упаковка в памяти
 * Описание :
 * В начале сегмента код (uint32) - чем меньше, тем свежее сегмент
 * Код сегмента уменьшается на 1 во время записи данных нового сегмента
 * Данные идут после заголовка (ИД, размер), в сегменте могут быть несколько записей с одинаковым ИД,
 * свежие данные - последние.
 * Если заголовок данных = fobj_x_free (0xFFFFFFFF) - конец конфигурации в сегменте
 * Записываемые данные добавляются в конец на свободное место, пока оно не закончится, тогда
 * копируем только свежие данные в новый сегмент, код которого наибольший
 * Максимальный размер данных MAX_FOBJ_SIZE (512) байт
 *
 */

#include "user_config.h"
#include "bios/ets.h"
#include "sdk/mem_manager.h"
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "sdk/flash.h"
#include "flash_eep.h"
#include "hw/esp8266.h"
//#ifdef USE_WEB
#include "web_srv.h"
//#endif
#ifdef USE_TCP2UART
#include "tcp2uart.h"
#endif
#ifdef USE_NETBIOS
#include "netbios.h"
#endif
#ifdef USE_MODBUS
#include "modbustcp.h"
#endif

//-----------------------------------------------------------------------------
#define mMIN(a, b)  ((a<b)?a:b)

#if FMEMORY_SCFG_BANKS == 1 // only 1 sector
#define get_addr_bscfg(f) FMEMORY_SCFG_BASE_ADDR
#define bank_head_size 0
#else
#define bank_head_size 4
//-----------------------------------------------------------------------------
// FunctionName : get_addr_bscfg
// Опции:
//  fasle - поиск текушего сегмента
//  true - поиск нового сегмента для записи (pack)
// Returns     : новый адрес сегмента для записи
// Поиск нового сегмента - с самым большим кодом
// При первом чтении на пустой памяти - инициализация кода = 0xfffffffe
//-----------------------------------------------------------------------------
LOCAL ICACHE_FLASH_ATTR uint32 get_addr_bscfg(bool flg)
{
	uint32 x1 = (flg)? 0 : 0xFFFFFFFF, x2;
	uint32 faddr = FMEMORY_SCFG_BASE_ADDR;
	uint32 reta = FMEMORY_SCFG_BASE_ADDR;
	do {
		if(flash_read(faddr, &x2, 4)) return 1;
		if(flg) { // поиск нового сегмента для записи (pack)
			if(x2 > x1) {
				x1 = x2;
				reta = faddr; // новый адрес сегмента для записи
			}
		} else if(x2 < x1) { // поиск текушего сегмента
			x1 = x2;
			reta = faddr; // новый адрес сегмента для записи
		};
		faddr += FMEMORY_SCFG_BANK_SIZE;
	} while(faddr < (FMEMORY_SCFG_BASE_ADDR + FMEMORY_SCFG_BANKS * FMEMORY_SCFG_BANK_SIZE));
	if((!flg)&&(x1 == 0xFFFFFFFF)&&(reta == FMEMORY_SCFG_BASE_ADDR)) {
		x1--;
		if(flash_write(reta, &x1, 4)) return 1;
	}
#if DEBUGSOO > 3
		if(flg) os_printf("bsseg:%p ", reta);
		else os_printf("brseg:%p ", reta);
#endif
	return reta;
}
#endif
//-----------------------------------------------------------------------------
// FunctionName : get_addr_fobj
// Опции:
//  false - Поиск последней записи объекта по id и size
//  true - Поиск присутствия записи объекта по id и size
// Returns : адрес записи данных объекта
// 0 - не найден
//-----------------------------------------------------------------------------
LOCAL ICACHE_FLASH_ATTR uint32 get_addr_fobj(uint32 base, fobj_head *obj, bool flg)
{
//	if(base == 0) return 0;
	fobj_head fobj;
	uint32 faddr = base + bank_head_size;
	uint32 fend = base + FMEMORY_SCFG_BANK_SIZE - align(fobj_head_size);
	uint32 reta = 0;
	do {
		if(flash_read(faddr, &fobj, fobj_head_size)) return 1;
		if(fobj.x == fobj_x_free) break;
		if(fobj.n.size <= MAX_FOBJ_SIZE) {
			if(fobj.n.id == obj->n.id) {
				if(flg) {
					return faddr;
				}
				obj->n.size = fobj.n.size;
				reta = faddr;
			}
			faddr += align(fobj.n.size + fobj_head_size);
		}
		else faddr += align(MAX_FOBJ_SIZE + fobj_head_size);
	}
	while(faddr < fend);
	return reta;
}
//-----------------------------------------------------------------------------
// FunctionName : get_addr_fend
// Поиск последнего пустого места в сегменте для записи объекта
// Returns : адрес для записи объекта
//-----------------------------------------------------------------------------
LOCAL ICACHE_FLASH_ATTR uint32 get_addr_fobj_save(uint32 base, fobj_head obj)
{
	fobj_head fobj;
	uint32 faddr = base + bank_head_size;
	uint32 fend = faddr + FMEMORY_SCFG_BANK_SIZE - align(obj.n.size + fobj_head_size);
	do {
		if(flash_read(faddr, &fobj, fobj_head_size)) return 1; // ошибка
		if(fobj.x == fobj_x_free) {
			return faddr; // нашли
		}
		if(fobj.n.size <= MAX_FOBJ_SIZE) {
			faddr += align(fobj.n.size + fobj_head_size);
		}
		else faddr += align(MAX_FOBJ_SIZE + fobj_head_size);
	} while(faddr < fend);
	return 0; // не влезет, на pack
}
//=============================================================================
// FunctionName : pack_cfg_fmem
// Перенос данных в новый сегмент, уменьшение кода нового сегмента, объект добавляется в конец
// Returns      : адрес заголовка объекта или 0 - если ошибка или нет места
//-----------------------------------------------------------------------------
LOCAL ICACHE_FLASH_ATTR uint32 pack_cfg_fmem(fobj_head obj)
{
	fobj_head fobj;
#if FMEMORY_SCFG_BANKS == 1 // only 1 sector
	uint8 *buffer = os_malloc(FMEMORY_SCFG_BANK_SIZE);
	if(buffer == NULL) return 0;
	os_memset(buffer, 0xFF, FMEMORY_SCFG_BANK_SIZE);
	uint32 baseseg = get_addr_bscfg(false);
	uint32 faddr = baseseg;
	uint16 len = 0;
	do {
		if(flash_read(faddr, &fobj, fobj_head_size)) goto xError; // последовательное чтение id из старого сегмента
		if(fobj.x == fobj_x_free || fobj.n.size > MAX_FOBJ_SIZE) break; // конец или мусор в cfg
		faddr += align(fobj.n.size + fobj_head_size);
		if(fobj.n.id != obj.n.id) { // кроме нового
			// найдем, сохранили ли мы его уже?
			uint8 *chkaddr = buffer;
			while(chkaddr < buffer + FMEMORY_SCFG_BANK_SIZE && ((fobj_head *)chkaddr)->x != fobj_x_free) {
				if(((fobj_head *)chkaddr)->n.id == fobj.n.id) {
					chkaddr = NULL;
					break;
				}
				chkaddr += align(((fobj_head *)chkaddr)->n.size + fobj_head_size);
			}
			if(chkaddr != NULL && chkaddr < buffer + FMEMORY_SCFG_BANK_SIZE) { // не сохранили
				uint32 xaddr = get_addr_fobj(baseseg, &fobj, false); // найдем последнее сохранение объекта в старом сегменте
				if(xaddr < 4) goto xError; // ???
				if(flash_read(xaddr, chkaddr, align(fobj.n.size + fobj_head_size))) goto xError; // прочитаем заголовок с телом объекта в конец буфера
				len += align(fobj.n.size + fobj_head_size);
			}
		}
	} while(faddr < (baseseg + FMEMORY_SCFG_BANK_SIZE - align(fobj_head_size)));
	faddr = baseseg + len;
	if(faddr > baseseg + FMEMORY_SCFG_BANK_SIZE) goto xError;
	if(flash_erase_sector(baseseg)) goto xError;
	if(flash_write(baseseg, buffer, len)) {
xError:
		os_free(buffer);
		return 0;
	}
	os_free(buffer);
	return faddr;
#else
	uint8 buf[align(MAX_FOBJ_SIZE + fobj_head_size)];
	uint32 fnewseg = get_addr_bscfg(true); // поиск нового сегмента для записи (pack)
	if(fnewseg < 4) return fnewseg; // error
	uint32 foldseg = get_addr_bscfg(false); // поиск текушего сегмента
	if(foldseg < 4) return fnewseg; // error
	uint32 faddr = foldseg;
	uint32 xaddr;
	uint16 len;
	if(flash_erase_sector(fnewseg)) return 1;
	faddr += bank_head_size;
	do {
		if(flash_read(faddr, &fobj, fobj_head_size)) return 1; // последовательное чтение id из старого сегмента
		if(fobj.x == fobj_x_free) break;
		if(fobj.n.size > MAX_FOBJ_SIZE) len = align(MAX_FOBJ_SIZE + fobj_head_size);
		else len = align(fobj.n.size + fobj_head_size);
		if(fobj.n.id != obj.n.id &&  fobj.n.size <= MAX_FOBJ_SIZE) { // объект валидный
			if(get_addr_fobj(fnewseg, &fobj, true) == 0) { // найдем, сохранили ли мы его уже? нет
				xaddr = get_addr_fobj(foldseg, &fobj, false); // найдем последнее сохранение объекта в старом сенгменте, size изменен
				if(xaddr < 4) return xaddr; // ???
				// прочитаем заголовок с телом объекта в буфер
				if(flash_read(xaddr, buf, align(fobj_head_size + fobj.n.size))) return 1;
				xaddr = get_addr_fobj_save(fnewseg, fobj); // адрес для записи объекта
				if(xaddr < 4) return xaddr; // ???
				// запишем заголовок с телом объекта во flash
				if(flash_write(xaddr, buf, align(fobj.n.size + fobj_head_size))) return 1;
			};
		};
		faddr += len;
	} while(faddr  < (foldseg + FMEMORY_SCFG_BANK_SIZE - align(fobj_head_size+1)));

	if(flash_read(foldseg, &xaddr, 4))	return 1;
	xaddr--;
	if(flash_write(fnewseg, &xaddr, 4))	return 1;
	return get_addr_fobj_save(fnewseg, obj); // адрес для записи объекта;
#endif
}
//=============================================================================
//- Сохранить объект в flash --------------------------------------------------
//  Дописывает объект в конец в пустое место в сегменте,
//  если нет места - переносим данные и пишем в новый сегмент
//  Returns	: true - ok, false - error
//-----------------------------------------------------------------------------
bool ICACHE_FLASH_ATTR flash_save_cfg(void *ptr, uint16 id, uint16 size)
{
#if FMEMORY_SCFG_BANKS == 1 // only 1 sector
	if(size == 0 || size > MAX_FOBJ_SIZE) return false;
	uint8 *buf = os_malloc(align(size + fobj_head_size));
	if(buf == NULL) return false;
	bool retval = true;
	fobj_head fobj;
	fobj.n.id = id;
	fobj.n.size = size;

	uint32 faddr = get_addr_bscfg(false);
	uint32 xfaddr = get_addr_fobj(faddr, &fobj, false);
	if(xfaddr > 4 && size == fobj.n.size) {
		if(flash_read(xfaddr, buf, align(size) + fobj_head_size) != SPI_FLASH_RESULT_OK) retval = false;
		else if(os_memcmp(ptr, buf + fobj_head_size, size) == 0) goto xEnd;	// уже записано то-же самое
	}
	if(retval) {
		fobj.n.id = id;
		fobj.n.size = size;
		#if DEBUGSOO > 2
			os_printf("save-id:%02x[%u] ", id, size);
		#endif
		faddr = get_addr_fobj_save(faddr, fobj);
		if(faddr == 0) {
			faddr = pack_cfg_fmem(fobj);
		}
		if(faddr) {
			if(flash_write(faddr, (uint32 *)&fobj, fobj_head_size) != SPI_FLASH_RESULT_OK) retval = false;
			else if(flash_write(faddr + fobj_head_size, (uint32 *)ptr, align(size)) != SPI_FLASH_RESULT_OK) retval = false;
		} else retval = false;
	}
xEnd:
	os_free(buf);
#if DEBUGSOO > 4
	os_printf(" - Ret %d\n", retval);
#endif
	return retval;
#else
	if(size > MAX_FOBJ_SIZE) return false;
	uint8 buf[align(MAX_FOBJ_SIZE + fobj_head_size)];
	fobj_head fobj;
	fobj.n.id = id;
	fobj.n.size = size;

	uint32 faddr = get_addr_bscfg(false);
	if(faddr < 4) return false;
	{
		uint32 xfaddr = get_addr_fobj(faddr, &fobj, false);
		if(xfaddr > 3 && size == fobj.n.size) {
			if((size)&&(flash_read(xfaddr, buf, align(size + fobj_head_size)))) return false; // error
			if(!os_memcmp(ptr, &buf[fobj_head_size], size)) return true; // уже записано то-же самое
		}
	}
	fobj.n.size = size;
#if DEBUGSOO > 2
	os_printf("save-id:%02x[%u] ", id, size);
#endif
	faddr = get_addr_fobj_save(faddr, fobj);
	if(faddr == 0) {
		faddr = pack_cfg_fmem(fobj);
	}
	if(faddr < 4) return false; // error
	uint16 len = align(size + fobj_head_size);
	os_memcpy(buf, &fobj, fobj_head_size);
	os_memcpy(&buf[fobj_head_size], ptr, size);
	if(flash_write(faddr, (uint32 *)buf, len)) return false; // error
	return true;
#endif
}
//=============================================================================
//- Прочитать объект из flash -------------------------------------------------
//  Параметры:
//   prt - указатель, куда сохранить
//   id - идентификатор искомого объекта
//   maxsize - сколько байт сохранить максимум из найденного объекта, по ptr
//  Returns:
//  -3 - error
//  -2 - flash rd/wr/clr error
//  -1 - не найден
//   0..MAX_FOBJ_SIZE - ok, сохраненный размер объекта
sint16 ICACHE_FLASH_ATTR flash_read_cfg(void *ptr, uint16 id, uint16 maxsize)
{
	if(maxsize > MAX_FOBJ_SIZE) return -3; // error
	fobj_head fobj;
	fobj.n.id = id;
	fobj.n.size = 0;
#if DEBUGSOO > 2
	os_printf("read-id:%02x[%u] ", id, maxsize);
#endif
	uint32 faddr = get_addr_bscfg(false);
	if(faddr < 4) return -faddr-1;
	faddr = get_addr_fobj(faddr, &fobj, false);
	if(faddr < 4) return -faddr-1;
	maxsize = mMIN(fobj.n.size, maxsize);
	if((maxsize)&&(flash_read(faddr + fobj_head_size, ptr, maxsize))) return -2; // error
#if DEBUGSOO > 2
		os_printf("ok,size:%u ", fobj.n.size);
#endif
	return fobj.n.size;
}
//=============================================================================
//-- Сохранение системных настроек -------------------------------------------
//=============================================================================
struct SystemCfg syscfg;
#if defined(USE_TCP2UART) || defined(USE_MODBUS)
uint8 * tcp_client_url;
#endif
//-----------------------------------------------------------------------------
// Чтение системных настроек
//-----------------------------------------------------------------------------
bool ICACHE_FLASH_ATTR sys_read_cfg(void) {
#if defined(USE_TCP2UART) || defined(USE_MODBUS)
	read_tcp_client_url();
#endif
	if(flash_read_cfg(&syscfg, ID_CFG_SYS, sizeof(syscfg)) != sizeof(syscfg)) {
		syscfg.cfg.w = 0
				| SYS_CFG_PIN_CLR_ENA
#ifdef USE_TCP2UART
				| SYS_CFG_T2U_REOPEN
#endif
#ifdef USE_MODBUS
				| SYS_CFG_MDB_REOPEN
#endif
#ifdef USE_CPU_SPEED
				| SYS_CFG_HI_SPEED
#endif
#if DEBUGSOO > 0
				| SYS_CFG_DEBUG_ENA
#endif
#ifdef USE_NETBIOS
	#if USE_NETBIOS
				| SYS_CFG_NETBIOS_ENA
	#endif
#endif
#ifdef USE_SNTP
	#if USE_SNTP
				| SYS_CFG_SNTP_ENA
	#endif
#endif
#ifdef USE_CAPTDNS
	#if USE_CAPTDNS
				| SYS_CFG_CDNS_ENA
	#endif
#endif
				;
#ifdef USE_TCP2UART
		syscfg.tcp2uart_port = DEFAULT_TCP2UART_PORT;
		syscfg.tcp2uart_twrec = 0;
		syscfg.tcp2uart_twcls = 0;
#endif
		syscfg.tcp_client_twait = 5000;
#ifdef USE_WEB
		syscfg.web_port = DEFAULT_WEB_PORT;
		syscfg.web_twrec = 5;
		syscfg.web_twcls = 5;
#endif
#ifdef USE_MODBUS
		syscfg.mdb_twrec = 10;
		syscfg.mdb_twcls = 10;
		syscfg.mdb_port = DEFAULT_MDB_PORT;	// (=0 - отключен)
		syscfg.mdb_id = DEFAULT_MDB_ID;
#endif
#if DEBUGSOO > 4
		os_printf("CfgFlg: %X\n", syscfg.cfg.w);
#endif
		return false;
	};
//	if(syscfg.web_port == 0) syscfg.cfg.b.pin_clear_cfg_enable = 1;
	return true;
}
//-----------------------------------------------------------------------------
// Сохранение системных настроек
//-----------------------------------------------------------------------------
bool ICACHE_FLASH_ATTR sys_write_cfg(void) {
	return flash_save_cfg(&syscfg, ID_CFG_SYS, sizeof(syscfg));
}

#if defined(USE_TCP2UART) || defined(USE_MODBUS)
//-------------------------------------------------------------------------------
// new_tcp_client_url()
//-------------------------------------------------------------------------------
bool ICACHE_FLASH_ATTR new_tcp_client_url(uint8 *url)
{
	if(tcp_client_url != NULL) {
		if (os_strcmp(tcp_client_url, url) == 0) {
			return false;
		}
		os_free(tcp_client_url);
	}
	uint32 len = os_strlen(url);
	if(len < VarNameSize || len != 0) {
		tcp_client_url = os_zalloc(len+1);
		if(tcp_client_url == NULL) return false;
		os_memcpy(tcp_client_url, url, len);
		if(flash_save_cfg(tcp_client_url, ID_CFG_UURL, len)) return true;
		os_free(tcp_client_url);
	}
	tcp_client_url = NULL;
	return false;
}
//-------------------------------------------------------------------------------
// read_tcp_client_url()
//-------------------------------------------------------------------------------
bool ICACHE_FLASH_ATTR read_tcp_client_url(void)
{
	uint8 url[VarNameSize] = {0};
	uint32 len = flash_read_cfg(&url, ID_CFG_UURL, VarNameSize);
	if(len != 0) {
		if(tcp_client_url != NULL) os_free(tcp_client_url);
		uint32 len = os_strlen(url);
		if(len < VarNameSize || len != 0) {
			tcp_client_url = os_zalloc(len+1);
			if(tcp_client_url == NULL) return false;
			os_memcpy(tcp_client_url, url, len);
			if(flash_save_cfg(tcp_client_url, ID_CFG_UURL, len)) return true;
			os_free(tcp_client_url);
		}
	}
	tcp_client_url = NULL;
	return false;
}
#endif

/*
 *  Чтение пользовательских констант (0 < idx < MAX_IDX_USER_CONST)
 */
uint32 ICACHE_FLASH_ATTR read_user_const(uint8 idx) {
#ifdef FIX_SDK_FLASH_SIZE
	uint32 ret = 0xFFFFFFFF;
	if (idx < MAX_IDX_USER_CONST) {
		if (flash_read_cfg(&ret, ID_CFG_KVDD + idx, 4) != 4) {
			if(idx == 0) ret = 83000; // 102400; // константа делителя для ReadVDD
		}
	}
	return ret;
#endif
}
/*
 * Запись пользовательских констант (0 < idx < MAX_IDX_USER_CONST)
 */
bool ICACHE_FLASH_ATTR write_user_const(uint8 idx, uint32 data) {
	if (idx >= MAX_IDX_USER_CONST)	return false;
	uint32 ret = data;
	return flash_save_cfg(&ret, ID_CFG_KVDD + idx, 4);
}

// vad7
// Возвращает размер текущей сохраненной конфигурации в байтах
int32 ICACHE_FLASH_ATTR current_cfg_length(void)
{
	fobj_head fobj;
	uint32 base_addr = get_addr_bscfg(false); // поиск текушего сегмента
	if(base_addr < 4) return -base_addr; // error
	uint32 faddr = (base_addr += bank_head_size);
	do {
		if(flash_read(faddr, &fobj, fobj_head_size)) return -4; // последовательное чтение из сегмента
		if(fobj.x == fobj_x_free) break;
		faddr += align(fobj_head_size + (fobj.n.size > MAX_FOBJ_SIZE ? MAX_FOBJ_SIZE : fobj.n.size));
	} while(faddr <= (base_addr + FMEMORY_SCFG_BANK_SIZE - bank_head_size - align(fobj_head_size+1)));
	return faddr - base_addr;
}
