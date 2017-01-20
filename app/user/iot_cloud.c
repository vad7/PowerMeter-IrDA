/*
 * Send data to IoT cloud, like thingspeak.com
 *
 * Setup:
 * protect/iot_cloud.ini:
 * iot_server=<имя http сервера>\n
 * iot_add_n=<параметры для GET запроса №1>\n
 * iot_add_n=<параметры для GET запроса №2>\n
 * iot_add_n=<параметры для GET запроса №3>\n
 * ...
 *
 * переменные обрамляются "~",
 * n - минимальный интервал между запросами в мили-сек, 0 - без ожидания
 * если при парсинге установлен SCB_RETRYCB, то пропускаем запрос
 * Итоговый запрос: http://<имя сервера><параметры>
 *
 * Written by vad7
 */

#include "user_config.h"
#include "bios.h"
#include "sdk/add_func.h"
#include "c_types.h"
#include "osapi.h"
#include "user_interface.h"
#include "lwip/tcp.h"
#include "lwip/dns.h"
#include "tcp_srv_conn.h"
#include "web_utils.h"
#include "wifi_events.h"
#include "webfs.h"
#include "web_utils.h"
#include "web_fs_init.h"
#include "../web/include/web_srv_int.h"
#include "power_meter.h"
#include "iot_cloud.h"

uint8 iot_cloud_ini[] = "protect/iot_cloud.ini";
const uint8 iot_get_request_tpl[] = "GET %s HTTP/1.0\r\nHost: %s\r\nAccept: text/html\r\n\r\n";
const uint8 key_http_ok1[] = "HTTP/"; // "1.1"
const uint8 key_http_ok2[] = " 200 OK\r\n";
char  iot_last_status[16] = "not runned";
time_t iot_last_status_time = 0;

os_timer_t error_timer;
ip_addr_t tc_remote_ip;
TCP_SERV_CFG * tc_servcfg;

int tc_init_flg; // внутренние флаги инициализации

#define TC_INITED 		(1<<0)
#define TC_RUNNING		(1<<1)
#define mMIN(a, b)  ((a<b)?a:b)

void tc_go_next(void);

//-------------------------------------------------------------------------------
// run_error_timer
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR run_error_timer(uint32 tsec)
{
	ets_timer_disarm(&error_timer);
	ets_timer_setfn(&error_timer, (os_timer_func_t *)tc_go_next, NULL);
	ets_timer_arm_new(&error_timer, tsec*1000, 0, 1); // таймер на x секунд
}
//-------------------------------------------------------------------------------
// tc_close
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR tc_close(void)
{
	if(tc_servcfg != NULL) {
		tcpsrv_close(tc_servcfg);
		tc_servcfg = NULL;
	}
}
//-------------------------------------------------------------------------------
// TCP sent_cb
//-------------------------------------------------------------------------------
/*
err_t ICACHE_FLASH_ATTR tc_sent_cb(TCP_SERV_CONN *ts_conn) {
#if DEBUGSOO > 1
	tcpsrv_sent_callback_default(ts_conn);
#endif
	tc_sconn = ts_conn;
	return ERR_OK;
}
*/
//-------------------------------------------------------------------------------
// TCP receive response from server
//-------------------------------------------------------------------------------
err_t ICACHE_FLASH_ATTR tc_recv(TCP_SERV_CONN *ts_conn) {
#if DEBUGSOO > 1
	tcpsrv_received_data_default(ts_conn);
#endif
	tcpsrv_unrecved_win(ts_conn);
    uint8 *pstr = ts_conn->pbufi;
    sint32 len = ts_conn->sizei;
#if DEBUGSOO > 4
    os_printf("IOT_Rec(%u): %s\n", len, pstr);
#endif
    os_memset(iot_last_status, 0, sizeof(iot_last_status));
    os_strncpy(iot_last_status, (char *)pstr, mMIN(sizeof(iot_last_status)-1, len)); // status/error
    iot_last_status_time = get_sntp_time();
	if(len >= sizeof(key_http_ok1) + 3 + sizeof(key_http_ok2)) {
		if(os_memcmp(pstr, key_http_ok1, sizeof(key_http_ok1)-1) == 0
				&& os_memcmp(pstr + sizeof(key_http_ok1)-1 + 3, key_http_ok2, sizeof(key_http_ok2)-1) == 0) { // Check - 200 OK?
			#if DEBUGSOO > 4
				os_printf(" - 200\n");
			#endif
	        uint8 *nstr = web_strnstr(pstr, "\r\n\r\n", len); // find body
	        if(nstr != NULL) {
	        	pstr = nstr + 4; // body start
	        	len -= nstr - pstr; // body size
		        uint8 *nstr = web_strnstr(pstr, "\r\n", len); // find next delimiter
		        if(nstr != NULL) *nstr = '\0';
	        	if(ahextoul(pstr)) { // not 0 = OK
	    			tc_init_flg &= ~TC_RUNNING; // clear run flag
	    			iot_data_processing->last_run = system_get_time();
					#if DEBUGSOO > 4
						os_printf("Ok!!!\n");
					#endif
	        	}
	        }
		}
	}
	//ts_conn->flag.rx_null = 1; // stop receiving data
   	tc_close();
	return ERR_OK;
}

// replace ~x~ in buf with calculated web_int_callback(x) and put buf to web_conn.msgbuf
void tc_parse_buf(TCP_SERV_CONN *ts_conn, uint8 *buf, int32_t len)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
	while(len > 0) {
		int32_t cmp = web_find_cbs(buf, len);
		if(cmp >= 0) { // callback (~) has been found
			int32 cmp2 = web_find_cbs(buf + cmp + 1, len - cmp - 1); // find closing '~'
			uint8 cbs_not_closed = (cmp2 < 1) * 2;
			if(web_conn->msgbuflen + cmp + cbs_not_closed > web_conn->msgbufsize) break; // overflow
			os_memcpy(&web_conn->msgbuf[web_conn->msgbuflen], buf, cmp + cbs_not_closed);
			web_conn->msgbuflen += cmp + cbs_not_closed;
			buf += cmp + 1 + cbs_not_closed;
			len -= cmp + 1 + cbs_not_closed;
			if(!cbs_not_closed) { // parse
				uint8 c = buf[cmp2];
				buf[cmp2] = '\0';
				web_int_callback(ts_conn, buf);
				buf[cmp2] = c;
				buf += cmp2 + 1; // skip closing '~'
				len -= cmp2 + 1;
			}
		} else {
			if(web_conn->msgbuflen + len > web_conn->msgbufsize) break; // overflow
			os_memcpy(&web_conn->msgbuf[web_conn->msgbuflen], buf, len);
			web_conn->msgbuflen += len;
			break;
		}
	}
}

//-------------------------------------------------------------------------------
// TCP listen, put GET request to the server
//-------------------------------------------------------------------------------
err_t ICACHE_FLASH_ATTR tc_listen(TCP_SERV_CONN *ts_conn) {
	uint16 len = 0;
	uint8 *buf = NULL;
	struct buf_fini *p = NULL;
	if(iot_data_processing != NULL) {
		p = web_fini_init(1); // prepare empty buffer, filled with 0
		if(p == NULL) return 1;
		tc_parse_buf(&p->ts_conn, iot_data_processing->iot_request, os_strlen(iot_data_processing->iot_request));
		if(p->web_conn.webflag & SCB_USER) { // cancel send
			#if DEBUGSOO > 4
				os_printf("iot-skip!\n");
			#endif
			tc_close();
			os_free(p);
			return 2;
		}
		buf = p->web_conn.msgbuf;
		len = p->web_conn.msgbuflen;
	}
#if DEBUGSOO > 4
	tcpsrv_print_remote_info(ts_conn);
	os_printf("tc_listen, send(%d): %s\n", len, buf);
#endif
	err_t err = tcpsrv_int_sent_data(ts_conn, buf, len);
	os_free(p);
	return err;
}

//-------------------------------------------------------------------------------
// TCP disconnect
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR tc_disconnect(TCP_SERV_CONN *ts_conn) {
#if DEBUGSOO > 1
	tcpsrv_disconnect_calback_default(ts_conn);
#endif
}
//-------------------------------------------------------------------------------
// tc_start
//-------------------------------------------------------------------------------
err_t ICACHE_FLASH_ATTR tc_init(void)
{
	err_t err = ERR_USE;
	tc_close();

	TCP_SERV_CFG * p = tcpsrv_init_client3();  // tcpsrv_init(3)
	if (p != NULL) {
		// изменим конфиг на наше усмотрение:
		p->max_conn = 3; // =0 - вечная попытка соединения
		p->flag.rx_buf = 1; // прием в буфер с его автосозданием.
		p->flag.nagle_disabled = 1; // отмена nagle
//		p->time_wait_rec = tc_twrec; // по умолчанию 5 секунд
//		p->time_wait_cls = tc_twcls; // по умолчанию 5 секунд
#if DEBUGSOO > 4
		os_printf("TC: Max retry connection %d, time waits %d & %d, min heap size %d\n",
					p->max_conn, p->time_wait_rec, p->time_wait_cls, p->min_heap);
#endif
		p->func_discon_cb = tc_disconnect;
		p->func_listen = tc_listen;
		p->func_sent_cb = NULL;
		p->func_recv = tc_recv;
		err = ERR_OK;
	}
	tc_servcfg = p;
	return err;
}
//-------------------------------------------------------------------------------
// dns_found_callback
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR tc_dns_found_callback(uint8 *name, ip_addr_t *ipaddr, void *callback_arg)
{
#if DEBUGSOO > 4
	os_printf("clb:%s, " IPSTR " ", name, IP2STR(ipaddr));
#endif
	if(tc_servcfg != NULL) {
		if(ipaddr != NULL && ipaddr->addr != 0) {
			tc_remote_ip = *ipaddr;
			err_t err = tcpsrv_client_start(tc_servcfg, tc_remote_ip.addr, DEFAULT_TC_HOST_PORT);
			if (err != ERR_OK) {
#if DEBUGSOO > 4
				os_printf("goerr=%d ", err);
#endif
				tc_close();
			}
		}
	}
}
//-------------------------------------------------------------------------------
// close_dns_found
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR close_dns_finding(void){
	ets_timer_disarm(&error_timer);
	if(tc_init_flg & TC_RUNNING) { // ожидание dns_found_callback() ?
		// убить вызов  tc_dns_found_callback()
		int i;
		for (i = 0; i < DNS_TABLE_SIZE; ++i) {
			if(dns_table[i].state != DNS_STATE_DONE && dns_table[i].found == (dns_found_callback)tc_dns_found_callback) {
				/* flush this entry */
				dns_table[i].found = NULL;
				dns_table[i].state = DNS_STATE_UNUSED;
				#if DEBUGSOO > 4
					os_printf("DNS unused: %s\n", dns_table[i].name);
				#endif
			}
		}
		tc_init_flg &= ~TC_RUNNING;
	}
	tc_close();
}
//-------------------------------------------------------------------------------
// TCP client start
//-------------------------------------------------------------------------------
err_t ICACHE_FLASH_ATTR tc_go(void)
{
	err_t err = ERR_USE;
	if((tc_init_flg & TC_RUNNING) || iot_data_processing == NULL) return err; // выход, если процесс запущен или нечего запускать
	#if DEBUGSOO > 4
		os_printf("Run: %x, %u\n", iot_data_processing, iot_data_processing->min_interval);
	#endif
	err = tc_init(); // инициализация TCP
	if(err == ERR_OK) {
		tc_init_flg |= TC_RUNNING; // процесс запущен
		run_error_timer(TCP_REQUEST_TIMEOUT); // обработать ошибки и продолжение

#if DEBUGSOO > 4
		int i;
		for (i = 0; i < DNS_TABLE_SIZE; ++i) {
			os_printf("TDNS%d: %d, %s, " IPSTR " (%d)\n", i, dns_table[i].state, dns_table[i].name, IP2STR(&dns_table[i].ipaddr), dns_table[i].ttl);
		}
#endif

		err = dns_gethostbyname(iot_server_name, &tc_remote_ip, (dns_found_callback)tc_dns_found_callback, NULL);
#if DEBUGSOO > 4
		os_printf("dns_gethostbyname(%s)=%d ", iot_server_name, err);
#endif
		if(err == ERR_OK) {	// Адрес разрешен из кэша или локальной таблицы
			err = tcpsrv_client_start(tc_servcfg, tc_remote_ip.addr, DEFAULT_TC_HOST_PORT);
		} else if(err == ERR_INPROGRESS) { // Запущен процесс разрешения имени с внешнего DNS
			err = ERR_OK;
		}
		if (err != ERR_OK) {
			tc_init_flg &= ~TC_RUNNING; // процесс не запущен
//				tc_close();
		}
	}
	return err;
}
// next iot_data connection
void tc_go_next(void)
{
	#if DEBUGSOO > 4
		os_printf("iot_go_next(%d): %u: %x %x\n", tc_init_flg, system_get_time(), iot_data_first, iot_data_processing);
	#endif
	if(tc_init_flg & TC_RUNNING) { // Process timeout
		close_dns_finding();
		tc_init_flg &= ~TC_RUNNING; // clear
		if(iot_data_processing != NULL) iot_data_processing = iot_data_processing->next;
	}
	// next
	while(iot_data_processing != NULL) {
		if(system_get_time() - iot_data_processing->last_run > iot_data_processing->min_interval) { // если рано - пропускаем
			if(tc_go() == ERR_OK) break;
		}
		iot_data_processing = iot_data_processing->next;
	}
	if(iot_data_processing == NULL) ets_timer_disarm(&error_timer); // stop timer
}

// return 0 - ok
uint8_t ICACHE_FLASH_ATTR iot_cloud_init(void)
{
	uint8_t retval;
	if(tc_init_flg) { // already init - restart
		close_dns_finding();
		tc_init_flg = 0;
	}
	if(iot_data_first != NULL) { // data exist - clear
		iot_data_clear();
	}
	if(!cfg_glo.iot_cloud_enable) return 0; // iot cloud disabled
	retval = web_fini(iot_cloud_ini);
	if(retval == 0) tc_init_flg |= TC_INITED;
#if DEBUGSOO > 4
		os_printf("iot_init: %d\n", tc_init_flg);
#endif
	return retval;
}

void ICACHE_FLASH_ATTR iot_data_clear(void)
{
	if(iot_data_first != NULL) {
		IOT_DATA *next, *iot = iot_data_first;
		do {
			next = iot->next;
			os_free(iot);
		} while((iot = next) != NULL);
		os_free(iot_server_name);
		iot_data_first = NULL;
		iot_data_processing = NULL;
		iot_server_name = NULL;
	}
}

// 1 - start, 0 - end
void ICACHE_FLASH_ATTR iot_cloud_send(uint8 fwork)
{
	if(wifi_station_get_connect_status() != STATION_GOT_IP) return; // st connected?
	if(!flg_open_all_service) {// some problem with WiFi here
		wifi_station_connect();
		#ifdef DEBUG_TO_RAM
			dbg_printf("WiFi reconnect\n");
		#endif
	}
	if(!cfg_glo.iot_cloud_enable) return; // iot cloud disabled
	#if DEBUGSOO > 4
		os_printf("iot_send: %d, %d: %x %x, IP%d(%d)\n", tc_init_flg, fwork, iot_data_first, iot_data_processing, wifi_station_get_connect_status(), flg_open_all_service);
	#endif
	if((tc_init_flg & TC_INITED) == 0) { //
		if(iot_cloud_init()) return; // Not inited - reinit
	}
	if(fwork == 0) { // end
		close_dns_finding();
		return;
	}
	if((tc_init_flg & TC_RUNNING)) return; // exit if process active
	if(iot_data_first != NULL) {
		if(iot_data_processing == NULL) iot_data_processing = iot_data_first;
		tc_go_next();
	}
}
