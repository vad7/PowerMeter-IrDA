
#include "web_srv.h"

#define CRLF "\r\n"
#define FINI_BUF_SIZE 512

struct buf_fini
{
	TCP_SERV_CONN ts_conn;
	WEB_SRV_CONN web_conn;
	uint8 buf[FINI_BUF_SIZE+1];
};

struct buf_fini * web_fini_init(uint8 init_msgbuf) ICACHE_FLASH_ATTR;
uint8 web_fini(const uint8 * fname) ICACHE_FLASH_ATTR;
