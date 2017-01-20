/*
 * eeprom read/write
 *
 * Created: 12.05.2016
 * Written by Vadim Kulakov, vad7 @ yahoo.com
 *
 */
#include "driver/eeprom.h"

#ifdef USE_HSPI

// max len = 64 bytes
uint8_t eeprom_read_block(uint32_t addr, uint8_t *buffer, uint32_t len)
{
	if(len > MAX_EEPROM_BLOCK_LEN) return 1;
	spi_write_read_block(SPI_RECEIVE, (EEPROM_READ<<EEPROM_ADDR_BITS) | addr, buffer, len);
	return 0;
}

// max len = 64 bytes
uint8_t eeprom_write_block(uint32_t addr, uint8_t *buffer, uint32_t len)
{
	if(len > MAX_EEPROM_BLOCK_LEN) return 1;
	uint8_t opcode[1] = { EEPROM_WREN };
	spi_write_read_block(SPI_SEND + SPI_RECEIVE, 0, opcode, 1);
	spi_write_read_block(SPI_SEND, (EEPROM_WRITE<<EEPROM_ADDR_BITS) | addr, buffer, len);
	return 0;
}

#endif
