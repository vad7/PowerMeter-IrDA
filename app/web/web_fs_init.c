/******************************************************************************
 * FileName: web_fs_ini.c
 * Description: The web server start configuration.
*******************************************************************************/
#include "user_config.h"
#ifdef USE_WEB
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "tcp_srv_conn.h"
#include "sdk/rom2ram.h"
#include "sdk/app_main.h"
#include "web_srv_int.h"
#include "web_utils.h"
#include "web_iohw.h"
#include "webfs.h"
#include "web_fs_init.h"

/******************************************************************************
 * find pos \n or \r or \0 if not found return len
*******************************************************************************/
LOCAL uint16 ICACHE_FLASH_ATTR find_crlf(uint8 * chrbuf, uint16 len) {
  int i;
  for(i = 0; i < len; i++) {
	  if(chrbuf[i] == '\n' || chrbuf[i] == '\r' || chrbuf[i] == '\0')  return i;
  }
  return len;
}

// Init buffer with TCP_SERV_CONN, WEB_SRV_CONN and buf[FINI_BUF_SIZE]
struct buf_fini * ICACHE_FLASH_ATTR web_fini_init(uint8 init_msgbuf)
{
	struct buf_fini *p = (struct buf_fini *) os_zalloc(sizeof(struct buf_fini));
	if(p == NULL) {
#if DEBUGSOO > 1
		os_printf("Error mem!\n");
#endif
	} else {
		TCP_SERV_CONN * ts_conn = &p->ts_conn;
		WEB_SRV_CONN * web_conn = &p->web_conn;
		web_conn->bffiles[0] = WEBFS_INVALID_HANDLE;
		web_conn->bffiles[1] = WEBFS_INVALID_HANDLE;
		web_conn->bffiles[2] = WEBFS_INVALID_HANDLE;
		web_conn->bffiles[3] = WEBFS_INVALID_HANDLE;
		ts_conn->linkd = (uint8 *)web_conn;
		ts_conn->sizeo = FINI_BUF_SIZE;
		ts_conn->pbufo = p->buf;
		if(init_msgbuf) {
			web_conn->msgbufsize = ts_conn->sizeo;
			web_conn->msgbuf = ts_conn->pbufo;
			web_conn->msgbuflen = 0;
		}
	}
	return p;
}

/******************************************************************************
*******************************************************************************/
// return 0 - ok
uint8 ICACHE_FLASH_ATTR web_fini(const uint8 * fname)
{
	struct buf_fini *p = web_fini_init(0);
	if(p == NULL) return 1;
	TCP_SERV_CONN * ts_conn = &p->ts_conn;
	WEB_SRV_CONN * web_conn = &p->web_conn;
	rom_strcpy(ts_conn->pbufo, (void *)fname, MAX_FILE_NAME_SIZE);
#if DEBUGSOO > 1
	os_printf("Run ini file: %s\n", ts_conn->pbufo);
#endif
	if(!web_inc_fopen(ts_conn, ts_conn->pbufo)) {
#if DEBUGSOO > 1
		os_printf("file not found!\n");
#endif
		os_free(p);
		return 2;
	}
	if(fatCache.flags & WEBFS_FLAG_ISZIPPED) {
#if DEBUGSOO > 1
		os_printf("\nError: file is ZIPped!\n");
#endif
		web_inc_fclose(web_conn);
		os_free(p);
		return 3;
	}
#if DEBUGSOO > 1
	user_uart_wait_tx_fifo_empty(DEBUG_UART,1000);
#endif
	while(1) {
		web_conn->msgbufsize = ts_conn->sizeo;
		web_conn->msgbuflen = 0;
		uint8 *pstr = web_conn->msgbuf = ts_conn->pbufo;
		if(CheckSCB(SCB_RETRYCB)) { // повторный callback? да
#if DEBUGSOO > 2
			os_printf("rcb ");
#endif
			if(web_conn->func_web_cb != NULL) web_conn->func_web_cb(ts_conn);
			if(CheckSCB(SCB_RETRYCB)) break; // повторить ещё раз? да.
		}
		uint16 len = WEBFSGetArray(web_conn->webfile, pstr, FINI_BUF_SIZE);
#if DEBUGSOO > 3
		os_printf("ReadF[%u]= %u, %u\n", web_conn->webfile, len, WEBFSGetBytesRem(web_conn->webfile));
#endif
		if(len) { // есть байты в файле
			pstr[len] = '\0';
			int sslen = 0;
			while(pstr[sslen] == '\n' || pstr[sslen] == '\r') sslen++;
			if(sslen == 0) {
				int nslen = find_crlf(pstr, len);
				if(nslen != 0) {
					pstr[nslen++] = '\0'; // закрыть string calback-а
					while((nslen < len) && (pstr[nslen] == '\n' || pstr[nslen] == '\r')) nslen++;
#if DEBUGSOO > 3
					os_printf("String:%s\n", pstr);
#endif
					if(!os_memcmp((void*)pstr, "inc:", 4)) { // "inc:file_name"
						if(!web_inc_fopen(ts_conn, &pstr[4])) {
#if DEBUGSOO > 1
							os_printf("file not found!");
#endif
						};
					}
					else web_int_callback(ts_conn, pstr);
				} else break; // \0 found
				sslen = nslen;
			};
			#if DEBUGSOO > 4
				os_printf("len: %u, %u\n", len, sslen);
			#endif
			// откат файла + передвинуть указатель в файле на считанные байты с учетом маркера, без добавки длины для передачи
			WEBFSSeek(web_conn->webfile, len - sslen, WEBFS_SEEK_REWIND);
		}
		else if(web_inc_fclose(web_conn)) {
			if(web_conn->web_disc_cb != NULL) web_conn->web_disc_cb(web_conn->web_disc_par);
			break;
		}
	}
	os_free(p);
	return 0;
}

#endif // USE_WEB
