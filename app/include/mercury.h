/*
 * Mercury power meter driver (http://www.incotexcom.ru/counters.htm)
 *
 * Use IrDa interface (UART0)
 *
 * Written by vad7
 */
#ifdef USE_MERCURY
#include "c_types.h"
#include "time.h"

typedef struct {
	uint32 W; 		// +W*h
	uint32 W_T1; 	// Tariff #1, +W*h
	//uint32 W_T2;  // = W - W_T1
} POWER;

typedef struct {
	uint32 W[4]; // Moment power *100W, All+Phase 1..3
	uint32 U[3]; // Voltage *100V, phase 1..3
	uint32 I[3]; // Current *1000A, phase 1..3
	uint32 K[3]; // Power K *1000, phase 1..3
	POWER Total;
	POWER Year;
	POWER Day;
	uint32 TotalPhase1; // W*h
	uint32 TotalPhase2; // W*h
	uint32 TotalPhase3; // W*h
	time_t Time;
} PWMT_CURRENT;
PWMT_CURRENT pwmt_cur;

typedef struct {
	POWER PreviousYear;
	POWER Month[12];
	POWER PreviousDay;
} PWMT_ARCHIVE;
PWMT_ARCHIVE pwmt_arch;

uint8 pwmt_connect_status; 	// PWMT_CONNECT_STATUS
uint8 pwmt_last_response;	// 1 byte response, 0..6
uint8 pwmt_address;
uint8 uart_queue_len;
time_t pwmt_current_time;
#define UART_QUEUE_IDX_MAX 15

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
	UART_RESPONSE_READY		= 0x04,
	UART_RESPONSE_READY_OK	= 0x05,
} UART_FLAG;

typedef struct {
	uint32 time;
	void *receive_var;
	uint8 type; // UART_RECORD_TYPE
	uint8 size; // type repeat count
	uint8 flag; // UART_FLAG
	uint8 len;
	uint8 buffer[21];
} UART_QUEUE;
UART_QUEUE uart_queue[UART_QUEUE_IDX_MAX];

#endif
