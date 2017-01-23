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
	uint32 W1; // Moment power Phase 1
	uint32 W2; // Moment power Phase 2
	uint32 W3; // Moment power Phase 3
	uint16 U1; // Voltage 1
	uint16 U2; // Voltage 2
	uint16 U3; // Voltage 3
	uint16 I1; // Current 1
	uint16 I2; // Current 2
	uint16 I3; // Current 3
	POWER Total;
	POWER Year;
	POWER Day;
	uint32 TotalPhase1; // W*h
	uint32 TotalPhase2; // W*h
	uint32 TotalPhase3; // W*h
	time_t Time;
} PWMT_CURRENT;
PWMT_CURRENT __attribute__((aligned(4))) pwmt_cur;

#define PWMT_CONNECT_PAUSE		5	// ms
#define PWMT_CONNECT_TIMEOUT	150 // ms
uint8 pwmt_connect_status; 			// PWMT_CONNECT_STATUS
uint8 pwmt_address;

typedef struct {
	POWER PreviousYear;
	POWER Month[12];
	POWER PreviousDay;
} PWMT_ARCHIVE;
PWMT_ARCHIVE __attribute__((aligned(4))) pwmt_arch;

uint8 uart_queue_len;
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
	uint8 flag; // UART_FLAG
	uint8 len;
	uint8 buffer[19];
} UART_QUEUE;
UART_QUEUE uart_queue[UART_QUEUE_IDX_MAX];

#endif
