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
#include "stdlib.h"
#include "bios.h"
#include "hw/esp8266.h"
#include "hw/pin_mux_register.h"
#include "hw/uart_register.h"
#include "sdk/add_func.h"
#include "sdk/rom2ram.h"
#include "user_interface.h"
#include "tcp2uart.h"
#include "mercury.h"
#include "localtime.h"
#include "power_meter.h"
#include "crc.h"
#include "driver/i2s.h"
void uart_wait_tx_fifo_empty(void) ICACHE_FLASH_ATTR;

uint8	pwmt_buffer[20];
uint8	pwmt_request_code = 0;
uint8	pwmt_request_param;
uint8	pwmt_response_len;
uint32	pwmt_request_time;
os_timer_t uart_receive_timer DATA_IRAM_ATTR;
os_timer_t pwmt_request_timer DATA_IRAM_ATTR;

#define TimeMismatchMaxForCorrect 230 // sec
// X0h..X5h
//static const uint8 response_text_1[] ICACHE_RODATA_ATTR	= "Недопустимая команда или параметр";
//static const uint8 response_text_2[] ICACHE_RODATA_ATTR	= "Внутренняя ошибка";
//static const uint8 response_text_3[] ICACHE_RODATA_ATTR	= "Низкий уровень доступа";
//static const uint8 response_text_4[] ICACHE_RODATA_ATTR	= "Часы сегодня уже корректировались";
//static const uint8 response_text_5[] ICACHE_RODATA_ATTR	= "Не открыт канал связи";
//static const uint8 response_text_6[] ICACHE_RODATA_ATTR	= "Нет ответа";
//static const uint8 response_text_7[] ICACHE_RODATA_ATTR	= "Ошибка CRC";
//static const uint8 * const response_text_array[] ICACHE_RODATA_ATTR = { response_text_1, response_text_2, response_text_3, response_text_4, response_text_5, response_text_6 };
// current array, first element size = 2, next - size = 3
#define cmd_get_current_array_element_size 	 	3
static const uint8 cmd_get_Total[] ICACHE_RODATA_ATTR = { 5, 0x60, 0x00 };
static const uint8 cmd_get_Total_T1[] ICACHE_RODATA_ATTR = { 5, 0x60, 0x01 };
static const uint8 cmd_get_P[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x00 };
static const uint8 cmd_get_S[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x08 };
static const uint8 cmd_get_U[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x11 };
static const uint8 cmd_get_I[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x21 };
static const uint8 cmd_get_K[] ICACHE_RODATA_ATTR = { 8, 0x16, 0x30 };
#define cmd_get_current_array_element_last_size 2 // for cmd_get_Time
static const uint8 cmd_get_Time[] ICACHE_RODATA_ATTR = { 4, 0x00 };
// current array.
uint8 cmd_get_archive[] = { 5, 0, 0 };
// second byte
#define cmd_get_arch_Today 		0x40
#define cmd_get_arch_Yesterday 	0x50
#define cmd_get_arch_Month		0x30 // number of month third byte
#define cmd_get_arch_ThisYear 	0x10
#define cmd_get_arch_PrevYear 	0x20
//
#define cmd_connect				0x01
#define cmd_write 				0x03
#define cmd_read_arrays			0x04
#define cmd_code_set_time 		0x0C
#define cmd_code_correct_time	0x0D
uint8 cmd_buffer[10];

typedef struct {
	const uint8 *data_send;
	void  *receive_var;
	uint32 type; // UART_RECORD_TYPE
	uint32 size; // receive_var array size
} UART_REQUEST_RECORD;

static const UART_REQUEST_RECORD cmd_get_current_array[] ICACHE_RODATA_ATTR = {
		{cmd_get_P, &pwmt_cur.P, URT_3N, 4}, {cmd_get_S, &pwmt_cur.S, URT_3N, 4},
		{cmd_get_U, &pwmt_cur.U, URT_3N, 3}, {cmd_get_I, &pwmt_cur.I, URT_3N, 3}, {cmd_get_K, &pwmt_cur.K, URT_3N, 4},
		{cmd_get_Total_T1, &pwmt_cur.Total_T1, URT_4N, 3}, {cmd_get_Total, &pwmt_cur.Total, URT_4N, 3},
		{cmd_get_Time, &pwmt_cur.Time, URT_CURTIME, 1} };

// Convert UART_Buffer to var[array_size]
// Array element type: b4 = 0 -> 3 bytes, 1 -> 4 bytes
void ICACHE_FLASH_ATTR uart_receive_get_number(uint32 *var, uint8 b4, uint8 array_size)
{
	uint8 i, *p = UART_Buffer + 1;
	for(i = 0; i < array_size; i++) {
		var[i] = b4 ? ((p[0] << 16) + ((p[1]&0x3F) << 24) + p[2] + (p[3] << 8)) : (((p[0]&0x3F) << 16) + p[1] + (p[2] << 8));
		p += 3 + b4;
	}
}

// convert BCD (1 byte) to number
uint8 ICACHE_FLASH_ATTR convert_BCD_1(uint8 * buffer)
{
	uint8 bcd = *buffer;
	return (bcd >> 4) * 10 + (bcd & 0xF);
}

// convert number to BCD (1 byte)
void ICACHE_FLASH_ATTR convert_to_BCD_1(uint8 * buffer, uint8 num)
{
	*buffer = ((num / 10) << 4) + num % 10;
}

time_t ICACHE_FLASH_ATTR uart_receive_get_time(uint8 *buf) {
	return buf[4] == 0 ? 0 : system_mktime(2000 + convert_BCD_1(&buf[5]), convert_BCD_1(&buf[4]), convert_BCD_1(&buf[3]),
			convert_BCD_1(&buf[2]), convert_BCD_1(&buf[1]), convert_BCD_1(&buf[0]));
}

void ICACHE_FLASH_ATTR uart_Task(os_event_t *events)
{
    if(events->sig == UART_RX_CHARS){ // Received all
		#if DEBUGSOO > 4
//			dbg_printf("RX(%u): ", system_get_time());
//			uint8 jjj;
//			for(jjj = 0; jjj < UART_Buffer_idx; jjj++) {
//				dbg_printf(" %02X", UART_Buffer[jjj]);
//			}
//			dbg_printf("\n");
    		os_printf(" R%d ", UART_Buffer_idx);
		#endif
    	if(uart_queue_len && (uart_queue[0].flag & UART_RESPONSE_WAITING)) {
    		uart_queue[0].flag = UART_RESPONSE_READING;
    		//uart_queue[0].time = system_get_time();
    	}
//    } else if(events->sig == UART_TX_CHARS){
//		#if DEBUGSOO > 4
//			os_printf(" T%d ", UART0_Buffer_idx);
//		#endif
    }
}

void ICACHE_FLASH_ATTR pwmt_prepare_send(const uint8 * data, uint8 len)
{
	if(!len || uart_queue_len == UART_QUEUE_IDX_MAX) return;
	uint8 *p = uart_queue[uart_queue_len].buffer;
	p[0] = cfg_glo.pwmt_address;
	copy_s4d1(p + 1, (uint8 *)data, len++);
	uint16_t crc = crc_modbus(p, len);
	p[len] = crc & 0xFF;
	p[len + 1] = crc >> 8;
	uart_queue[uart_queue_len].time = 0;
	uart_queue[uart_queue_len].len = len + 2;
	uart_queue[uart_queue_len].flag = UART_SEND_WAITING;
	uart_queue_len++;
}

// 1 - user, 2 - admin
void ICACHE_FLASH_ATTR pwmt_connect(uint8 user)
{
	if(uart_queue_len == UART_QUEUE_IDX_MAX) return;
	cmd_buffer[0] = cmd_connect;
	cmd_buffer[1] = user;
	os_memcpy(&cmd_buffer[2], &cfg_glo.Pass[user - 1], 6);
	uart_queue[uart_queue_len].type = URT_CONNECT;
	pwmt_prepare_send(cmd_buffer, 2 + 6);
}

void ICACHE_FLASH_ATTR pwmt_uart_queue_next(void)
{
	if(uart_queue_len) {
		uart_queue_len--;
		if(uart_queue_len) os_memmove(&uart_queue[0], &uart_queue[1], uart_queue_len * sizeof(uart_queue[0]));
	}
}

void ICACHE_FLASH_ATTR pwmt_send_to_uart(void)
{
	#if DEBUGSOO > 4
		os_printf("SUART(%u)%d,%d ", system_get_time(), uart_queue_len, uart_queue[0].flag);
	#endif
	if(uart_queue_len && uart_queue[0].flag <= UART_SEND_WAITING) {
		if(uart_queue[0].flag == UART_DELETED) {
			pwmt_uart_queue_next();
			#if DEBUGSOO > 4
				os_printf("deleted(%d) ", uart_queue_len);
			#endif
			if(uart_queue[0].flag != UART_SEND_WAITING || uart_queue_len == 0) return;
		}
		uint8 type = uart_queue[0].type;
		if(type == URT_STR) {
			pwmt_command_response_status = 0xFF; // waiting response
		} else if(type == URT_CONNECT) {
			pwmt_connect_status = PWMT_CONNECTING;
		} else if(type == URT_SETTIME) { // set new time
			time_t t = get_sntp_localtime();
			if(t) {
				struct tm tm;
				_localtime(&t, &tm);
				tm.tm_wday++;
				tm.tm_mon++;
				tm.tm_year = tm.tm_year + 1900 - 2000;
				uint8 save_uql = uart_queue_len;
				uart_queue_len = 0;
				cmd_buffer[0] = cmd_write;
				convert_to_BCD_1(&cmd_buffer[2], tm.tm_sec);
				convert_to_BCD_1(&cmd_buffer[3], tm.tm_min);
				convert_to_BCD_1(&cmd_buffer[4], tm.tm_hour);
				if(uart_queue[0].size == 0) { // Correct time
					cmd_buffer[1] = cmd_code_correct_time;
					pwmt_prepare_send(cmd_buffer, 5);
					dbg_printf("=%02d:%02d:%02d\n", tm.tm_hour, tm.tm_min, tm.tm_sec);
				} else { // Set time
					cmd_buffer[1] = cmd_code_set_time;
					convert_to_BCD_1(&cmd_buffer[5], tm.tm_wday);
					convert_to_BCD_1(&cmd_buffer[6], tm.tm_mday);
					convert_to_BCD_1(&cmd_buffer[7], tm.tm_mon);
					convert_to_BCD_1(&cmd_buffer[8], tm.tm_year);
					cmd_buffer[9] = 1; // in Russia always winter
					pwmt_prepare_send(cmd_buffer, 10);
					dbg_printf("=%02d.%02d.%02d %02d:%02d:%02d\n", tm.tm_mday, tm.tm_mon, tm.tm_year, tm.tm_hour, tm.tm_min, tm.tm_sec);
				}
				pwmt_time_was_corrected_today = 1;
				uart_queue_len = save_uql;
			}
		}
		uart_drv_start();
		UART_Buffer_idx = 0;
		if(uart_tx_buf(uart_queue[0].buffer, uart_queue[0].len) == uart_queue[0].len) {
			uart_queue[0].flag = UART_RESPONSE_WAITING;
		}
		uart_queue[0].time = system_get_time();
	}
}

void ICACHE_FLASH_ATTR pwmt_read_current(void)
{
	#if DEBUGSOO > 4
		os_printf("RCurr(%u) %u,%u; fl: %d ", uart_queue[0].time, uart_queue_len, pwmt_connect_status, uart_queue[0].flag);
	#endif
	if(UART_QUEUE_IDX_MAX - uart_queue_len < sizeof(cmd_get_current_array) / sizeof(cmd_get_current_array[0])) return; // overload
	uint8 i;
	for(i = 0; i < sizeof(cmd_get_current_array) / sizeof(cmd_get_current_array[0]); i++) {
		uart_queue[uart_queue_len].receive_var = cmd_get_current_array[i].receive_var;
		uart_queue[uart_queue_len].type = cmd_get_current_array[i].type;
		uart_queue[uart_queue_len].size = cmd_get_current_array[i].size;
		pwmt_prepare_send(cmd_get_current_array[i].data_send,
				i == sizeof(cmd_get_current_array)/sizeof(cmd_get_current_array[0])-1 ? cmd_get_current_array_element_last_size : cmd_get_current_array_element_size);
	}
	#if DEBUGSOO > 4
		os_printf(" =>%d\n", uart_queue_len);
	#endif
}

// tarif: 0 - All, 1 - T1
void ICACHE_FLASH_ATTR pwmt_read_archive_add_queue(uint32 *var, uint8 opcode, uint8 tarif)
{
	uart_queue[uart_queue_len].type = URT_4N;
	uart_queue[uart_queue_len].size = 1;
	uart_queue[uart_queue_len].receive_var = var + tarif;
	cmd_get_archive[1] = opcode;
	cmd_get_archive[2] = tarif;
	pwmt_prepare_send(cmd_get_archive, sizeof(cmd_get_archive));
}

void ICACHE_FLASH_ATTR pwmt_read_archive(void)
{
	#if DEBUGSOO > 4
		os_printf("RArch(%u) %u,%u; fl: %d ", system_get_time(), uart_queue_len, pwmt_connect_status, uart_queue[0].flag);
	#endif
	if(UART_QUEUE_IDX_MAX - uart_queue_len < PWMT_ARCHIVE_READ_BUF_SIZE || pwmt_cur.Time == 0) return; // overload or not inited
	uint16 days = pwmt_cur.Time / SECSPERDAY;
	if(days != pwmt_arch.DayLastRead || pwmt_arch.Status >= 2) {
		if(pwmt_arch.Status <= 3) {
			uint8 tarif = pwmt_arch.Status <= 2 ? 0 : 1;
			#if DEBUGSOO > 4
				os_printf("Tarif: %d ", tarif);
			#endif
			pwmt_read_archive_add_queue(&pwmt_arch.PrevYear, cmd_get_arch_PrevYear, tarif);
			pwmt_read_archive_add_queue(&pwmt_arch.Yesterday, cmd_get_arch_Yesterday, tarif);
			if(pwmt_cur.Month) {
				pwmt_read_archive_add_queue(&pwmt_arch.PrevMonth, cmd_get_arch_Month + (pwmt_cur.Month > 1 ? pwmt_cur.Month - 1 : 12), tarif);
				pwmt_read_archive_add_queue(&pwmt_arch.ThisMonth, cmd_get_arch_Month + pwmt_cur.Month, tarif);
			}
			pwmt_read_archive_add_queue(&pwmt_arch.ThisYear, cmd_get_arch_ThisYear, tarif);
			pwmt_read_archive_add_queue(&pwmt_arch.Today, cmd_get_arch_Today, tarif);
		} else { // Months
			uint8 m = (pwmt_arch.Status - 4) * (PWMT_ARCHIVE_READ_BUF_SIZE/2);
			uint8 em = m + (PWMT_ARCHIVE_READ_BUF_SIZE/2);
			for(; m < em; m++) {
				pwmt_read_archive_add_queue(&pwmt_arch.Month[m][0], cmd_get_arch_Month + m + 1, 0);
				pwmt_read_archive_add_queue(&pwmt_arch.Month[m][0], cmd_get_arch_Month + m + 1, 1);
			}
		}
	}
	#if DEBUGSOO > 4
		os_printf(" =>%d\n", uart_queue_len);
	#endif
}

// arr: 0..0x12
void ICACHE_FLASH_ATTR pwmt_read_time_array(uint8 arr)
{
	uint8 i = 0;
	os_memset(pwmt_time_array, 0, sizeof(pwmt_time_array));
	for(i = 0; i < 10; i++) {
		if(uart_queue_len == UART_QUEUE_IDX_MAX) break;
		cmd_buffer[0] = cmd_read_arrays;
		cmd_buffer[1] = arr;
		cmd_buffer[2] = i;
		uart_queue[uart_queue_len].receive_var = &pwmt_time_array[i][0];
		uart_queue[uart_queue_len].type = URT_TIME;
		uart_queue[uart_queue_len].size = 2;
		pwmt_prepare_send(cmd_buffer, 3);
	}
	pwmt_send_to_uart();
}

void ICACHE_FLASH_ATTR uart_receive_timer_func(void) // call every cfg_glo.pwmt_read_timeout
{
	static uint8_t _uart_buf_idx = 0;
	if(uart_queue_len && sleep_after_errors_cnt == 0) {
		uint32 dt = system_get_time() - uart_queue[0].time;
		uint8 fl = uart_queue[0].flag;
		if(Debug_level == 4) {
			if(UART_Buffer_idx && UART_Buffer_idx != _uart_buf_idx && _uart_buf_idx <= UART_Buffer_idx) dbg_printf("U:%X\n", UART_Buffer[_uart_buf_idx++]);
		}
		if(fl == UART_SEND_WAITING) {
			if(dt >= cfg_glo.pwmt_delay_after_err) {
				#if DEBUGSOO > 4
					os_printf("SEND timeout\n");
				#endif
				pwmt_send_to_uart();
			}
		} else if(fl & UART_RESPONSE_WAITING) { // UART_RESPONSE_WAITING or UART_RESPONSE_READING
			if(dt >= (fl == UART_RESPONSE_WAITING ? cfg_glo.pwmt_response_timeout : cfg_glo.pwmt_read_timeout)) {
				_uart_buf_idx = 0;
				#if DEBUGSOO > 5
					os_printf("UART(%u)%d: ", system_get_time(), fl);
					uint8 jjj;
					for(jjj = 0; jjj < uart_queue[0].len; jjj++) {
						os_printf("%02X ", uart_queue[0].buffer[jjj]);
					}
				#endif
				if(UART_Buffer_idx >= 4 && (crc_modbus(UART_Buffer, UART_Buffer_idx - 2) == UART_Buffer[UART_Buffer_idx - 1] * 256 + UART_Buffer[UART_Buffer_idx - 2])) {
					#if DEBUGSOO > 4
						os_printf(" RX(%u)%d:", system_get_time(), fl);
						uint8 jjj;
						for(jjj = 0; jjj < UART_Buffer_idx; jjj++) {
							os_printf(" %02X", UART_Buffer[jjj]);
						}
						os_printf("\n");
					#endif
					uart_queue[0].flag = UART_RESPONSE_READY_OK;
					pwmt_last_response = UART_Buffer_idx == 4 ? UART_Buffer[1] : 0;
					pwmt_repeated_errors = 0;
				} else { // error
					pwmt_last_response = 6 + (UART_Buffer_idx >= 4); // 6 - Not responding, 7 - CRC error
					#if DEBUGSOO > 4
						os_printf("Err(%u)%d=%d, %d\n", system_get_time(), fl, pwmt_last_response, UART_Buffer_idx);
					#endif
					if(UART_Buffer_idx)	{
						if(pwmt_last_response == 7) dbg_printf("ECRC\n"); else dbg_printf("ENR\n");
					}
					if(uart_queue[0].type == URT_STR) { // custom command
						goto xfill_command_reponse;
					} else {
						pwmt_read_errors++;
					}
					if(++pwmt_repeated_errors > cfg_glo.repeated_errors_thr) {
						pwmt_repeated_errors--;
						Fram_halted = 1;
						GPIO_OUT_W1TC = (1<<2);//I2C_SDA_PIN);
						GPIO_OUT_W1TC = (1<<0);//I2C_SCL_PIN);
					}
					if(pwmt_connect_status != PWMT_CONNECTED || pwmt_repeat_on_error_cnt > 0) { // reconnect after cfg_glo.pwmt_delay_after_err
						if(pwmt_repeat_on_error_cnt == 0 && cfg_glo.sleep_after_series_errors != 0) { // UART0 off
							sleep_after_errors_cnt = cfg_glo.sleep_after_series_errors;
							dbg_printf("ErrSleepC\n");
							uart_drv_close();
							disable_mux_uart0();
						} else {
							uart_queue[0].flag = UART_SEND_WAITING;
							uart_queue[0].time = system_get_time();
						}
						if(pwmt_repeat_on_error_cnt) pwmt_repeat_on_error_cnt--;
					} else {
						pwmt_repeat_on_error_cnt = cfg_glo.pwmt_on_error_repeat_cnt;
						dbg_printf("Eu%u,%u:%d,%d=", dbg_next_time(), dt, pwmt_last_response, UART_Buffer_idx);
						uint8 jjj;
						for(jjj = 0; jjj < uart_queue[0].len; jjj++) {
							dbg_printf("%02X", uart_queue[0].buffer[jjj]);
						}
						//dbg_printf("\n");
						dbg_printf(", del\n");
						uart_queue[0].flag = UART_DELETED;
						if(cfg_glo.sleep_after_series_errors != 0) { // UART0 off
							sleep_after_errors_cnt = cfg_glo.sleep_after_series_errors;
							dbg_printf("ErrSleep\n");
							uart_drv_close();
							disable_mux_uart0();
						} else pwmt_send_to_uart();
					}
				}
			}
		} else if(fl == UART_RESPONSE_READY_OK) {
			pwmt_repeat_on_error_cnt = cfg_glo.pwmt_on_error_repeat_cnt;
			UART_QUEUE *uq = &uart_queue[0];
			if(UART_Buffer_idx == 4) { // status returned
				if(pwmt_last_response == 5) { // is not connected
					uart_queue_len = 1; // record 0 will be deleted below
					pwmt_connect(1);
				} else if(pwmt_last_response == 4) { // time was corrected today
					pwmt_time_was_corrected_today = 1;
				} else {
					if(pwmt_connect_status == PWMT_CONNECTING) {
						#if DEBUGSOO > 4
							os_printf("Connect %d\n", pwmt_last_response);
						#endif
						if(pwmt_last_response == 0) { // ok
							pwmt_connect_status = PWMT_CONNECTED;
						} else { // reconnect cfg_glo.pwmt_delay_after_err
							uart_queue[0].flag = UART_SEND_WAITING;
							uart_queue[0].time = system_get_time();
						}
					}
				}
			} else if(uq->type == URT_2B) {
				os_memcpy(uq->receive_var, UART_Buffer + 1, 2);
			} else if(uq->type == URT_3N || uq->type == URT_4N) {
				uart_receive_get_number(uq->receive_var, uq->type == URT_3N ? 0 : 1, uq->size);
				if(uq->receive_var == &pwmt_cur.Total && pwmt_arch.Status == 1) { // update archive
					uint32 delta = pwmt_cur.Total[0] + pwmt_cur.Total[1] + pwmt_cur.Total[2];
					if(pwmt_arch.Total == 0) { // first time
						pwmt_arch.Total = delta;
					} else {
						delta -= pwmt_arch.Total;
						#if DEBUGSOO > 4
							os_printf("Total: %u, Delta: %d\n", pwmt_arch.Total, delta);
						#endif
						pwmt_arch.Today += delta;
						pwmt_arch.ThisMonth += delta;
						pwmt_arch.Month[pwmt_cur.Month - 1][0] += delta;
						pwmt_arch.ThisYear += delta;
						pwmt_arch.Total += delta;
					}
				} else if(uq->receive_var == &pwmt_cur.Total_T1 && pwmt_arch.Status == 1) { // update archive T1
					uint32 delta = pwmt_cur.Total_T1[0] + pwmt_cur.Total_T1[1] + pwmt_cur.Total_T1[2];
					if(pwmt_arch.Total_T1 == 0) { // first time
						pwmt_arch.Total_T1 = delta;
					} else {
						delta -= pwmt_arch.Total_T1;
						#if DEBUGSOO > 4
							os_printf("Total_T1: %u, Delta: %d\n", pwmt_arch.Total_T1, delta);
						#endif
						pwmt_arch.Today_T1 += delta;
						pwmt_arch.ThisMonth_T1 += delta;
						pwmt_arch.Month[pwmt_cur.Month - 1][1] += delta;
						pwmt_arch.ThisYear_T1 += delta;
						pwmt_arch.Total_T1 += delta;
					}
				} else if(uq->receive_var == &pwmt_arch.Today) { // Next archive
					pwmt_arch.Status = 3; // next archive portion
					pwmt_read_archive();
				} else if(uq->receive_var == &pwmt_arch.Today_T1) { // Archive received, next months
					pwmt_arch.DayLastRead = pwmt_cur.Time / SECSPERDAY;
					pwmt_arch.Status = 4;
					pwmt_read_archive();
				} else if(uq->receive_var == &pwmt_arch.Month[(pwmt_arch.Status - 4) * (PWMT_ARCHIVE_READ_BUF_SIZE/2) + (PWMT_ARCHIVE_READ_BUF_SIZE/2) - 1][1]) {
					// next month
					pwmt_arch.Status++;
					if((pwmt_arch.Status - 4) * (PWMT_ARCHIVE_READ_BUF_SIZE/2) >= 12) {
						pwmt_arch.Status = 1; // finish
					} else {
						pwmt_read_archive();
					}
				}
			} else if(uq->type == URT_TIME) {
				((time_t *)uq->receive_var)[0] = uart_receive_get_time(&UART_Buffer[1]);
				if(UART_Buffer_idx > 7)	((time_t *)uq->receive_var)[1] = uart_receive_get_time(&UART_Buffer[7]);
			} else if(uq->type == URT_CURTIME && uq->receive_var == &pwmt_cur.Time) {
				pwmt_cur.sntp_time = get_sntp_localtime();
				pwmt_cur.Month = convert_BCD_1(&UART_Buffer[6]);
				pwmt_cur.Time = system_mktime(2000 + convert_BCD_1(&UART_Buffer[7]), pwmt_cur.Month, convert_BCD_1(&UART_Buffer[5]),
						convert_BCD_1(&UART_Buffer[3]), convert_BCD_1(&UART_Buffer[2]), convert_BCD_1(&UART_Buffer[1]));
				if(pwmt_cur.sntp_time) {
					uint32 delta = abs(pwmt_cur.Time - pwmt_cur.sntp_time);
					#if DEBUGSOO > 2
						if(delta) os_printf("Time mismatch: %u - %u = %d sec\n", pwmt_cur.Time, pwmt_cur.sntp_time, delta);
					#endif
					if(cfg_glo.TimeMaxMismatch && (delta > cfg_glo.TimeMaxMismatch)) {
					  #if DEBUGSOO > 4
						os_printf("need correct! %d\n", uart_queue_len);
					  #else
						#ifdef DEBUG_TO_RAM
							static uint32 last_delta;
							if(last_delta != delta) {
								dbg_printf("TM(%u): %u, %d", dbg_next_time(), pwmt_cur.sntp_time, pwmt_cur.Time);
								last_delta = delta;
							}
						#endif
						if(cfg_glo.pwmt_address && pwmt_time_was_corrected_today == 0) { // address must be != 0
							if(delta < TimeMismatchMaxForCorrect) { // Correct time
								if(UART_QUEUE_IDX_MAX - uart_queue_len >= 1) { // enough space?
									uart_queue[uart_queue_len].type = URT_SETTIME;
									uart_queue[uart_queue_len].size = 0;
									uart_queue[uart_queue_len].flag = UART_SEND_WAITING;
									uart_queue_len++;
								}
							} else { // Set time
								if(UART_QUEUE_IDX_MAX - uart_queue_len >= 3) { // enough space?
									pwmt_connect(2);
									uart_queue[uart_queue_len].type = URT_SETTIME;
									uart_queue[uart_queue_len].size = 1;
									uart_queue[uart_queue_len].flag = UART_SEND_WAITING;
									uart_queue_len++;
									pwmt_connect(1);
								}
							}
						}
					  #endif
					}
				}
				iot_cloud_send(1);
			} else if(uq->type == URT_STR) {
xfill_command_reponse:
				if(UART_Buffer_idx >= 4) {
					pwmt_command_response_len = UART_Buffer_idx - 3;
					os_memcpy(uart_queue[0].receive_var, UART_Buffer + 1, pwmt_command_response_len);
				}
				pwmt_command_response_status = pwmt_last_response;
			}
			uart_queue[0].flag = UART_DELETED;
			pwmt_send_to_uart();
		}
	}
	if(cfg_glo.pwmt_read_timeout < 500) cfg_glo.pwmt_read_timeout = 500;
	ets_timer_arm_new(&uart_receive_timer, cfg_glo.pwmt_read_timeout, 0, 0); // no repeat, us
}

void ICACHE_FLASH_ATTR pwmt_request_timer_func(void) // call every: cfg_glo.request_period
{
	if(sleep_after_errors_cnt != 0) return;
	uint8 autosend = !uart_queue_len;
	if((uart_queue_len == 0 && pwmt_connect_status == PWMT_NOT_CONNECTED)
			|| (pwmt_last_response == 6 && pwmt_connect_status != PWMT_CONNECTING)) {
		pwmt_connect(1);
	}
	pwmt_read_archive();
	pwmt_read_current();
	if(autosend) pwmt_send_to_uart();
}

void ICACHE_FLASH_ATTR irda_init(void)
{
	//init_crc16_tab();
	uarts_init();
	pwmt_connect_status = PWMT_NOT_CONNECTED;
	pwmt_time_was_corrected_today = 0;
	pwmt_repeated_errors = 0;
	pwmt_repeat_on_error_cnt = cfg_glo.pwmt_on_error_repeat_cnt;
	uart_queue_len = 0;
	ets_timer_disarm(&uart_receive_timer);
	os_timer_setfn(&uart_receive_timer, (os_timer_func_t *)uart_receive_timer_func, NULL);
	ets_timer_arm_new(&uart_receive_timer, cfg_glo.pwmt_read_timeout, 0, 0); // no repeat, us
	if(cfg_glo.request_period) {
		ets_timer_disarm(&pwmt_request_timer);
		os_timer_setfn(&pwmt_request_timer, (os_timer_func_t *)pwmt_request_timer_func, NULL);
		ets_timer_arm_new(&pwmt_request_timer, cfg_glo.request_period * 1000, 1, 1); // repeat ms
	}
	#ifdef I2S_CLOCK_OUT
		i2s_clock_out();
	#endif

	//	ets_intr_lock();
	//	//GPIOx_PIN(0) = GPIO_PIN_DRIVER; // Open-drain
	//	SET_PIN_FUNC(0, (MUX_FUN_IO_PORT(0))); // Pullup
	//	GPIO_OUT_W1TS = (1<<0); // Set HI (WO)
	//	GPIO_ENABLE_W1TS = (1<<0); // Enable output (WO)
	//	ets_intr_unlock();
	//	GPIO_OUT ^= (1<<0);
}

#endif

