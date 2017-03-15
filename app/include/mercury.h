/*
 * Mercury power meter driver (http://www.incotexcom.ru/counters.htm)
 *
 * Use IrDa interface (UART0)
 *
 * Written by vad7
 */
#ifdef USE_MERCURY
#include "c_types.h"
#include "localtime.h"

typedef struct {
	uint32 P[4];		// W, Moment active power *100, All+Phase 1..3
	uint32 S[4];		// VA, Moment full power *100, All+Phase 1..3
	uint32 I[3];		// A, Current *1000, phase 1..3
	uint32 U[3];		// V, Voltage *100, phase 1..3
	uint32 K[4];		// Power K *1000, All+phase 1..3
	uint32 Total[3];	// Total, W*h, phase 1..3
	uint32 Total_T1[3];	// Total Tarif 1 (day), W*h, phase 1..3
	time_t Time;		// local time
	time_t sntp_time;
	uint8  Month;		// 1..12 current month
} PWMT_CURRENT;
PWMT_CURRENT pwmt_cur;

typedef struct {
	uint32 Total;    // last updated
	uint32 Total_T1; // last updated
	uint32 Today;
	uint32 Today_T1;
	uint32 Yesterday;
	uint32 Yesterday_T1;
	uint32 ThisMonth;
	uint32 ThisMonth_T1;
	uint32 PrevMonth;
	uint32 PrevMonth_T1;
	uint32 ThisYear;
	uint32 ThisYear_T1;
	uint32 PrevYear;
	uint32 PrevYear_T1;
	uint16 DayLastRead;
	uint8  Status; // 0 - empty, 1 - received, 2 - receiving TAll, 3 - receiving T1
} PWMT_ARCHIVE;
#define PWMT_ARCHIVE_READ_BUF_SIZE	6
PWMT_ARCHIVE pwmt_arch;

uint8 pwmt_connect_status; 	// PWMT_CONNECT_STATUS
uint8 pwmt_last_response;	// 1 byte response, 0..7
uint8 uart_queue_len;
uint8 pwmt_command_send[16]; 	 // custom command to send
uint8 pwmt_command_send_len;
uint8 pwmt_command_response[16]; // last response on custom send
uint8 pwmt_command_response_len;
uint8 pwmt_command_response_status; // 0xFF - waiting, otherwise pwmt_last_response
uint32 pwmt_read_errors; // total count read error pwmt

typedef enum
{
	PWMT_NOT_CONNECTED = 0,
	PWMT_CONNECTING,
	PWMT_CONNECTED
} PWMT_CONNECT_STATUS;

typedef enum
{
	UART_DELETED 			= 0,
	UART_SEND_WAITING		= 0x01,
	UART_RESPONSE_WAITING	= 0x02,
	UART_RESPONSE_READING	= 0x03,
	UART_RESPONSE_READY_OK	= 0x04,
} UART_FLAG;

typedef enum
{
	URT_STATUS = 0, // 1 byte answer (1..5)
	URT_CONNECT,
	URT_2B,		// 2 Bytes
	URT_3N,		// Numeric 22bites, 3 Bytes (1B, 3B, 2B) (nnnnnnn)
	URT_4N,		// Numeric 30bites, 4 Bytes (2B, 1B, 4B, 3B) (nnnnnnnnnn)
	URT_TIME,	// 6 bytes (BCD): сек, мин, час, число, месяц, год
	URT_CURTIME,// 8 bytes (BCD): cек, мин, час, день недели, число, месяц, год, зима(1)/лето(0)
	URT_STR		// char array
} UART_RECORD_TYPE;

typedef struct {
	uint32 time;
	void *receive_var;
	uint8 buffer[20];
	uint8 type; // UART_RECORD_TYPE
	uint8 size; // type repeat count
	uint8 flag; // UART_FLAG
	uint8 len;  // data length in the buffer
} UART_QUEUE;
// must be greater than elements in cmd_get_current_array or PWMT_ARCHIVE_READ_BUF_SIZE + 1
#define UART_QUEUE_IDX_MAX 	(PWMT_ARCHIVE_READ_BUF_SIZE + 8 + 1)
UART_QUEUE uart_queue[UART_QUEUE_IDX_MAX];

uint8  Web_Tlog; 	// ~Tlog~ : time array idx
time_t pwmt_time_array[10][2]; // [][0]: on time / before correction; [][1]: off time / after correction

void ICACHE_FLASH_ATTR irda_init(void);
void ICACHE_FLASH_ATTR pwmt_prepare_send(const uint8 * data, uint8 len);
void ICACHE_FLASH_ATTR pwmt_send_to_uart(void);
void ICACHE_FLASH_ATTR pwmt_connect(uint8 user);
void ICACHE_FLASH_ATTR pwmt_read_archive(void);
void ICACHE_FLASH_ATTR pwmt_read_time_array(uint8 arr);

#endif
