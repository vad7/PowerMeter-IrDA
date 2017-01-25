/*
 * Mercury 230, 231AT power meter driver (http://www.incotexcom.ru/counters.htm)
 *
 * Use IrDa interface (UART0) at 9600b
 *
 * Written by Vadim Kulakov (c) vad7@yahoo.com
 *
 */
#include "user_config.h"
#ifdef USE_MERCURY
#include "bios.h"
#include "sdk/add_func.h"
#include "hw/esp8266.h"
#include "user_interface.h"
#include "tcp2uart.h"
#include "mercury.h"

uint8	pwmt_buffer[20];
uint8	pwmt_request_code = 0;
uint8	pwmt_request_param;
uint8	pwmt_response_len;
uint32	pwmt_request_time;
os_timer_t uart_receive_timer DATA_IRAM_ATTR;

#define PWMT_READ_TIMEOUT		5000 // us
#define PWMT_RESPONSE_TIMEOUT	150000 // us
// X0h..X5h
static const uint8 response_text_1[] ICACHE_RODATA_ATTR	= "Недопустимая команда или параметр";
static const uint8 response_text_2[] ICACHE_RODATA_ATTR	= "Внутренняя ошибка";
static const uint8 response_text_3[] ICACHE_RODATA_ATTR	= "Низкий уровень доступа";
static const uint8 response_text_4[] ICACHE_RODATA_ATTR	= "Часы сегодня уже корректировались";
static const uint8 response_text_5[] ICACHE_RODATA_ATTR	= "Не открыт канал связи";
static const uint8 response_text_6[] ICACHE_RODATA_ATTR	= "Ошибка CRC";
static const uint8 * const response_text_array[] ICACHE_RODATA_ATTR = { response_text_1, response_text_2, response_text_3, response_text_4, response_text_5, response_text_6 };
static const uint8 connect_user[] ICACHE_RODATA_ATTR = { 0,1,1, 1,1,1,1,1,1, 0x77,0x81 };
static const uint8 cmd_get_W[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x00 };
static const uint8 cmd_get_U[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x10 };
static const uint8 cmd_get_I[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x20 };
static const uint8 cmd_get_K[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x30 };

typedef enum
{
	URT_STATUS = 0, // 1 byte answer (1..5)
	URT_2B,		// 2 Bytes
	URT_3N,		// Numeric 22bites, 3 Bytes (1B, 3B, 2B) (nnnnnnn)
	URT_4N,		// Numeric 30bites, 4 Bytes (2B, 1B, 4B, 3B) (nnnnnnnnnn)
	URT_TIME,	// 6 bytes (BCD): сек, мин, час, число, месяц, год
	URT_CURTIME,// 8 bytes (BCD): cек, мин, час, день недели, число, месяц, год, зима(1)/лето(0)
	URT_STR		// char array
} UART_RECORD_TYPE;
typedef struct {
	const uint8 *data_send;
	void  *receive_var;
	uint8 type;	// UART_RECORD_TYPE
	uint8 size;
} UART_REQUEST_RECORD;

#define cmd_get_current_array_element_size 3
static const UART_REQUEST_RECORD cmd_get_current_array[] ICACHE_RODATA_ATTR = { {cmd_get_W, pwmt_cur.W, URT_3N, 4}, {cmd_get_U, pwmt_cur.U, URT_3N, 3},
		{cmd_get_I, pwmt_cur.I, URT_3N, 3}, {cmd_get_K, pwmt_cur.K, URT_3N, 3} };

// CRC16
uint16 		crc_tab16[256];
//static const uint16 crc_tab16[256] ICACHE_RODATA_ATTR = {
#define		CRC_START_MODBUS	0xFFFF
#define		CRC_POLY_16			0xA001
// https://www.lammertbies.nl/comm/info/crc-calculation.html
uint16_t ICACHE_FLASH_ATTR crc_modbus(uint8 *input_str, size_t num_bytes ) {
	uint16_t crc, tmp, short_c;
	uint8 *ptr;
	size_t a;
	crc = CRC_START_MODBUS;
	ptr = input_str;
	if ( ptr != NULL ) for (a=0; a<num_bytes; a++) {
		short_c = 0x00ff & (uint16_t) *ptr;
		tmp     =  crc       ^ short_c;
		crc     = (crc >> 8) ^ crc_tab16[ tmp & 0xff ];
		ptr++;
	}
	return crc;
}

void ICACHE_FLASH_ATTR  init_crc16_tab( void ) {
	uint16_t i, j, crc, c;
	for (i=0; i<256; i++) {
		crc = 0;
		c   = i;
		for (j=0; j<8; j++) {
			if ( (crc ^ c) & 0x0001 ) crc = ( crc >> 1 ) ^ CRC_POLY_16;
			else                      crc =   crc >> 1;
			c = c >> 1;
		}
		crc_tab16[i] = crc;
	}
//	os_printf("\nCRC-16 Modbus:\n");
//	for(i = j = 0; i < 256; i++) {
//		os_printf(" %02X,", crc_tab16[i]);
//		if(++j == 32) os_printf("\n");
//	}
}

void ICACHE_FLASH_ATTR uart_recvTask(os_event_t *events)
{
    if(events->sig == 0){
		#if DEBUGSOO > 4
    		os_printf("%s\n", UART_Buffer);
		#endif
    	if(uart_queue_len && (uart_queue[0].flag | UART_RESPONSE_WAITING)) {
    		uart_queue[0].time = system_get_time();
    		uart_queue[0].flag = UART_RESPONSE_READING;
    	}
    }
}

void ICACHE_FLASH_ATTR pwmt_prepare_send(const uint8 * data, uint8 len)
{
	uint8 *p = uart_queue[uart_queue_len].buffer;
	p[0] = pwmt_address;
	os_memcpy(&p + 1, data, len++);
	uint16_t crc = crc_modbus(p, len);
	p[len++] = crc & 0xFF;
	p[len] = crc >> 8;
	uart_queue[uart_queue_len].len = len + 1;
	uart_queue[uart_queue_len].flag = UART_SEND_WAITING;
	uart_queue_len++;
}

void ICACHE_FLASH_ATTR pwmt_connect(void)
{
	if(pwmt_connect_status != PWMT_NOT_CONNECTED) return;
	uart_queue_len = 0;
	pwmt_prepare_send(connect_user, sizeof(connect_user));
	pwmt_connect_status = PWMT_CONNECTING;
}

void ICACHE_FLASH_ATTR pwmt_uart_queue_next(void)
{
	if(uart_queue_len > 1)
		os_memcpy(&uart_queue[0], &uart_queue[1], sizeof(cmd_get_current_array) - sizeof(cmd_get_current_array[0]));
	uart_queue_len--;
}

void ICACHE_FLASH_ATTR pwmt_send_to_uart(void)
{
	if(uart_queue_len && uart_queue[0].flag <= UART_SEND_WAITING) {
		if(uart_queue_len > 1 && uart_queue[0].flag == UART_DELETED) {
			pwmt_uart_queue_next();
			if(uart_queue[0].flag != UART_SEND_WAITING) return;
		}
		uart_drv_start();
		UART_Buffer_idx = 0;
		if(uart_tx_buf(uart_queue[0].buffer, uart_queue[0].len) == uart_queue[0].len) {
			uart_queue[0].flag = UART_RESPONSE_WAITING;
			uart_queue[0].time = system_get_time();
		}
	}
}

void ICACHE_FLASH_ATTR pwmt_read_current(void)
{
	if(pwmt_connect_status == PWMT_NOT_CONNECTED) {
		pwmt_connect();
	}
	if(UART_QUEUE_IDX_MAX - uart_queue_len < sizeof(cmd_get_current_array)) return; // overload
	uint8 autosend = !uart_queue_len;
	uint8 i;
	for(i = 0; i < sizeof(cmd_get_current_array) / sizeof(cmd_get_current_array[0]); i++) {
		uart_queue[uart_queue_len].receive_var = cmd_get_current_array[i].receive_var;
		uart_queue[uart_queue_len].type = cmd_get_current_array[i].type;
		uart_queue[uart_queue_len].size = cmd_get_current_array[i].size;
		pwmt_prepare_send(cmd_get_current_array[i].data_send, cmd_get_current_array_element_size);

	}
	if(autosend) pwmt_send_to_uart();
}

// type = 0 -> 3 bytes, 1 -> 4 bytes
void ICACHE_FLASH_ATTR uart_receive_get_number(uint32 *var, uint8 b4, uint8 array_size)
{
	uint8 i, *p = UART_Buffer + 1;
	for(i = 0; i < array_size; i++) {
		var++[i] = b4 ? (p[0] << 16) + (p[1] << 24) + p[2] + (p[3] << 8) : (p[0] << 16) + p[1] + (p[2] << 8);
		p += 3 + b4;
	}
}

void ICACHE_FLASH_ATTR uart_receive_timer_func(void) // call every 2 msec
{
	if(uart_queue_len) {
		uint8 fl = uart_queue[0].flag;
		if(fl | UART_RESPONSE_WAITING) {
			if(system_get_time() - uart_queue[0].time >= (fl == UART_RESPONSE_WAITING ? PWMT_RESPONSE_TIMEOUT : PWMT_READ_TIMEOUT)) {
				uart_queue[0].flag = UART_RESPONSE_READY;
				if(UART_Buffer_idx >= 4 && (crc_modbus(UART_Buffer, UART_Buffer_idx - 2) == UART_Buffer[UART_Buffer_idx - 1] * 256 + UART_Buffer[UART_Buffer_idx - 2])) {
					#if DEBUGSOO > 4
						os_printf("UART: ");
						uint8 jjj;
						for(jjj = 0; jjj < uart_queue[0].len; jjj++) {
							os_printf("%02X ", uart_queue[0].buffer[jjj]);
						}
						os_printf(" => ");
						for(jjj = 0; jjj < UART_Buffer_idx; jjj++) {
							os_printf("%02X ", UART_Buffer[jjj]);
						}
						os_printf("\n");
					#endif
					uart_queue[0].flag = UART_RESPONSE_READY_OK;
					//os_memcpy(uart_queue[0].buffer, UART_Buffer, UART_Buffer_idx);
				} else { // error
					//os_memset(uart_queue[0].buffer, 0, sizeof(uart_queue[0].buffer));
					#if DEBUGSOO > 4
						os_printf("Err UART %d\n", UART_Buffer_idx);
					#else
						dbg_printf("E_U %d\n", UART_Buffer_idx);
					#endif
					pwmt_last_response = 6; // CRC error
					uart_queue[0].flag = UART_DELETED;
					pwmt_send_to_uart();
				}
			}
		} else if(fl == UART_RESPONSE_READY_OK) {
			UART_QUEUE *uq = &uart_queue[0];
			pwmt_last_response = UART_Buffer[1];
			if(pwmt_connect_status == PWMT_CONNECTING) {
				if(pwmt_last_response == 0) { // ok
					pwmt_connect_status = PWMT_CONNECTED;
					pwmt_uart_queue_next();
				} else {
					pwmt_connect_status = PWMT_NOT_CONNECTED;
					uart_queue_len = 0;
				}
			} else if(uq->type == URT_2B) {
				os_memcpy(uq->receive_var, UART_Buffer + 1, 2);
			} else if(uq->type == URT_3N || uq->type == URT_4N) {
				uart_receive_get_number(uq->receive_var, uq->type == URT_3N ? 0 : 1, uq->size);
			} else if(uq->type == URT_CURTIME) {




			}
		}
	}
}

void ICACHE_FLASH_ATTR irda_init(void)
{
	uarts_init();
	init_crc16_tab();
	pwmt_connect_status = PWMT_NOT_CONNECTED;
	ets_timer_disarm(&uart_receive_timer);
	os_timer_setfn(&uart_receive_timer, (os_timer_func_t *)uart_receive_timer_func, NULL);
	ets_timer_arm_new(&uart_receive_timer, 2, 1, 1); // 2ms, repeat
}

#endif

