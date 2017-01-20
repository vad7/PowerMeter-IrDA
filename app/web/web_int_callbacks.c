/******************************************************************************
 * FileName: web_int_callbacks.c
 * Description: The web server inernal callbacks.
*******************************************************************************/

#include "user_config.h"
#ifdef USE_WEB
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "hw/uart_register.h"
#include "ets_sys.h"
#include "os_type.h"
#include "osapi.h"
#include "user_interface.h"
#include "lwip/tcp.h"
#include "sdk/flash.h"
#include "flash_eep.h"
#include "tcp_srv_conn.h"
#include "web_srv_int.h"
#include "wifi.h"
#include "web_utils.h"
#include "driver/adc.h"
#include "webfs.h"
#include "tcp2uart.h"
#include "web_iohw.h"
#include "sdk/rom2ram.h"
#include "sys_const_utils.h"
#include "wifi_events.h"
#include "driver/spi_register.h"
#include "power_meter.h"
#include "driver/eeprom.h"
#include "time.h"
#include "iot_cloud.h"

#ifdef USE_NETBIOS
#include "netbios.h"
#endif

#ifdef USE_SNTP
#include "sntp.h"
#endif

#ifdef USE_CAPTDNS
#include "captdns.h"
#endif

#ifdef USE_MODBUS
#include "modbustcp.h"
#include "mdbtab.h"
#endif

#ifdef USE_RS485DRV
#include "driver/rs485drv.h"
#endif

#ifdef USE_OVERLAY
#include "overlay.h"
#endif

/*extern uint32 adc_rand_noise;
extern uint32 dpd_bypass_original;
extern uint16 phy_freq_offset
extern uint8 phy_in_most_power;
extern uint8 rtc_mem_check_fail;
extern uint8 phy_set_most_tpw_disbg;
extern uint8 phy_set_most_tpw_index; // uint32? */
extern uint8 phy_in_vdd33_offset;

//extern int rom_atoi(const char *);
#define atoi rom_atoi

#define mMIN(a, b)  ((a<b)?a:b)
//#define ifcmp(a)   if(!os_memcmp((void*)cstr, a , sizeof(a)))
#define ifcmp(a)  if(rom_xstrcmp(cstr, a))

//#define TEST_SEND_WAVE

#ifdef TEST_SEND_WAVE
//-------------------------------------------------------------------------------
// Test adc
// Читает adc в одиночный буфер (~2килобайта) на ~20ksps и сохраняет в виде WAV
// Правильное чтение организуется по прерыванию таймера(!).
// Тут только демо!
//-------------------------------------------------------------------------------
typedef struct
{ // https://ccrma.stanford.edu/courses/422/projects/WaveFormat/
  unsigned long int RIFF ;/* +00 'RIFF'           */
  unsigned long int size8;/* +04 file size - 8    */
  unsigned long int WAVE ;/* +08 'WAVE'           */
  unsigned long int fmt  ;/* +12 'fmt '           */
  unsigned long int fsize;/* +16 указатель до 'fact' или 'data' */
  unsigned short int ccod;/* +20 01 00  Compression code: 1 - PCM/uncompressed */
  unsigned short int mono;/* +22 00 01 или 00 02  */
  unsigned long int freq ;/* +24 частота          */
  unsigned long int bps  ;/* +28                  */
  unsigned short int blka;/* +32 1/2/4  BlockAlign*/
  unsigned short int bits;/* +34 разрядность 8/16 */
  unsigned long int data ;/* +36 'data'           */
  unsigned long int dsize;/* +40 размер данных    */
} WAV_HEADER;
const WAV_HEADER ICACHE_RODATA_ATTR wav_header =
{0x46464952L,
 0x00000008L,
 0x45564157L,
 0x20746d66L,
 0x00000010L,
 0x0001     ,
 0x0001     ,
 0x000055f0L,
 0x000055f0L,
 0x0002     ,
 0x0010     ,
 0x61746164L,
 0x00000000L};
#define WAV_HEADER_SIZE sizeof(wav_header)
//===============================================================================
// web_test_adc()
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR web_test_adc(TCP_SERV_CONN *ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
    unsigned int len = web_conn->msgbufsize - web_conn->msgbuflen;
    if(len > WAV_HEADER_SIZE + 10) {
    	len -= WAV_HEADER_SIZE;
    	WAV_HEADER * ptr = (WAV_HEADER *) &web_conn->msgbuf[web_conn->msgbuflen];
    	os_memcpy(ptr, &wav_header, WAV_HEADER_SIZE);
    	ptr->dsize = len;
    	web_conn->msgbuflen += WAV_HEADER_SIZE;
    	len >>= 1;
    	read_adcs((uint16 *)(web_conn->msgbuf + web_conn->msgbuflen), len, 0x0808);
    	web_conn->msgbuflen += len << 1;
    }
    SetSCB(SCB_FCLOSE | SCB_DISCONNECT); // connection close
}
#endif // TEST_SEND_WAVE

// Send text file until zero byte found.
// web_conn->udata_stop - WEBFS handle
// web_conn->udata_start - start / current pos
void ICACHE_FLASH_ATTR web_callback_textfile(TCP_SERV_CONN *ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
	WEBFS_HANDLE fh = web_conn->udata_stop; // file handle
	if(fh != WEBFS_INVALID_HANDLE) {
//	    if(CheckSCB(SCB_RETRYCB)==0) { // first run
//	    }
		if(WEBFSSeek(fh, web_conn->udata_start, WEBFS_SEEK_START)) {
		    uint32 len = mMIN(web_conn->msgbufsize - web_conn->msgbuflen - 1, WEBFSGetBytesRem(fh)); // +space for '\0'
		    uint8 *cbufpos = web_conn->msgbuf + web_conn->msgbuflen;
			len = WEBFSGetArray(fh, cbufpos, len);
			cbufpos[len] = '\0';
			uint32 zlen = os_strlen(cbufpos);
			#if DEBUGSOO > 2
				os_printf("Send %u+%u ", web_conn->udata_start, zlen);
			#endif
			web_conn->msgbuflen += zlen;
			if(zlen == len && WEBFSGetBytesRem(fh)) { // continue (\0 not found and not file end)
				web_conn->udata_start += len;
				SetSCB(SCB_RETRYCB);
	    		SetNextFunSCB(web_callback_textfile);
	    		return;
			}
		}
		WEBFSClose(fh);
	}
	ClrSCB(SCB_RETRYCB);
//    SetSCB(SCB_FCLOSE | SCB_DISCONNECT);
}

//===============================================================================
// WiFi Saved Aps XML
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR wifi_aps_xml(TCP_SERV_CONN *ts_conn)
{
	struct buf_html_string {
		uint8 ssid[32*6 + 1];
		uint8 psw[64*6 + 1];
	};
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
	struct station_config config[5];
	struct buf_html_string * buf = (struct buf_html_string *)os_malloc(sizeof(struct buf_html_string));
	if(buf == NULL) return;
	int total_aps = wifi_station_get_ap_info(config);
    // Check if this is a first round call
    if(CheckSCB(SCB_RETRYCB)==0) {
    	tcp_puts_fd("<total>%u</total><cur>%u</cur>", total_aps, wifi_station_get_current_ap_id());
    	if(total_aps == 0) return;
    	web_conn->udata_start = 0;
    }
	while(web_conn->msgbuflen + 74 + 32 <= web_conn->msgbufsize) {
	    if(web_conn->udata_start < total_aps) {
	    	struct station_config *p = (struct station_config *)&config[web_conn->udata_start];
	    	if(web_conn->msgbuflen + 74 + htmlcode(buf->ssid, p->ssid, 32*6, 32) + htmlcode(buf->psw, p->password, 64*6, 64) > web_conn->msgbufsize) break;
			tcp_puts_fd("<aps id=\"%u\"><ss>%s</ss><ps>%s</ps><bs>" MACSTR "</bs><bt>%d</bt></aps>",
					web_conn->udata_start, buf->ssid, buf->psw, MAC2STR(p->bssid), p->bssid_set);
	   		web_conn->udata_start++;
	    	if(web_conn->udata_start >= total_aps) {
			    ClrSCB(SCB_RETRYCB);
			    os_free(buf);
			    return;
	    	}
	    }
	    else {
		    ClrSCB(SCB_RETRYCB);
		    os_free(buf);
		    return;
	    }
	}
	// repeat in the next call ...
    SetSCB(SCB_RETRYCB);
    SetNextFunSCB(wifi_aps_xml);
    os_free(buf);
    return;
}
//===============================================================================
// WiFi Scan XML
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR web_wscan_xml(TCP_SERV_CONN *ts_conn)
{
	struct bss_scan_info si;
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
    // Check if this is a first round call
    if(CheckSCB(SCB_RETRYCB)==0) {
    	tcp_puts_fd("<total>%d</total>", total_scan_infos);
    	if(total_scan_infos == 0) return;
    	web_conn->udata_start = 0;
    }
	while(web_conn->msgbuflen + 96 + 32 <= web_conn->msgbufsize) {
	    if(web_conn->udata_start < total_scan_infos) {
	    	struct bss_scan_info *p = (struct bss_scan_info *)buf_scan_infos;
	    	p += web_conn->udata_start;
	    	ets_memcpy(&si, p, sizeof(si));
/*
	    	uint8 ssid[33];
	    	ssid[32] = '\0';
	    	ets_memcpy(ssid, si.ssid, 32); */
	    	uint8 ssid[32*6 + 1];
	    	if(web_conn->msgbuflen + 96 + htmlcode(ssid, si.ssid, 32*6, 32) > web_conn->msgbufsize) break;
			tcp_puts_fd("<ap id=\"%d\"><ch>%d</ch><au>%d</au><bs>" MACSTR "</bs><ss>%s</ss><rs>%d</rs><hd>%d</hd></ap>", web_conn->udata_start, si.channel, si.authmode, MAC2STR(si.bssid), ssid, si.rssi, si.is_hidden);
	   		web_conn->udata_start++;
	    	if(web_conn->udata_start >= total_scan_infos) {
			    ClrSCB(SCB_RETRYCB);
			    return;
	    	}
	    }
	    else {
		    ClrSCB(SCB_RETRYCB);
		    return;
	    }
	}
	// repeat in the next call ...
    SetSCB(SCB_RETRYCB);
    SetNextFunSCB(web_wscan_xml);
    return;
}
//===============================================================================
// WiFi Probe Request XML
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR web_ProbeRequest_xml(TCP_SERV_CONN *ts_conn)
{
	struct s_probe_requests pr;
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
    // Check if this is a first round call
	uint32 cnt = (probe_requests_count < MAX_COUNT_BUF_PROBEREQS)? probe_requests_count : MAX_COUNT_BUF_PROBEREQS;
    if(CheckSCB(SCB_RETRYCB)==0) {
    	if(cnt == 0) {
    		tcp_strcpy_fd("<total>0</total>");
    		return;
    	}
    	web_conn->udata_start = 0;
    }
	while(web_conn->msgbuflen + 92 <= web_conn->msgbufsize) {
	    if(web_conn->udata_start < cnt) {
	    	struct s_probe_requests *p = (struct s_probe_requests *)&buf_probe_requests;
	    	p += web_conn->udata_start;
	    	ets_memcpy(&pr, p, sizeof(struct s_probe_requests));
			tcp_puts_fd("<pr id=\"%u\"><mac>" MACSTR "</mac><min>%d</min><max>%d</max></pr>", web_conn->udata_start, MAC2STR(pr.mac), pr.rssi_min, pr.rssi_max);
	   		web_conn->udata_start++;
	    	if(web_conn->udata_start >= cnt) {
	    		tcp_puts_fd("<total>%d</total>", cnt);
			    ClrSCB(SCB_RETRYCB);
			    return;
	    	}
	    }
	    else {
		    ClrSCB(SCB_RETRYCB);
		    return;
	    }
	}
	// repeat in the next call ...
    SetSCB(SCB_RETRYCB);
    SetNextFunSCB(web_ProbeRequest_xml);
    return;
}
#ifdef USE_MODBUS
//===============================================================================
// Mdb XML
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR web_modbus_xml(TCP_SERV_CONN *ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
	while(web_conn->msgbuflen + 24 <= web_conn->msgbufsize) {
	    if(web_conn->udata_start < web_conn->udata_stop) {
	    	uint16 val16;
	    	if(RdMdbData((uint8 *)&val16, web_conn->udata_start, 1) != 0) tcp_puts_fd("<m%u>?</m%u>", web_conn->udata_start, web_conn->udata_start);
	    	else {
		    	if(ts_conn->flag.user_option2) {
		    		if(ts_conn->flag.user_option1) {
			    		tcp_puts_fd("<m%u>0x%04x</m%u>", web_conn->udata_start, val16, web_conn->udata_start);
		    		}
				    else {
			    		tcp_puts_fd("<m%u>%04x</m%u>", web_conn->udata_start, val16, web_conn->udata_start);
				    };
		    	}
		    	else {
			    	if(ts_conn->flag.user_option1) {
			    		tcp_puts_fd("<m%u>%d</m%u>", web_conn->udata_start, (sint32)((sint16)val16), web_conn->udata_start);
			    	}
			    	else {
			    		tcp_puts_fd("<m%u>%u</m%u>", web_conn->udata_start, val16, web_conn->udata_start);
			    	};
		    	};
	    	};
	    	web_conn->udata_start++;
	    }
	    else {
		    ClrSCB(SCB_RETRYCB);
		    return;
	    }
	}
	// repeat in the next call ...
    SetSCB(SCB_RETRYCB);
    SetNextFunSCB(web_modbus_xml);
    return;
}
#endif
//===============================================================================
// RAM hexdump
//-------------------------------------------------------------------------------
void ICACHE_FLASH_ATTR web_hexdump(TCP_SERV_CONN *ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *) ts_conn->linkd;
	union {
		uint32 dw[4];
		uint8 b[16];
	}data;
	web_conn->udata_start &= 0xfffffff0;
	uint32 *addr = (uint32 *)web_conn->udata_start;
	int i;
	web_conn->udata_stop &= 0xfffffff0;
	while(web_conn->msgbuflen + (9+3*16+17+2) <= web_conn->msgbufsize) {
		if(((uint32)addr >= 0x20000000)&&(((uint32)addr < 0x60002000)||((uint32)addr >= 0x60008000))) {
			tcp_puts("%08x", addr);
			for(i=0 ; i < 4 ; i++) data.dw[i] = *addr++;
			web_conn->udata_start = (uint32)addr;
			if(ts_conn->flag.user_option1) {
				for(i=0 ; i < 4 ; i++) tcp_puts(" %08x", data.dw[i]);
			}
			else {
				for(i=0 ; i < 16 ; i++) tcp_puts(" %02x", data.b[i]);
			}
			tcp_put(' '); tcp_put(' ');
			for(i=0 ; i < 16 ; i++) tcp_put((data.b[i] >=' ' && data.b[i] != 0x7F)? data.b[i] : '.');
			tcp_puts("\r\n");
			if((uint32)addr >= web_conn->udata_stop) {
			    ClrSCB(SCB_RETRYCB);
			    SetSCB(SCB_FCLOSE | SCB_DISCONNECT); // connection close
			    return;
			}
		}
		else {
			tcp_puts("%p = Bad address!\r\n", addr);
		    ClrSCB(SCB_RETRYCB);
		    SetSCB(SCB_FCLOSE | SCB_DISCONNECT); // connection close
		    return;
		}
	}
	// repeat in the next call ...
    SetSCB(SCB_RETRYCB);
    SetNextFunSCB(web_hexdump);
    return;
}

//---------------------------- output history.csv ---------------------------
typedef struct {
	uint32 	PtrCurrent; 	// ptr in the FRAM array
	time_t 	LastTime;		// Last printed time
	time_t	PreviousTime; 	// Previous printed time
	int32_t	minutes;		// How many minutes printed
	bool 	FlagContinue;	// Need continue print packed
	uint8_t OutType;		// HST_*
	int32_t	len;
	int32_t	i;
	bool 	packed_flag;
	uint8_t	n;
	uint32	previous_n;		// to skip multi-zeros or in TotalCnt mode the same value
	uint32	Sum;			// for OutType by date / TotalCnt
	uint32	SumT1;			// for OutType by date / TotalCnt
	uint32	*Hours;
	uint16_t StringSizeMax;  // 32, 64, 900
	uint8_t	buf[MAX_EEPROM_BLOCK_LEN];
} history_output;

// history_output.OutType
#define HST_MTariffs   0b10000
#define HST_TotalCnt	0b1000
#define HST_ByHour		0b0100
#define HST_ByDay		0b0010
#define HST_kWt			0b0001

// return True if overflow
void web_get_history_put_csv_str(WEB_SRV_CONN *web_conn, history_output *hst, time_t Time, uint32_t num)
{
	struct tm tm;
	if(hst->OutType & HST_TotalCnt) Time -= 60; // TotalCnt
	_localtime(&Time, &tm);
	tcp_puts("%04d-%02d-%02d %02d:%02d:00%c", 1900+tm.tm_year, 1+tm.tm_mon, tm.tm_mday, tm.tm_hour, tm.tm_min, cfg_glo.csv_delimiter);
	if(hst->OutType & HST_kWt) { // kWt
		if((hst->OutType & (HST_TotalCnt | HST_ByHour | HST_ByDay)) == 0) num *= 60; // kwt per hour
		uint32_t n = num * 10 / cfg_glo.PulsesPer0_01KWt;
		tcp_puts("%u.%03u", n / 1000, n % 1000);
		if(hst->OutType & HST_MTariffs) {
			n = hst->SumT1 * 10 / cfg_glo.PulsesPer0_01KWt;
			tcp_puts("%c%u.%03u", cfg_glo.csv_delimiter, n / 1000, n % 1000);
			n = (num - hst->SumT1) * 10 / cfg_glo.PulsesPer0_01KWt;
			tcp_puts("%c%u.%03u", cfg_glo.csv_delimiter, n / 1000, n % 1000);
		}
	} else {
		tcp_puts("%u", num);
		if(hst->OutType & HST_MTariffs) {
			tcp_puts("%c%u%c%u", cfg_glo.csv_delimiter, hst->SumT1, cfg_glo.csv_delimiter, num - hst->SumT1);
		}
	}
	tcp_puts("\r\n");
	#if DEBUGSOO > 4
		os_printf("%02d.%02d.%04d %02d:%02d:%02d,%d\n", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec, num);
	#endif
}

// Output history by 1 min from last record to previous,
// web_conn->udata_stop - how many minutes out, if minutes = 0 - all records
// yyyy-mm-dd hh:mm:00,n
void ICACHE_FLASH_ATTR web_get_history(TCP_SERV_CONN *ts_conn)
{
	history_output * hst;
	int32 len, i;
	uint8 n;
	bool packed_flag;

    WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
	uint32 wtimer = system_get_time();
	#if DEBUGSOO > 2
		os_printf("History %u, %u (%u): ", web_conn->udata_start, web_conn->udata_stop, wtimer);
	#endif
    if(CheckSCB(SCB_RETRYCB)==0) {  // Check if this is a first round call
		hst = os_zalloc(sizeof(history_output));
		if(hst == NULL) {
			#if DEBUGSOO > 2
				os_printf("Error malloc: %u\n", sizeof(history_output));
			#endif
			return;
		}
		hst->StringSizeMax = 32;
		hst->minutes = web_conn->udata_stop;
		web_conn->udata_stop = (uint32)hst;
		hst->OutType = web_conn->udata_start;
		hst->PtrCurrent = fram_store.PtrCurrent;
		if(CntCurrent.Cnt2) { // packed available
			hst->PtrCurrent += CntCurrent.Cnt2 == 1 ? 1 : 2; // 0,1 in CurrentCnt is 0 last minute
			if(hst->PtrCurrent >= cfg_glo.Fram_Size - StartArrayOfCnts) hst->PtrCurrent -= cfg_glo.Fram_Size - StartArrayOfCnts;
		}
		hst->LastTime = fram_store.LastTime;
		if(hst->OutType & HST_MTariffs) {
			tcp_puts("date,value,T1,T2\r\n"); // csv header
		} else {
			tcp_puts("date,value\r\n"); // csv header
		}
		if(hst->OutType & HST_ByHour) {
			if((hst->Hours = (uint32 *) os_zalloc(25 * sizeof(uint32))) == NULL) {
				#if DEBUGSOO > 2
					os_printf("Error malloc hours: %u\n", sizeof(history_output));
				#endif
				return;
			}
			hst->StringSizeMax = 900;
		}
		if(hst->OutType & HST_TotalCnt) { // TotalCnt
			hst->Sum = fram_store.TotalCnt;
			if(hst->OutType & HST_MTariffs) {
				hst->SumT1 = fram_store.TotalCntT1;
				hst->StringSizeMax = 64;
			}
			if((hst->OutType & HST_ByDay) == 0) { // not By day
				hst->previous_n = hst->Sum; // save value and wait changing
			}
		} else hst->previous_n = 0xFFFFFFFF;
    } else hst = (history_output *)web_conn->udata_stop; // restore ptr
    // Get/put as many bytes as possible
	if(hst->FlagContinue) {
		len = hst->len;
		i = hst->i;
		n = hst->n;
		packed_flag = hst->packed_flag;
		if(hst->minutes == 0 || (packed_flag && n == 0)) goto xEnd;
		goto xContinue;
	}
	do {
		uint16_t num = 0;
		if(hst->PtrCurrent == 0) hst->PtrCurrent = cfg_glo.Fram_Size - StartArrayOfCnts; // jump to the end
		len = mMIN(sizeof(hst->buf), hst->PtrCurrent);
		#if DEBUGSOO > 4
			os_printf(" st %u -> len: %u (+%u), ", hst->PtrCurrent, len, system_get_time() - wtimer);
		#endif
		if(eeprom_read_block(StartArrayOfCnts + hst->PtrCurrent - len, hst->buf, len)) {
xErrorI2C:
			#if DEBUGSOO > 2
				os_printf("fram R error\n");
			#endif
			break;
		} else {
			if(len == 1) { // may be packed - load previous byte from end
				hst->buf[1] = hst->buf[0];
				if(eeprom_read_block(cfg_glo.Fram_Size - 1, hst->buf, 1)) goto xErrorI2C;
				len = 2;
			}
			for(i = len - 1; i > 0; i--) { // first byte may be not proceeded
				n = hst->buf[i];
				#if DEBUGSOO > 4
					os_printf(" %d=%d ", i, n);
				#endif
				packed_flag = hst->buf[i-1] == 0; // packed
				if(packed_flag) {
					if(n == 0) { // end
xEnd:
						if(web_conn->msgbuflen + hst->StringSizeMax > web_conn->msgbufsize) goto xBufferFull; // overflow
						if(hst->OutType & HST_ByHour) {
							hst->LastTime = fram_store.LastTime / 86400 * 86400;
							for(n = 0; n < 24; n++) {
								web_get_history_put_csv_str(web_conn, hst, hst->LastTime, hst->Hours[n]);
								hst->LastTime += 3600;
							}
							hst->LastTime--;
						}
						// if TotalCnt/ByDay - print sum, otherwise 0;
						web_get_history_put_csv_str(web_conn, hst, hst->LastTime, (hst->OutType & (HST_TotalCnt | HST_ByDay)) ? hst->Sum : 0);
						goto xEndExit;
					}
					if(n == 1) packed_flag = 0; // special case "0,1" - last min = 1, previous min = 0
					else i--;
				}
				do {
xContinue:
					if((system_get_time() - wtimer > 100000 && web_conn->msgbuflen)
							|| web_conn->msgbuflen + hst->StringSizeMax > web_conn->msgbufsize) { // overflow or timer > 100000us and buffer is not empty
xBufferFull:			hst->len = len;
						hst->i = i;
						hst->n = n;
						hst->packed_flag = packed_flag;
						hst->FlagContinue = 1;
						#if DEBUGSOO > 4
							os_printf("Buf full (%u)/wtimer: %d, %d, %d.\n", web_conn->msgbuflen, len, i, n);
						#endif
						SetNextFunSCB(web_get_history);
						SetSCB(SCB_RETRYCB);
						return;
					}
					num = packed_flag ? 0 : n;
					uint32_t prn_num;
					if(hst->OutType & HST_TotalCnt) {
						hst->Sum -= num; // TotalCnt
						if(hst->OutType & HST_MTariffs) check_add_CntT1(&hst->LastTime, &hst->SumT1, -num);
					}
					if(hst->OutType & HST_ByDay) { // by day
						if((hst->OutType & HST_TotalCnt) == 0) hst->Sum += num; // Not TotalCnt(+)
						if(hst->LastTime % 86400 / 60 != 0) { // time is not 00:00 - skip
							goto xSkip;
						}
						prn_num = hst->Sum;
					} else if(hst->OutType & HST_TotalCnt) { // TotalCnt not by day
						if(hst->previous_n == hst->Sum) goto xSkip; // Skip the same value
						prn_num = hst->previous_n;
					} else {
						prn_num = num;
					}
					if(hst->OutType & HST_ByHour) {
						hst->Hours[hst->LastTime % 86400 / 3600] += prn_num;
					} else {
						web_get_history_put_csv_str(web_conn, hst, hst->LastTime, prn_num);
					}
					if((hst->OutType & HST_TotalCnt) == 0) hst->Sum = 0; // not TotalCnt
					hst->previous_n = (hst->OutType & HST_TotalCnt) ? hst->Sum : num; // TotalCnt = sum
xSkip:
					hst->PreviousTime = hst->LastTime;
					hst->LastTime -= TIME_STEP_SEC;
					if(hst->minutes) {
						hst->minutes -= 1;
						if(hst->minutes == 0) goto xEnd;
					}
				} while(packed_flag && --n);
			}
			if(i <= 0) { // buffer has been proceeded
				len -= 1 + i;
				if(hst->PtrCurrent < len) hst->PtrCurrent += cfg_glo.Fram_Size - StartArrayOfCnts - len;
				else hst->PtrCurrent -= len;
				#if DEBUGSOO > 4
					os_printf("H ptr_curr: %u, t%u\n", hst->PtrCurrent, hst->LastTime);
				#endif
			}
		}
	} while(1);
xEndExit:
	#if DEBUGSOO > 4
		os_printf("End(mbs=%u) ", web_conn->msgbufsize);
	#endif
	if(hst->Hours) os_free(hst->Hours);
	os_free(hst);
	web_conn->udata_stop = 0;
	ClrSCB(SCB_RETRYCB);
	//FRAM_Status = 2;
}

// Output i2c from eeprom web_conn->udata_start, end: web_conn->udata_stop
void ICACHE_FLASH_ATTR web_get_i2c_eeprom(TCP_SERV_CONN *ts_conn)
{
    WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
    // Check if this is a first round call
    if(CheckSCB(SCB_RETRYCB)==0) {
    	if(web_conn->udata_start == web_conn->udata_stop) return;
#if DEBUGSOO > 2
		os_printf("i2c from:%u ", web_conn->udata_start);
#endif
    }
    // Get/put as many bytes as possible
    unsigned int len = mMIN(web_conn->msgbufsize - web_conn->msgbuflen, FRAM_MAX_BLOCK_AT_ONCE);
#if DEBUGSOO > 2
	os_printf("%u+%u ",web_conn->udata_start, len);
#endif
	if(eeprom_read_block(web_conn->udata_start, web_conn->msgbuf + web_conn->msgbuflen, len)) {
		os_printf("i2c R error %u, %u\n", web_conn->udata_start, len);
		//FRAM_Status = 2;
	} else {
		web_conn->udata_start += len;
		web_conn->msgbuflen += len;
		if(web_conn->udata_start < web_conn->udata_stop) {
			SetSCB(SCB_RETRYCB);
    		SetNextFunSCB(web_get_i2c_eeprom);
    		return;
    	}
    }
    ClrSCB(SCB_RETRYCB);
//    SetSCB(SCB_FCLOSE | SCB_DISCONNECT);
    return;
}

/******************************************************************************
 * FunctionName : web saved flash
 * Description  : Processing the flash data send
 * Parameters   : none (Calback)
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR web_get_flash(TCP_SERV_CONN *ts_conn)
{
    WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
    // Check if this is a first round call
    if(CheckSCB(SCB_RETRYCB)==0) {
    	if(web_conn->udata_start == web_conn->udata_stop) return;
#if DEBUGSOO > 2
		os_printf("file_size:%08x ", web_conn->udata_stop - web_conn->udata_start );
#endif
    }
    // Get/put as many bytes as possible
    unsigned int len = mMIN(web_conn->msgbufsize - web_conn->msgbuflen, web_conn->udata_stop - web_conn->udata_start);
    // Read Flash addr = web_conn->webfinc_offsets, len = x, buf = sendbuf
#if DEBUGSOO > 2
	os_printf("%08x..%08x ",web_conn->udata_start, web_conn->udata_start + len );
#endif
    if(spi_flash_read(web_conn->udata_start, web_conn->msgbuf + web_conn->msgbuflen, len) == SPI_FLASH_RESULT_OK) {
      web_conn->udata_start += len;
      web_conn->msgbuflen += len;
      if(web_conn->udata_start < web_conn->udata_stop) {
        SetSCB(SCB_RETRYCB);
        SetNextFunSCB(web_get_flash);
        return;
      };
    };
    ClrSCB(SCB_RETRYCB);
//    SetSCB(SCB_FCLOSE | SCB_DISCONNECT);
    return;
}
/******************************************************************************
 * FunctionName : web saved flash
 * Description  : Processing the flash data send
 * Parameters   : none (Calback)
 * Returns      : none
*******************************************************************************/
void ICACHE_FLASH_ATTR web_get_ram(TCP_SERV_CONN *ts_conn)
{
    WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
    // Check if this is a first round call
    if(CheckSCB(SCB_RETRYCB)==0) { // On initial call, проверка параметров
		if(web_conn->udata_start == web_conn->udata_stop) {
//    	    SetSCB(SCB_FCLOSE | SCB_DISCONNECT);
    	    return;
		}
#if DEBUGSOO > 2
		os_printf("file_size:%08x ", web_conn->udata_stop - web_conn->udata_start );
#endif
	}
    // Get/put as many bytes as possible
    uint32 len = mMIN(web_conn->msgbufsize - web_conn->msgbuflen, web_conn->udata_stop - web_conn->udata_start);
	copy_align4(web_conn->msgbuf + web_conn->msgbuflen, (void *)(web_conn->udata_start), len);
	web_conn->msgbuflen += len;
	web_conn->udata_start += len;
#if DEBUGSOO > 2
	os_printf("%08x-%08x ",web_conn->udata_start, web_conn->udata_start + len );
#endif
    if(web_conn->udata_start != web_conn->udata_stop) {
        SetSCB(SCB_RETRYCB);
        SetNextFunSCB(web_get_ram);
        return;
    };
    ClrSCB(SCB_RETRYCB);
//    SetSCB(SCB_FCLOSE | SCB_DISCONNECT);
    return;
}
/******************************************************************************
 * FunctionName : get_new_url
 * Parameters   : Parameters   : struct TCP_SERV_CONN
 * Returns      : string url
 ******************************************************************************/
void ICACHE_FLASH_ATTR get_new_url(TCP_SERV_CONN *ts_conn)
{
	WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
	uint32 ip = 0;
	uint32 ip_ap = 0;
	uint32 ip_st = 0;
//	int z  = NULL_MODE;
	struct ip_info wifi_info;
	WIFI_MODE opmode = wifi_get_opmode();
	if(opmode == STATIONAP_MODE) {
		if((opmode & STATION_MODE) && wifi_get_ip_info(STATION_IF, &wifi_info)) {
			ip_st = wifi_info.ip.addr;
		}
		if((opmode & SOFTAP_MODE) && wifi_get_ip_info(SOFTAP_IF, &wifi_info)) {
			ip_ap = wifi_info.ip.addr;
		}
		if(((ts_conn->pcb->local_ip.addr^ip_ap) & 0xFFFFFF) == 0 || (ip_current_netif() != NULL && ip_current_netif()->ip_addr.addr == ip_ap)) {
			ip = ip_ap;
			opmode = SOFTAP_MODE;
		}
		else if(((ts_conn->pcb->local_ip.addr^ip_st) & 0xFFFFFF) == 0 || (ip_current_netif() != NULL && ip_current_netif()->ip_addr.addr == ip_st)) {
			ip = ip_st;
			opmode = STATION_MODE;
		};
	};
	opmode &= wificonfig.b.mode;
	if(opmode == WIFI_DISABLED) opmode = wificonfig.b.mode;
	if(wificonfig.ap.ipinfo.ip.addr == 0 || (opmode & SOFTAP_MODE)) {
		ip = wificonfig.ap.ipinfo.ip.addr;
#ifdef USE_NETBIOS
		netbios_name[0]='A';
#endif
	} else {
		ip = wificonfig.st.ipinfo.ip.addr;
#ifdef USE_NETBIOS
		netbios_name[0]='S';
#endif
	}
#ifdef USE_NETBIOS
	if(syscfg.cfg.b.netbios_ena) tcp_strcpy(netbios_name);
	else {
		if(ip == 0) tcp_strcpy_fd("esp8266.ru"); 
		else tcp_puts(IPSTR, IP2STR(&ip));
	};
#else
	if(ip == 0) ip = WEB_DEFAULT_SOFTAP_IP; // 0x0104A8C0
	tcp_puts(IPSTR, IP2STR(&ip));
#endif
	if(syscfg.web_port != 80) tcp_puts(":%u", syscfg.web_port);
}

/******************************************************************************
 * FunctionName : web_callback
 * Description  : callback, DO NOT send thru tcp_puts more than SCB_SEND_SIZE (128 bytes)!
 * Parameters   : struct TCP_SERV_CONN
 * Returns      : none
 ******************************************************************************/
void ICACHE_FLASH_ATTR web_int_callback(TCP_SERV_CONN *ts_conn, uint8 *cstr)
{
    WEB_SRV_CONN *web_conn = (WEB_SRV_CONN *)ts_conn->linkd;
//        uint8 *cstr = &web_conn->msgbuf[web_conn->msgbuflen];
        {
           uint8 *vstr = os_strchr(cstr, '=');
           if(vstr != NULL) {
        	   *vstr++ = '\0';
        	   web_int_vars(ts_conn, cstr, vstr);
        	   return;
           }
        }
#if DEBUGSOO > 5
        os_printf("[%s]\n", cstr);
#endif
        ifcmp("start") tcp_puts("0x%08x", web_conn->udata_start);
        else ifcmp("stop") tcp_puts("0x%08x", web_conn->udata_stop);
        else ifcmp("xml_") {
            cstr+=4;
            web_conn->udata_start&=~3;
            ifcmp("ram") tcp_puts("0x%08x", *((uint32*)web_conn->udata_start));
#ifdef USE_MODBUS
            else ifcmp("mdb") {
            	web_conn->udata_start &= 0xffff;
      			web_conn->udata_stop &= 0xffff;
      			if(web_conn->udata_stop <= web_conn->udata_start) web_conn->udata_stop = web_conn->udata_start + 10;
      			if(cstr[3] == 'h')	{
      				ts_conn->flag.user_option1 = 0;
      				ts_conn->flag.user_option2 = 1;
      			}
      			else if(cstr[3] == 'x')	{
      				ts_conn->flag.user_option1 = 1;
      				ts_conn->flag.user_option2 = 1;
      			}
      			else if(cstr[3] == 's')	{
      				ts_conn->flag.user_option1 = 1;
      				ts_conn->flag.user_option2 = 0;
      			}
      			else {
      				ts_conn->flag.user_option1 = 0;
      				ts_conn->flag.user_option2 = 0;
      			}
      			web_modbus_xml(ts_conn);
            }
#endif
            else tcp_put('?');
            web_conn->udata_start += 4;
        }
#ifdef USE_MODBUS
        else ifcmp("mdb") {
        	if(cstr[3]=='_') {
            	cstr+=4;
            	struct tcp_pcb *pcb = NULL;
            	if(mdb_tcp_conn != NULL) pcb = mdb_tcp_conn->pcb;
            	ifcmp("remote") {
       				if(pcb != NULL) {
       					tcp_puts(IPSTR ":%d", IP2STR(&(pcb->remote_ip.addr)), pcb->remote_port);
       				}
            		else tcp_strcpy_fd("none");
            	}
#ifndef USE_TCP2UART
            	else ifcmp("host") {
        			if(pcb != NULL) {
            			tcp_puts(IPSTR ":%d", IP2STR(&(pcb->local_ip.addr)), pcb->local_port);
            		}
            		else tcp_strcpy_fd("closed");
            	}
#endif
#ifdef USE_RS485DRV
            	else ifcmp("trn") {
            		if(cstr[3]=='s') tcp_puts("%u", MDB_TRN_MAX);
            		else if(cstr[3]=='a') tcp_puts("%u", MDB_BUF_MAX);
                	else tcp_put('?');
            	}
#endif
            	else tcp_put('?');
        	}
        	else if(cstr[3]=='a') {
            	cstr+=4;
        		if((*cstr>='0')&&(*cstr<='9')) {
        			uint32 addr = ahextoul(cstr);
        			if(addr < 0x10000) {
        				uint32 val = 0;
        				uint32 i = 16;
        				while(i--) {
        					if(RdMdbData((uint8 *)&val, addr++, 1) != 0) break;
        					if(val&0xFF) tcp_put(val);
        					else break;
        					if(val>>8) tcp_put(val>>8);
        					else break;
        				}
        			}
        		}
        		else tcp_put('?');
        	}
        	else if(cstr[3]=='u' || cstr[3]=='s' || cstr[3]=='h' || cstr[3]=='x') {
        		cstr+=5;
        		if((*cstr>='0')&&(*cstr<='9')) {
        			uint32 addr = ahextoul(cstr);
        			if(addr < 0x10000) {
        				uint32 val = 0;
        				if(cstr[-1]=='w') {
        					if(RdMdbData((uint8 *)&val, addr, 1) != 0) tcp_put('?');
        					else if (cstr[-2]=='u') tcp_puts("%u", val);
        					else if (cstr[-2]=='s') tcp_puts("%d", (val&0x8000)? val | 0xFFFF0000 : val);
        					else if (cstr[-2]=='h') tcp_puts("%04x", val);
        					else if (cstr[-2]=='x') tcp_puts("0x%04x", val);
        				}
        				else if(cstr[-1]=='d') {
        					if(RdMdbData((uint8 *)&val, addr, 2) != 0) tcp_put('?');
        					else if (cstr[-2]=='u') tcp_puts("%u", val);
        					else if (cstr[-2]=='s') tcp_puts("%d", val);
        					else if (cstr[-2]=='h') tcp_puts("%08x", val);
        					else if (cstr[-2]=='x') tcp_puts("0x%08x", val);
        				}
        				else tcp_put('?');
        			}
        			else tcp_put('?');
    			}
    			else tcp_put('?');
        	};
        }
#endif
        else ifcmp("sys_") {
          cstr+=4;
          ifcmp("cid") tcp_puts("%08x", system_get_chip_id());
          else ifcmp("fid") tcp_puts("%08x", spi_flash_get_id());
          else ifcmp("fsize") tcp_puts("%u", spi_flash_real_size()); // flashchip->chip_size
          else ifcmp("sdkver") tcp_puts(system_get_sdk_version());
          else ifcmp("sysver") tcp_strcpy_fd(SYS_VERSION);
          else ifcmp("webver") tcp_strcpy_fd(WEB_SVERSION);
          else ifcmp("heap") tcp_puts("%u", system_get_free_heap_size());
          else ifcmp("adc") {
        	  uint16 x[4];
        	  read_adcs(x, 4, 0x00808);
        	  tcp_puts("%u", x[0]+x[1]+x[2]+x[3]); // 16 бит ADC :)
          }
          else ifcmp("time") tcp_puts("%u", system_get_time());
          else ifcmp("rtctime") tcp_puts("%u", system_get_rtc_time());
          else ifcmp("mactime") {
        	  union {
        	  		uint32 dw[2];
        	  		uint64 dd;
        	  	}ux;
        	  ux.dd = get_mac_time();
        	  tcp_puts("0x%08x%08x", ux.dw[1], ux.dw[0]);
          }
          else ifcmp("vdd33") tcp_puts("%u", readvdd33()); // system_get_vdd33() phy_get_vdd33();
          else ifcmp("vdd") tcp_puts("%u", system_get_vdd33()); //  phy_get_vdd33();
          else ifcmp("wdt") tcp_puts("%u", ets_wdt_get_mode());
          else ifcmp("res_event") tcp_puts("%u", rtc_get_reset_reason()); // 1 - power/ch_pd, 2 - reset, 3 - software, 4 - wdt ...
          else ifcmp("rst") tcp_puts("%u", system_get_rst_info()->reason);
          else ifcmp("clkcpu") tcp_puts("%u", ets_get_cpu_frequency());
          else ifcmp("clkspi") {
        	  uint32 r = READ_PERI_REG(SPI_CLOCK(0));
        	  tcp_puts("%u", ets_get_cpu_frequency()/(((r>>SPI_CLKDIV_PRE_S)&SPI_CLKDIV_PRE)+1)/(((r>>SPI_CLKCNT_N_S)&SPI_CLKCNT_N)+1));
          }
          else ifcmp("sleep_old") tcp_puts("%u", deep_sleep_option); // если нет отдельного питания RTC и deep_sleep не устанавливался/применялся при текущем включения питания чипа, то значение неопределенно (там хлам).
//          else ifcmp("test") { };
          else ifcmp("reset") web_conn->web_disc_cb = (web_func_disc_cb)_ResetVector;
          else ifcmp("restart") web_conn->web_disc_cb = (web_func_disc_cb)system_restart;
          else ifcmp("ram") tcp_puts("0x%08x", *((uint32 *)(ahextoul(cstr+3)&(~3))));
          else ifcmp("rdec") tcp_puts("%d", *((uint32 *)(ahextoul(cstr+4)&(~3))));
//          else ifcmp("reg") tcp_puts("0x%08x", IOREG(ahextoul(cstr+3)));
          else ifcmp("ip") {
/*        	  uint32 cur_ip = 0;
        	  if(netif_default != NULL) cur_ip = netif_default->ip_addr.addr; */
        	  uint32 cur_ip = ts_conn->pcb->local_ip.addr;
//        	  if(cur_ip == 0) cur_ip = 0x0104A8C0;
			  tcp_puts(IPSTR, IP2STR(&cur_ip));
          }
          else ifcmp("url") get_new_url(ts_conn);
          else ifcmp("debug") tcp_puts("%u", system_get_os_print()&1); // system_set_os_print
          else ifcmp("const_") {
        	  cstr += 6;
        	  ifcmp("faddr") {
        		  tcp_puts("0x%08x", faddr_esp_init_data_default);
        	  }
        	  else tcp_puts("%u", read_sys_const(ahextoul(cstr)));
          }
          else ifcmp("ucnst_") tcp_puts("%u", read_user_const(ahextoul(cstr+6)));
#ifdef USE_NETBIOS
          else ifcmp("netbios") {
       		  if(syscfg.cfg.b.netbios_ena) tcp_strcpy(netbios_name);
          }
#endif
          else tcp_put('?');
        }
        else ifcmp("cfg_") {
			cstr += 4;
			ifcmp("web_") {
				cstr += 4;
				ifcmp("port") tcp_puts("%u", syscfg.web_port);
	        	else ifcmp("twrec") tcp_puts("%u", syscfg.web_twrec);
	        	else ifcmp("twcls") tcp_puts("%u", syscfg.web_twcls);
				else ifcmp("twd") tcp_put((syscfg.cfg.b.web_time_wait_delete)? '1' : '0');
				else tcp_put('?');
			}
		    else ifcmp("tcp_") {
		    	cstr+=4;
		    	ifcmp("tcrec") tcp_puts("%u", syscfg.tcp_client_twait);
#ifdef USE_TCP2UART
	        	else ifcmp("port") tcp_puts("%u", syscfg.tcp2uart_port);
	        	else ifcmp("twrec") tcp_puts("%u", syscfg.tcp2uart_twrec);
	        	else ifcmp("twcls") tcp_puts("%u", syscfg.tcp2uart_twcls);
	        	else ifcmp("url") {
	        		if(tcp_client_url == NULL) tcp_puts("none");
	        		else tcp_puts("%s", tcp_client_url);
	        	}
	        	else ifcmp("reop") tcp_put((syscfg.cfg.b.tcp2uart_reopen)? '1' : '0');
#endif
		    	else tcp_put('?');
		    }
		    else ifcmp("meter_") { // cfg_
	        	cstr += 6;
		        ifcmp("PulsesPerKWt") tcp_puts("%u00", cfg_glo.PulsesPer0_01KWt);
		        else ifcmp("Fram_Size") tcp_puts("%u", cfg_glo.Fram_Size);
		        else ifcmp("csv_delim") tcp_puts("%c", cfg_glo.csv_delimiter);
		        else ifcmp("FramFr") tcp_puts("%u", cfg_glo.fram_freq);
		        else ifcmp("Debouncing") tcp_puts("%u", cfg_glo.Debouncing_Timeout);
		        else ifcmp("revsens") tcp_puts("%u", cfg_glo.ReverseSensorPulse);
		        else ifcmp("TAdj") tcp_puts("%d", cfg_glo.TimeAdjust);
		        else ifcmp("T1St") tcp_puts("%04u", cfg_glo.TimeT1Start);
		        else ifcmp("T1En") tcp_puts("%04u", cfg_glo.TimeT1End);
		    }
	        else ifcmp("iot_") {	// cfg_
	        	cstr += 4;
	        	ifcmp("cloud_enable") tcp_puts("%d", cfg_glo.iot_cloud_enable);
	            else ifcmp("ini") {
	        		web_conn->udata_start = 0; // pos in the file
	        		web_conn->udata_stop = WEBFSOpen(iot_cloud_ini); // file handle
	            	web_callback_textfile(ts_conn);
	            }
	        }
#ifdef USE_MODBUS
		    else ifcmp("mdb_") {
		    	cstr+=4;
		    	ifcmp("port") tcp_puts("%u", syscfg.mdb_port);
		    	else ifcmp("twrec") tcp_puts("%u", syscfg.mdb_twrec);
		    	else ifcmp("twcls") tcp_puts("%u", syscfg.mdb_twcls);
	        	else ifcmp("url") {
	        		if(tcp_client_url == NULL) tcp_puts("none");
	        		else tcp_puts("%s", tcp_client_url);
	        	}
	        	else ifcmp("reop") tcp_put((syscfg.cfg.b.mdb_reopen)? '1' : '0');
	        	else ifcmp("id") tcp_puts("%u", syscfg.mdb_id);
		    	else tcp_put('?');
		    }
#endif
			else ifcmp("overclk") tcp_put((syscfg.cfg.b.hi_speed_enable)? '1' : '0');
			else ifcmp("pinclr") tcp_put((syscfg.cfg.b.pin_clear_cfg_enable)? '1' : '0');
			else ifcmp("debug") tcp_put((syscfg.cfg.b.debug_print_enable)? '1' : '0');
#ifdef USE_NETBIOS
			else ifcmp("netbios") tcp_put((syscfg.cfg.b.netbios_ena)? '1' : '0');
#endif
#ifdef USE_SNTP
			else ifcmp("sntp") tcp_put((syscfg.cfg.b.sntp_ena)? '1' : '0');
#endif
#ifdef USE_CAPTDNS
			else ifcmp("cdns") tcp_put((syscfg.cfg.b.cdns_ena)? '1' : '0');
#endif
		    else tcp_put('?');
		}
        else ifcmp("wifi_") {
          cstr+=5;
          ifcmp("rdcfg") Read_WiFi_config(&wificonfig, WIFI_MASK_ALL);
          else ifcmp("newcfg") {
/*        	  if(CheckSCB(SCB_WEBSOC)) {
        		  tcp_puts("%d",New_WiFi_config(WIFI_MASK_ALL));
        	  }
        	  else { */
        		  web_conn->web_disc_cb = (web_func_disc_cb)New_WiFi_config;
        		  web_conn->web_disc_par = WIFI_MASK_ALL;
//        	  }
          }
//          else ifcmp("csta") tcp_puts("%d", wifi_station_get_connect_status());
          else ifcmp("mode") tcp_puts("%d", wifi_get_opmode());
          else ifcmp("phy") tcp_puts("%d", wifi_get_phy_mode());
//          else ifcmp("chl") tcp_puts("%d", wifi_get_channel());
          else ifcmp("sleep") tcp_puts("%d", wifi_get_sleep_type());
          else ifcmp("scan") web_wscan_xml(ts_conn);
          else ifcmp("prrq_xml") web_ProbeRequest_xml(ts_conn);
//          else ifcmp("aucn") tcp_puts("%d", wifi_station_get_auto_connect());
//          else ifcmp("power") { rom_en_pwdet(1); tcp_puts("%d", rom_get_power_db()); }
//          else ifcmp("noise") tcp_puts("%d", get_noisefloor_sat()); // noise_init(), rom_get_noisefloor(), ram_get_noisefloor(), ram_set_noise_floor(),noise_check_loop(), read_hw_noisefloor(), ram_start_noisefloor()
//          else ifcmp("hwnoise") tcp_puts("%d", read_hw_noisefloor());
/*          else ifcmp("fmsar") { // Test!
        	  rom_en_pwdet(1); // ?
        	  tcp_puts("%d", ram_get_fm_sar_dout(ahextoul(cstr+5)));
          } */
          else ifcmp("rfopt") tcp_puts("%u",(RTC_RAM_BASE[24]>>16)&7); // system_phy_set_rfoption | phy_afterwake_set_rfoption(option); 0..4
          else ifcmp("vddpw") tcp_puts("%u", phy_in_vdd33_offset); // system_phy_set_tpw_via_vdd33(val); // = pphy_vdd33_set_tpw(vdd_x_1000); Adjust RF TX Power according to VDD33, unit: 1/1024V, range [1900, 3300]
          else ifcmp("maxpw") tcp_puts("%u", phy_in_most_power); // read_sys_const(34));// system_phy_set_max_tpw(val); // = phy_set_most_tpw(pow_db); unit: 0.25dBm, range [0, 82], 34th byte esp_init_data_default.bin
          else ifcmp("aps_") {
        	  cstr+=4;
        	  ifcmp("xml") wifi_aps_xml(ts_conn);
              else ifcmp("id") tcp_puts("%d", wifi_station_get_current_ap_id());
              else tcp_put('?');
/*
        	  struct station_config config[5];
        	  int x = wifi_station_get_ap_info(config);
              if(cstr[1] != '_' || cstr[0]<'0' || cstr[0]>'4' ) {
            	  ifcmp("cnt") tcp_puts("%d", x);
            	  else tcp_put('?');
              }
              int i = cstr[0]-'0';
       	      if(i < x) {
       	    	  ifcmp("ssid") tcp_htmlstrcpy(config[i].ssid, sizeof(config[0].ssid));
       	    	  else ifcmp("psw") tcp_htmlstrcpy(config[i].password, sizeof(config[0].password));
       	    	  else ifcmp("sbss") tcp_puts("%d", config[i].bssid_set);
       	      	  else ifcmp("bssid") tcp_puts(MACSTR, MAC2STR(config[i].bssid));
              	  else tcp_put('?');
       	      }
        	  else ifcmp("id") wifi_aps_xml(ts_conn); */
          }
          else {
            uint8 if_index;
            ifcmp("ap_") if_index = SOFTAP_IF;
            else ifcmp("st_") if_index = STATION_IF;
            else { tcp_put('?'); return; };
            cstr+=3;
			struct ip_info wifi_info;
            if(if_index == SOFTAP_IF) {
            	// SOFTAP_MODE
            	ifcmp("dhcp") tcp_puts("%d", (dhcps_flag==0)? 0 : 1);
				else ifcmp("mac") {
					uint8 macaddr[6];
					wifi_get_macaddr(if_index, macaddr);
					tcp_puts(MACSTR, MAC2STR(macaddr));
				}
				else {
					uint8 opmode = wifi_get_opmode();
#ifndef USE_OPEN_DHCPS
					struct dhcps_lease dhcps_lease = wificonfig.ap.ipdhcp;
					wifi_softap_get_dhcps_lease(&dhcps_lease);
#endif
					if (((opmode & SOFTAP_MODE) == 0) || (!wifi_get_ip_info(SOFTAP_IF, &wifi_info))) {
						wifi_info = wificonfig.ap.ipinfo;
#ifdef USE_OPEN_DHCPS
						dhcps_lease = wificonfig.ap.ipdhcp;
#endif
//						p_dhcps_lease->start_ip = htonl(wificonfig.ap.ipdhcp.start_ip);
//						p_dhcps_lease->end_ip = htonl(wificonfig.ap.ipdhcp.end_ip);
//						p_dhcps_lease->start_ip = wificonfig.ap.ipdhcp.start_ip;
//						p_dhcps_lease->end_ip = wificonfig.ap.ipdhcp.end_ip;
					}
					ifcmp("sip") tcp_puts(IPSTR, IP2STR((struct ip_addr *)&dhcps_lease.start_ip));
					else ifcmp("eip") tcp_puts(IPSTR, IP2STR((struct ip_addr *)&dhcps_lease.end_ip));
					else ifcmp("ip") tcp_puts(IPSTR, IP2STR(&wifi_info.ip));
					else ifcmp("gw") tcp_puts(IPSTR, IP2STR(&wifi_info.gw));
					else ifcmp("msk") tcp_puts(IPSTR, IP2STR(&wifi_info.netmask));
					else {
						struct softap_config wicfgap;
						if(((opmode & SOFTAP_MODE) == 0) || (!wifi_softap_get_config(&wicfgap)) ) wicfgap = wificonfig.ap.config;
						ifcmp("ssid") tcp_htmlstrcpy(wicfgap.ssid, sizeof(wicfgap.ssid));
						else ifcmp("psw") tcp_htmlstrcpy(wicfgap.password, sizeof(wicfgap.password));
						else ifcmp("chl") tcp_puts("%d", wicfgap.channel);
						else ifcmp("auth") tcp_puts("%d", wicfgap.authmode);
						else ifcmp("hssid") tcp_puts("%d", wicfgap.ssid_hidden);
						else ifcmp("mcns") tcp_puts("%d", wicfgap.max_connection);
						else ifcmp("bint") tcp_puts("%d", wicfgap.beacon_interval);
						else tcp_put('?');
					};
				};
            }
            else { 
            	// STATION_MODE
            	ifcmp("dhcp") tcp_puts("%d", (dhcpc_flag==0)? 0 : 1);
            	else ifcmp("rssi") tcp_puts("%d", wifi_station_get_rssi());
    	        else ifcmp("aucn") tcp_puts("%d", wifi_station_get_auto_connect());
				else ifcmp("sta") tcp_puts("%d", wifi_station_get_connect_status());
	            else ifcmp("rect") tcp_puts("%u", wificonfig.st.reconn_timeout);
                else ifcmp("hostname") { if(hostname != NULL) tcp_puts(hostname); else tcp_puts("none"); }
                else ifcmp("mac") {
					uint8 macaddr[6];
					wifi_get_macaddr(if_index, macaddr);
                	tcp_puts(MACSTR, MAC2STR(macaddr));
                }
            	else {
					uint8 opmode = wifi_get_opmode();
                	if (((opmode & STATION_MODE) == 0) || (!wifi_get_ip_info(STATION_IF, &wifi_info))) wifi_info = wificonfig.st.ipinfo;
                    ifcmp("ip") tcp_puts(IPSTR, IP2STR(&wifi_info.ip));
                    else ifcmp("gw") tcp_puts(IPSTR, IP2STR(&wifi_info.gw));
                    else ifcmp("msk") tcp_puts(IPSTR, IP2STR(&wifi_info.netmask));
                    else {
                        struct station_config wicfgs;
                        if(((opmode & STATION_MODE) == 0) || (!wifi_station_get_config(&wicfgs)) ) wicfgs = wificonfig.st.config;
                        ifcmp("ssid") tcp_htmlstrcpy(wicfgs.ssid, sizeof(wicfgs.ssid));
                        else ifcmp("psw") tcp_htmlstrcpy(wicfgs.password, sizeof(wicfgs.password));
                        else ifcmp("sbss") tcp_puts("%d", wicfgs.bssid_set);
                        else ifcmp("bssid") tcp_puts(MACSTR, MAC2STR(wicfgs.bssid));
                        else tcp_put('?');
                    };
            	};
            };
          };
        }
        else ifcmp("uart_") {
            cstr+=5;
            volatile uint32 * uart_reg = uart0_;
            if(cstr[1] != '_' || cstr[0]<'0' || cstr[0]>'1' ) {
            	tcp_put('?');
            	return;
            }
            if(cstr[0] != '0') uart_reg = uart1_;
            cstr += 2;
            ifcmp("baud") tcp_puts("%u", (uint32) UART_CLK_FREQ / (uart_reg[IDX_UART_CLKDIV] & UART_CLKDIV_CNT));
//            else ifcmp("reg")	tcp_puts("%08x", uart_reg[hextoul(cstr + 3)]);
            else ifcmp("parity") tcp_put((uart_reg[IDX_UART_CONF0] & UART_PARITY_EN)? '1':'0');
            else ifcmp("even") tcp_put((uart_reg[IDX_UART_CONF0] & UART_PARITY)? '1' : '0');
            else ifcmp("bits") tcp_puts("%u", (uart_reg[IDX_UART_CONF0] >> UART_BIT_NUM_S) & UART_BIT_NUM);
            else ifcmp("stop") tcp_puts("%u",(uart_reg[IDX_UART_CONF0] >> UART_STOP_BIT_NUM_S) & UART_STOP_BIT_NUM);
            else ifcmp("loopback") tcp_put((uart_reg[IDX_UART_CONF0] & UART_LOOPBACK)? '1' : '0');
            else ifcmp("flow") {
            	if(uart_reg == uart0_) tcp_put((uart0_flow_ctrl_flg)? '1' : '0');
            	else tcp_put((uart_reg[IDX_UART_CONF0]&UART_RX_FLOW_EN)? '1' : '0');
            }
        	else ifcmp("swap") {
        		tcp_put((PERI_IO_SWAP & ((uart_reg == uart0_)? PERI_IO_UART0_PIN_SWAP : PERI_IO_UART1_PIN_SWAP))? '1' : '0');
        	}
            else ifcmp("rts_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_RTS_INV)? '1' : '0');
            else ifcmp("cts_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_CTS_INV)? '1' : '0');
            else ifcmp("rxd_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_RXD_INV)? '1' : '0');
            else ifcmp("txd_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_TXD_INV)? '1' : '0');
            else ifcmp("dsr_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_DSR_INV)? '1' : '0');
            else ifcmp("dtr_inv") tcp_put((uart_reg[IDX_UART_CONF0]&UART_DTR_INV)? '1' : '0');
            else tcp_put('?');
        }
        else ifcmp("bin_") {
        	cstr+=4;
        	ifcmp("flash") {
        		cstr+=5;
        		if(*cstr == '_') {
        			cstr++;
            		ifcmp("all") {
            	    	web_conn->udata_start = 0;
            	    	web_conn->udata_stop = spi_flash_real_size();
            	    	web_get_flash(ts_conn);
            		}
            		else ifcmp("sec_") {
            			web_conn->udata_start = ahextoul(cstr+4) << 12;
            			web_conn->udata_stop = web_conn->udata_start + SPI_FLASH_SEC_SIZE;
            			web_get_flash(ts_conn);
            		}
            		else ifcmp("const") {
            	    	web_conn->udata_start = faddr_esp_init_data_default;
            	    	web_conn->udata_stop = web_conn->udata_start + SIZE_SYS_CONST;
            	    	web_get_flash(ts_conn);
            		}
            		else ifcmp("disk") {
            			web_conn->udata_start = WEBFS_base_addr();
            			web_conn->udata_stop = web_conn->udata_start + WEBFS_curent_size();
            			web_get_flash(ts_conn);
            		}
            		else tcp_put('?');
        		}
        		else web_get_flash(ts_conn);
        	}
        	else ifcmp("ram") web_get_ram(ts_conn);
#ifdef USE_MODBUS
        	else ifcmp("mdb") {
    			web_conn->udata_start = (uint32) &mdb_buf;
    			web_conn->udata_stop = web_conn->udata_start + sizeof(mdb_buf);
        		web_get_ram(ts_conn);
        	}
#endif
        	// history.csv, historycnt.csv
        	else ifcmp("history") {
        		cstr += 7;
        		web_conn->udata_start = 0;
				web_conn->udata_start = (Web_ShowBy<<1) | Web_ShowByKWT;	// OutType
        		ifcmp("cnt") {
        			web_conn->udata_start = (web_conn->udata_start & ~HST_ByHour) | HST_TotalCnt;
        			if(cfg_glo.TimeT1Start || cfg_glo.TimeT1End) web_conn->udata_start |= HST_MTariffs; // Multi tariffs
        		}
				web_conn->udata_stop = Web_ChartMaxDays * 60*24; 	// how many minutes, 0 = all
				web_get_history(ts_conn);
        	}
        	// fram_all.bin
        	else ifcmp("fram_all") {
    			web_conn->udata_start = 0;
    			web_conn->udata_stop = cfg_glo.Fram_Size;
    			web_get_i2c_eeprom(ts_conn);
        	}
        	//
        	else tcp_put('?');
        }
        else ifcmp("hexdmp") {
        	if(cstr[6]=='d') ts_conn->flag.user_option1 = 1;
        	else ts_conn->flag.user_option1 = 0;
        	web_hexdump(ts_conn);
        }
        else ifcmp("hellomsg") tcp_puts_fd("Power Meter");
#ifdef USE_TCP2UART
        else ifcmp("tcp_") {
        	cstr+=4;
#if 0
        	ifcmp("connected") {
        		if(tcp2uart_conn == NULL) tcp_put('0');
        		else tcp_put('1');
        	}
        	else
#endif
        	ifcmp("remote") {
        		if(uart_drv.uart_rx_buf != NULL && tcp2uart_conn != NULL) {
	        		tcp_puts(IPSTR ":%d", IP2STR(&(tcp2uart_conn->remote_ip.dw)), tcp2uart_conn->remote_port);
        		}
        		else tcp_strcpy_fd("closed");
        	}
        	else ifcmp("host") {
        		if(uart_drv.uart_rx_buf != NULL && tcp2uart_conn != NULL && tcp2uart_conn->pcb != NULL) tcp_puts(IPSTR ":%d", IP2STR(&(tcp2uart_conn->pcb->local_ip.addr)), tcp2uart_conn->pcb->local_port);
        		else tcp_strcpy_fd("none");
        	}
#if 0
       		else ifcmp("twrec") {
       			if(tcp2uart_conn!=NULL) tcp_puts("%u", tcp2uart_conn->pcfg->time_wait_rec);
       			else tcp_strcpy_fd("closed");
       		}
       		else ifcmp("twcls") {
       			if(tcp2uart_conn!=NULL) tcp_puts("%u", tcp2uart_conn->pcfg->time_wait_cls);
       			else tcp_strcpy_fd("closed");
       		}
#endif
       		else tcp_put('?');
        }
#endif // USE_TCP2UART
        else ifcmp("web_") {
        	cstr+=4;
#if 0
        	ifcmp("port") tcp_puts("%u", ts_conn->pcfg->port);
        	else
#endif
        		ifcmp("host") tcp_puts(IPSTR ":%d", IP2STR(&(ts_conn->pcb->local_ip.addr)), ts_conn->pcb->local_port);
        	else ifcmp("remote") tcp_puts(IPSTR ":%d", IP2STR(&(ts_conn->remote_ip.dw)), ts_conn->remote_port);
#if 0
        	else ifcmp("twrec") tcp_puts("%u", ts_conn->pcfg->time_wait_rec);
        	else ifcmp("twcls") tcp_puts("%u", ts_conn->pcfg->time_wait_cls);
#endif
        	else tcp_put('?');
        }
#ifdef USE_MODBUS
        else ifcmp("mdb_") {
        	cstr+=4;
       		ifcmp("host") {
       			if(mdb_tcp_servcfg!= NULL && mdb_tcp_servcfg->conn_links != NULL)
       				tcp_puts(IPSTR ":%d", IP2STR(&(mdb_tcp_servcfg->conn_links->pcb->local_ip.addr)), mdb_tcp_servcfg->conn_links->pcb->local_port);
       			else tcp_strcpy_fd("none");
       		}
        	else ifcmp("remote") {
        		if(mdb_tcp_servcfg!= NULL && mdb_tcp_servcfg->conn_links != NULL)
        			tcp_puts(IPSTR ":%d", IP2STR(&(mdb_tcp_servcfg->conn_links->remote_ip.dw)), mdb_tcp_servcfg->conn_links->remote_port);
        		else tcp_strcpy_fd("closed");
        	}
        	else tcp_put('?');
        }
#endif
#ifdef USE_RS485DRV
        else ifcmp("rs485_") {
        	cstr+=6;
        	ifcmp("baud") tcp_puts("%u", rs485cfg.baud);
        	else ifcmp("timeout") tcp_puts("%u", rs485cfg.timeout);
           	else ifcmp("pause") tcp_puts("%u", rs485cfg.pause);
        	else ifcmp("parity") tcp_puts("%u", rs485cfg.flg.b.parity);
        	else ifcmp("pinre") tcp_puts("%u", rs485cfg.flg.b.pin_ena);
        	else ifcmp("swap") tcp_put((rs485cfg.flg.b.swap)? '1' : '0');
        	else ifcmp("spdtw") tcp_put((rs485cfg.flg.b.spdtw)? '1' : '0');
        	else ifcmp("master") tcp_put((rs485cfg.flg.b.master)? '1' : '0');
        	else ifcmp("enable") tcp_put((rs485vars.status != RS485_TX_RX_OFF)? '1' : '0');
        	else tcp_put('?');
        }
#endif

        else ifcmp("wfs_") {
        	cstr+=4;
        	ifcmp("files") tcp_puts("%u", numFiles);
        	else ifcmp("addr") tcp_puts("0x%08x", WEBFS_base_addr());
        	else ifcmp("size") tcp_puts("%u", WEBFS_curent_size());
        	else ifcmp("max_size") tcp_puts("%u", WEBFS_max_size());
        	else tcp_put('?');
        }
        else ifcmp("gpio") {
            cstr+=4;
        	if((*cstr>='0')&&(*cstr<='9')) {
        		uint32 n = atoi(cstr);
        		cstr++;
        		if(*cstr != '_') cstr++;
        		if(*cstr == '_' && n < 16) {
        			cstr++;
        			ifcmp("inp") {
        				if(GPIO_IN & (1<<n)) tcp_put('1');
        				else tcp_put('0');
        			}
        			else ifcmp("out") {
        				if(GPIO_OUT &(1<<n)) tcp_put('1');
        				else tcp_put('0');
        			}
        			else ifcmp("dir") {
        				if(GPIO_ENABLE & (1<<n)) tcp_put('1');
        				else tcp_put('0');
        			}
        			else ifcmp("fun")	tcp_puts("%u", get_gpiox_mux_func(n));
        			else ifcmp("pull")	tcp_puts("%u", (get_gpiox_mux(n) >> GPIO_MUX_PULLDOWN_BIT) & 3);
    	            else ifcmp("opd")	tcp_put((GPIOx_PIN(n) & (1<<GPIO_PIN_DRIVER)) ? '1' : '0');
    	            else ifcmp("pu")	tcp_put((get_gpiox_mux(n) & (1 << GPIO_MUX_PULLUP_BIT)) ? '1' : '0');
    	            else ifcmp("pd")	tcp_put((get_gpiox_mux(n) & (1 << GPIO_MUX_PULLDOWN_BIT)) ? '1' : '0');
        			else tcp_put('?');
        		}
        		else tcp_put('?');
        	}
        	else if(*cstr == '_') {
        		cstr++;
        		ifcmp("inp") tcp_puts("%u", GPIO_IN & 0xFFFF);
        		else ifcmp("out") tcp_puts("%u", GPIO_OUT & 0xFFFF);
        		else ifcmp("dir") tcp_puts("%u", GPIO_ENABLE & 0xFFFF);
            	else tcp_put('?');
        	}
        	else tcp_put('?');
        }
#ifdef USE_OVERLAY
		else ifcmp("ovl") {
			cstr += 3;
			if(*cstr == ':') {
				int i = ovl_loader(cstr + 1);
				if (i == 0) {
					if(CheckSCB(SCB_WEBSOC)) {
						tcp_puts("%d", ovl_call(1));
					}
					else {
						web_conn->web_disc_cb = (web_func_disc_cb)ovl_call; // адрес старта оверлея
						web_conn->web_disc_par = 1; // параметр функции - инициализация
					}
				}
				tcp_puts("%d", i);
			}
			else if(*cstr == '$') {
    			if(ovl_call != NULL) tcp_puts("%d", ovl_call(ahextoul(cstr + 1)));
    			else tcp_put('?');
    		}
			else if(*cstr == '@') {
    			if(ovl_call != NULL) tcp_puts("%d", ovl_call((int) cstr + 1));
    			else tcp_put('?');
    		}
			else tcp_put('?');
		}
#endif
#ifdef USE_SNTP
		else ifcmp("sntp_") {
			cstr += 5;
			ifcmp("time") tcp_puts("%u", get_sntp_time());
			else tcp_put('?');
		}
#endif
#ifdef TEST_SEND_WAVE
        else ifcmp("test_adc") web_test_adc(ts_conn);
#endif
// PowerMeter
        else ifcmp("PowerCnt") tcp_puts("%u", fram_store.PowerCnt);
        else ifcmp("TotalCntTime") tcp_puts("%u", sntp_local_to_UTC_time(fram_store.LastTime));
        else ifcmp("TotalCntT1") tcp_puts("%u", fram_store.TotalCntT1);
        else ifcmp("TotalCntT2") tcp_puts("%u", fram_store.TotalCnt - fram_store.TotalCntT1);
        else ifcmp("TotalCnt") tcp_puts("%u", fram_store.TotalCnt);
        else ifcmp("LastCnt") {
        	cstr += 7;
        	tcp_puts("%u", LastCnt);
        	ifcmp("_Fill") { // only non zero value
        		if(LastCnt_Previous == 0 && LastCnt == 0) web_conn->webflag |= SCB_USER; // do not send data to IoT cloud
        		LastCnt_Previous = LastCnt;
        	}
        }
        else ifcmp("PtrCurrent") tcp_puts("%u", fram_store.PtrCurrent);
        else ifcmp("CntCurrent1") tcp_puts("%u", CntCurrent.Cnt1);
        else ifcmp("CntCurrent2") tcp_puts("%u", CntCurrent.Cnt2);
        else ifcmp("TotalKWT") { // TotalKWT, TotalKWTT1, TotalKWTT2; TotalKWT_New, TotalKWT_NewT1, TotalKWT_NewT2
        	cstr += 8;
        	ifcmp("_New") { // only new value
        		if(fram_store.TotalCnt == KWT_Previous) web_conn->webflag |= SCB_USER; // do not send data to IoT cloud;
        		KWT_Previous = fram_store.TotalCnt;
        		cstr += 4;
        	}
        	typeof(fram_store.TotalCnt) cnt;
        	cnt = rom_xstrcmp(cstr, "T1") ? fram_store.TotalCntT1 : rom_xstrcmp(cstr, "T2") ? fram_store.TotalCnt - fram_store.TotalCntT1 : fram_store.TotalCnt;
        	tcp_puts("%u.%03u", cnt / (cfg_glo.PulsesPer0_01KWt * 100), (cnt * 10 / cfg_glo.PulsesPer0_01KWt) % 1000);
        }
#ifdef USE_I2C
        else ifcmp("i2c_errors") tcp_puts("%u", I2C_EEPROM_Error);
#endif
        else ifcmp("ChartMaxDays") tcp_puts("%u", Web_ChartMaxDays);
        else ifcmp("ShowByKWT") tcp_puts("%d", Web_ShowByKWT);
        else ifcmp("ShowBy") tcp_puts("%d", Web_ShowBy);
        else ifcmp("iot_") {
        	cstr += 4;
            ifcmp("LastSt_time") tcp_puts("%u", iot_last_status_time);
            else ifcmp("LastSt") tcp_puts("%s", iot_last_status);
        }
#ifdef DEBUG_TO_RAM
        else ifcmp("dbg_") { // debug to RAM
        	cstr += 4;
        	ifcmp("enable") tcp_puts("%d", Debug_level);
        	else ifcmp("addr") tcp_puts("0x%x", (uint32)Debug_RAM_addr);
        	else ifcmp("size") tcp_puts("%u", Debug_RAM_size);
        	else ifcmp("len") tcp_puts("%u", Debug_RAM_len);
        	else ifcmp("ram") dbg_tcp_send(ts_conn);
        }
#endif
        else ifcmp("mktime") tcp_puts("%s %s", __DATE__, __TIME__); // make date time
// PowerMeter
		else tcp_put('?');
}

#endif // USE_WEB
