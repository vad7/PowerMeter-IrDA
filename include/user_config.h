#ifndef _user_config_h_
#define _user_config_h_

#include "sdk/sdk_config.h"

#define SYS_VERSION "0.6.2"

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define WEB_DEFAULT_SOFTAP_IP	DEFAULT_SOFTAP_IP // ip 192.168.4.1
#define WEB_DEFAULT_SOFTAP_MASK	DEFAULT_SOFTAP_MASK // mask 255.255.255.0
#define WEB_DEFAULT_SOFTAP_GW	DEFAULT_SOFTAP_IP // gw 192.168.4.1
#define WEB_DEFAULT_STATION_IP    0x3201A8C0 // ip 192.168.1.50
#define WEB_DEFAULT_STATION_MASK  0x00FFFFFF // mask 255.255.255.0
#define WEB_DEFAULT_STATION_GW    0x0101A8C0 // gw 192.168.1.1
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define PROGECT_NUMBER 2
// 0 -> проект "TCP2UART"
// 1 -> проект MODBUS-"RS485"
// 2 -> пустой web+websocket
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#if PROGECT_NUMBER == 0
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Конфигурация для проекта TCP2UART
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define USE_WEB		80 // включить в трансялцию порт Web, если =0 - по умолчанию выключен
#define WEBSOCKET_ENA  // включить WEBSOCKET

#define USE_TCP2UART 12345 // включить в трансялцию драйвер TCP2UART, номер порта по умолчанию (=0 - отключен)

#define USE_CPU_SPEED  160 // установить частоту CPU по умолчанию 80 или 160 MHz

#define USE_NETBIOS	1 // включить в трансялцию драйвер NETBIOS, если =0 - по умолчанию выключен.

#define USE_SNTP	1 // включить в трансялцию драйвер SNTP, если =0 - по умолчанию выключен, = 1 - по умолчанию включен.

#define USE_MODBUS	502 // включить в трансялцию Modbus TCP, если =0 - по умолчанию выключен
#define MDB_ID_ESP 50 // номер устройства ESP на шине modbus

#define USE_CAPTDNS	0	// включить в трансялцию NDS отвечающий на всё запросы клиента при соединении к AP модуля
						// указанием на данный WebHttp (http://aesp8266/), если =0 - по умолчанию выключен

#define USE_OVERLAY 7424 // включить в трансялцию возможность работы с оверлеями (максимальный размер кода оверлея >8192)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#elif PROGECT_NUMBER == 1
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Конфигурация для проекта MODBUS-RS-485
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#define USE_WEB		80 // включить в трансялцию порт Web, если =0 - по умолчанию выключен
#define WEBSOCKET_ENA // включить WEBSOCKET

#define USE_CPU_SPEED  160 // установить частоту CPU по умолчанию 80 или 160 MHz

#define USE_NETBIOS	1 // включить в трансялцию драйвер NETBIOS, если =0 - по умолчанию выключен.

#define USE_SNTP	1 // включить в трансялцию драйвер SNTP, если =0 - по умолчанию выключен, = 1 - по умолчанию включен.

#ifndef USE_TCP2UART
#define USE_RS485DRV	// использовать RS-485 драйвер
#define MDB_RS485_MASTER // Modbus RTU RS-485 master & slave
#endif
#define USE_MODBUS	502 // включить в трансялцию Modbus TCP, если =0 - по умолчанию выключен
#define MDB_ID_ESP 50 // номер устройства ESP на шине modbus

#define USE_CAPTDNS	0	// включить в трансялцию DNS отвечающий на всё запросы клиента при соединении к AP модуля
						// указанием на данный WebHttp (http://aesp8266/), если =0 - по умолчанию выключен

#define USE_OVERLAY 7424 // включить в трансялцию возможность работы с оверлеями (максимальный размер кода оверлея 7456)
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#elif PROGECT_NUMBER == 2
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
// Конфигурация для проекта WEB+WEBSOCK
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#define USE_WEB		80 // включить в трансляцию порт Web, если =0 - по умолчанию выключен
//#define BUILD_FOR_OTA_512k // if enabled - very small build for two stage OTA on 512kbyte flash
#ifndef BUILD_FOR_OTA_512k
//#define USE_GPIO3_AS_CFG_RESET // При замыкании на землю GPIO3(RX) обнулять конфигурацию
//#define WEBSOCKET_ENA // включить WEBSOCKET
//#define USE_NETBIOS	1 // включить в трансялцию драйвер NETBIOS, если =0 - по умолчанию выключен.
#define USE_SNTP	1 // включить в трансялцию драйвер SNTP, если =0 - по умолчанию выключен, = 1 - по умолчанию включен.
//#define USE_GPIOs_intr
//#define USE_UART1 // включить UART1 для отладки
//#define USE_OVERLAY 4000 // включить в трансляцию возможность работы с оверлеями (максимальный размер кода оверлея)
#define DEBUG_TO_RAM

//#define NO_LOCAL_STORAGE
#ifndef NO_LOCAL_STORAGE

#define STORE_FLASH_ADDR	0x100000 // Начальный адрес для хранилища во флеш (до верхней ее границы), должен быть выровнен на начало сектора.

#define USE_I2C 	  // драйвер I2C
#ifdef  USE_I2C
#define I2C_ID 0x50
#ifndef DEBUG_UART
#define I2C_SDA_PIN 2 // SDA on GPIO#
#else
#define I2C_SDA_PIN 4 // SDA on GPIO# (debug only)
#endif
#define I2C_SCL_PIN 0 // SCL on GPIO#
#endif
//#define USE_HSPI	  // драйвер  HSPI
#endif

#define UTC_OFFSET +3 // Moscow time

#define USE_UART0	  // UART0 in/out
//#define UART0_IRDA	  // Hardware esp8266 IrDA, uncomment USE_TIMER0 and TIMER0_USE_NMI_VECTOR in sdk_config.h
#define USE_MERCURY	  // UART0 as interface to Mercury power meter (speed 9600b)
//#define USE_I2S
//#define I2S_CLOCK_OUT // GPIO2 (I2SO_WS)
#endif
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#endif // PROGECT_NAME

#define MAX_SYS_CONST_BLOCK 0x400 // for write some info for bootloader

#if DEBUGSOO > 0
#if !(defined(USE_RS485DRV) || defined(USE_TCP2UART))
// #define USE_GDBSTUB // UDK пока не поддерживает GDB
#endif
#endif

#endif // _user_config_h_


