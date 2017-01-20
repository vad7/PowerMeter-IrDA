/*
 * Mercury power meter driver (http://www.incotexcom.ru/counters.htm)
 *
 * Use IrDa interface (UART0) at 9600b
 *
 * Written by vad7
 */
#include "user_config.h"
#ifdef USE_MERCURY
#include "mercury.h"

uint8	pwmt_buffer[20];
uint8	pwmt_request_code = 0;
uint8	pwmt_request_param;
uint8	pwmt_response_len;
uint32	pwmt_request_time;
os_timer_t uart_receive_timer DATA_IRAM_ATTR;

#define PWMT_REQUEST_TIMEOUT	5000 // us
#define PWMT_RESPONSE_TIMEOUT	150000 // us
// X0h..X5h
static const uint8 response_text_1[] ICACHE_RODATA_ATTR	= "Недопустимая команда или параметр";
static const uint8 response_text_2[] ICACHE_RODATA_ATTR	= "Внутренняя ошибка счетчика";
static const uint8 response_text_3[] ICACHE_RODATA_ATTR	= "Не достаточен уровень доступа";
static const uint8 response_text_4[] ICACHE_RODATA_ATTR	= "Часы счетчика уже корректировались в течение суток";
static const uint8 response_text_5[] ICACHE_RODATA_ATTR	= "Не открыт канал связи";
static const uint8 * const response_text_array[] ICACHE_RODATA_ATTR = { response_text_1, response_text_2, response_text_3, response_text_4, response_text_5 };
static const uint8 connect_user[] = { 0,1,1, 1,1,1,1,1,1, 0x77,0x81 };
static const uint8 cmd_get_current[] = { 0, 8, 0x11 };
//#define cmd_get_W1 0x01
//#define cmd_get_W2 0x02
//#define cmd_get_W3 0x03
//#define cmd_get_U1 0x11
//#define cmd_get_U2 0x12
//#define cmd_get_U3 0x13
//#define cmd_get_I1 0x21
//#define cmd_get_I2 0x22
//#define cmd_get_I3 0x23
//#define cmd_get_K1 0x31
//#define cmd_get_K2 0x32
//#define cmd_get_K3 0x33
static const uint8 cmd_get_W1[] = { 8, 0x11, 0x01 };
static const uint8 cmd_get_W2[] = { 8, 0x11, 0x02 };
static const uint8 cmd_get_W3[] = { 8, 0x11, 0x03 };
static const uint8 cmd_get_U1[] = { 8, 0x11, 0x11 };
static const uint8 cmd_get_U2[] = { 8, 0x11, 0x12 };
static const uint8 cmd_get_U3[] = { 8, 0x11, 0x13 };
static const uint8 cmd_get_I1[] = { 8, 0x11, 0x21 };
static const uint8 cmd_get_I2[] = { 8, 0x11, 0x22 };
static const uint8 cmd_get_I3[] = { 8, 0x11, 0x23 };
static const uint8 cmd_get_K1[] = { 8, 0x11, 0x31 };
static const uint8 cmd_get_K2[] = { 8, 0x11, 0x32 };
static const uint8 cmd_get_K3[] = { 8, 0x11, 0x33 };
static const uint32 *cmd_get_current_array[] = {cmd_get_W1, cmd_get_W2, cmd_get_W3, cmd_get_U1, cmd_get_U2, cmd_get_U3, cmd_get_I1, cmd_get_I2, cmd_get_I3, cmd_get_K1, cmd_get_K2, cmd_get_K3};

// CRC16
uint16 		crc_tab16[256];
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
	crc_tab16_init = true;
}

void ICACHE_FLASH_ATTR uart_recvTask(os_event_t *events)
{
    if(events->sig == 0){
		#if DEBUGSOO > 4
    		os_printf("%s\n", UART_Buffer);
		#endif
    	if(uart_queue_len && uart_queue[0].flag == UART_WAITING_RESPONSE) {
    		uart_queue[0].time = system_get_time();
    	}
    	if(UART_Buffer_idx == pwmt_response_len) {

    		UART_Buffer[UART_Buffer_size-1] = 0;
    		uint8 *p = web_strnstr(UART_Buffer, AZ_7798_ResponseEnd, UART_Buffer_idx);
    		if(p == NULL) return;
   			*p = 0;
   			if((p = os_strchr(UART_Buffer, AZ_7798_TempStart)) == NULL) return;
   			p++;
			uint8 *p2 = os_strchr(p, AZ_7798_TempEnd);
			if(p2 == NULL) return;
			*p2 = 0;
			Temperature = atoi_z(p, 1);
			p = p2 + 3;
			if((p2 = os_strchr(p, AZ_7798_CO2End)) != NULL) {
				*p2 = 0;
				CO2level = atoi_z(p, 1);
				p = p2 + 5;
				Humidity = atoi_z(p, 1);
				ProcessIncomingValues();
				CO2_work_flag = 1;
				receive_timeout = 0;
			}
    	}
    }
}


void ICACHE_FLASH_ATTR pwmt_prepare_send(uint8 * data, uint8 len)
{
	uint8 *p = &uart_queue[uart_queue_len].buffer;
	p[0] = pwmt_address;
	os_memcpy(&p + 1, data, len++);
	uint16_t crc = crc_modbus(p, len);
	p[len++] = crc & 0xFF;
	p[len] = crc >> 8;
	uart_queue[uart_queue_len].flag = UART_WAITING_SEND;
	uart_queue_len++;
}

void ICACHE_FLASH_ATTR pwmt_read_current(void)
{
	if(pwmt_connect_status == 0) {
		pwmt_connect();
	}
	if(UART_QUEUE_IDX_MAX - uart_queue_len < sizeof(cmd_get_current_array)) return; // overload
	uint8 i;
	for(i = 0; i < sizeof(cmd_get_current_array) / sizeof(cmd_get_current_array[0]); i++) {
		pwmt_prepare_send(cmd_get_current_array[i], sizeof(cmd_get_W1));
	}
}

void ICACHE_FLASH_ATTR pwmt_send_to_uart(void)
{
	if(uart_queue_len && uart_queue[0].flag <= UART_WAITING_SEND) {
		if(uart_queue_len > 1 && uart_queue[0].flag == UART_DELETED) {
			os_memcpy(&uart_queue[0], &uart_queue[1], sizeof(cmd_get_current_array) - sizeof(cmd_get_current_array[0]));
			if(uart_queue[0].flag != UART_WAITING_SEND) return;
		}
		uart_drv_start();
		if(uart_tx_buf(uart_queue[0].buffer, sizeof(uart_queue[0].buffer)) == sizeof(uart_queue[0].buffer)) {
			uart_queue[0].flag = UART_WAITING_RESPONSE;
			uart_queue[0].time = system_get_time();
		}
	}
}

void ICACHE_FLASH_ATTR pwmt_connect(void)
{
	if(pwmt_connect_status) return;
	pwmt_prepare_send(connect_user, sizeof(connect_user));
	pwmt_connect_status = 1;
}

void ICACHE_FLASH_ATTR uart_receive_timer_func(void) // call every 5 msec
{
	if(uart_queue_len && uart_queue[0].flag == UART_WAITING_RESPONSE && uart_queue[0].time < ) {
		uart_queue[0].time = system_get_time();
	}
}

void ICACHE_FLASH_ATTR irda_init(void)
{
	uarts_init();
	init_crc16_tab();
	pwmt_connect_status = 0;
	ets_timer_disarm(&uart_receive_timer);
	os_timer_setfn(&uart_receive_timer, (os_timer_func_t *)uart_receive_timer_func, NULL);
	ets_timer_arm_new(&uart_receive_timer, 5, 1, 1); // 1s, repeat
}

#endif
