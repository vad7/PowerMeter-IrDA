/*
* The MIT License (MIT)
* 
* spi_write_read_block(), spi_write_read_byte(), SPI overlap
* and other improvements written by Vadim Kulakov, 2016
*
* spi_transaction() written by David Ogilvy (MetalPhreak), 2015
* 
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
* 
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
* 
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include "user_config.h"
#ifdef USE_HSPI
#ifndef SPI_APP_H
#define SPI_APP_H

#include "spi_register.h"
#include "ets_sys.h"
#include "osapi.h"
//#include "uart.h"
#include "os_type.h"

//#define SPI_TINY 	// Only 1 byte (8 bits) r/w duplex functions
#define SPI_BLOCK	// Write, Read: command + data(max 64 bytes), can full-duplex

//Define SPI hardware modules
#define SPI 0
#define HSPI 1
#define spi_no HSPI
#define SPI_OVERLAP // overlap SPI (used by firmware flash), GPIO0 used as CS2
//#define SPI_QIO

//|Pin Name| GPIO # | HSPI Function |
//|--------|--------|---------------|
//|  MTDI  | GPIO12 | MISO (DIN)    |
//|  MTCK  | GPIO13 | MOSI (DOUT)   |
//|  MTMS  | GPIO14 | CLOCK         |
//|  MTDO  | GPIO15 | CS / SS       |

#define SPI_NOT_USE_CS			0	// 1 - CS pin is not used by hardware
#define SPI_CLK_80MHZ_NODIV 	0	// 1 - full 80Mhz clock
#define DELAY_BEFORE_CHANGE_CS	1 	// set delay before and after CS change

#define SPI_BYTE_ORDER_HIGH_TO_LOW 0
#define SPI_BYTE_ORDER_LOW_TO_HIGH 0

//Define some default SPI clock settings,
//SPI_CLK_FREQ = CPU_CLK_FREQ/(SPI_CLK_PREDIV*SPI_CLK_CNTDIV)
//#define SPI_CLK_PREDIV 1
//#define SPI_CLK_CNTDIV 2

#ifdef SPI_OVERLAP
	#if USE_FIX_QSPI_FLASH == 80 && SPI_CLK_80MHZ_NODIV == 0
		#error "SPI overlap available only if flash SPI speed less than 80Mhz!"
	#endif
#endif

void spi_init(uint32 freq) ICACHE_FLASH_ATTR;
void spi_clock(uint16 prediv, uint8 cntdiv) ICACHE_FLASH_ATTR;
//void spi_mode(uint8 spi_cpha,uint8 spi_cpol) ICACHE_FLASH_ATTR;

#ifdef SPI_BLOCK

#define SPI_SEND 		1
#define SPI_RECEIVE 	2
#define SPI_ADDR_BITS 	(8 + 16) // opcode + address

void spi_write_read_block(uint8 sr, uint32 addr, uint8 * data, uint8 data_size);

#endif
#ifdef SPI_TINY

uint8 spi_write_read_byte(uint8 dout_data) ICACHE_FLASH_ATTR; // HSPI

#else
#ifndef SPI_BLOCK

void spi_tx_byte_order(uint8 byte_order) ICACHE_FLASH_ATTR;
void spi_rx_byte_order(uint8 byte_order) ICACHE_FLASH_ATTR;
uint32 spi_transaction(uint8 cmd_bits, uint16 cmd_data, uint32 addr_bits, uint32 addr_data, uint32 dout_bits, uint32 dout_data, uint32 din_bits, uint32 dummy_bits) ICACHE_FLASH_ATTR;

//Expansion Macros

#define spi_txd(bits, data) spi_transaction(0, 0, 0, 0, bits, (uint32) data, 0, 0)
#define spi_tx8(data)       spi_transaction(0, 0, 0, 0, 8,    (uint32) data, 0, 0)
#define spi_tx16(data)      spi_transaction(0, 0, 0, 0, 16,   (uint32) data, 0, 0)
#define spi_tx32(data)      spi_transaction(0, 0, 0, 0, 32,   (uint32) data, 0, 0)

#define spi_rxd(bits) spi_transaction(0, 0, 0, 0, 0, 0, bits, 0)
#define spi_rx8()       spi_transaction(0, 0, 0, 0, 0, 0, 8,    0)
#define spi_rx16()      spi_transaction(0, 0, 0, 0, 0, 0, 16,   0)
#define spi_rx32()      spi_transaction(0, 0, 0, 0, 0, 0, 32,   0)
#endif
#endif

#define spi_busy(spi_no) READ_PERI_REG(SPI_CMD(spi_no))&SPI_USR

#endif

#endif
