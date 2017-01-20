/*
 * I2C.c
 *
 * Created: 17.01.2014 14:00:10, modified 2016
 *  Author: Vadim Kulakov, vad7@yahoo.com
 */
#include "user_config.h"

#ifdef USE_I2C
#ifndef __I2C_H__
#define __I2C_H__

#include "ets_sys.h"
#include "osapi.h"
#include "gpio.h"

#define I2C_SDA_PIN 2 // SDA on GPIO2
#define I2C_SCL_PIN 0 // SCL on GPIO0

uint32	I2C_EEPROM_Error;

#define I2C_WRITE			0
#define I2C_READ			1
#define I2C_NOACK			1
#define I2C_ACK				0

void 	i2c_init(uint32 delay_us) ICACHE_FLASH_ATTR;
uint8_t i2c_Start(uint8_t addr) ICACHE_FLASH_ATTR;
void 	i2c_Stop(void) ICACHE_FLASH_ATTR;
uint8_t i2c_WriteBit(uint8_t bit);
uint8_t i2c_ReadBit(void);
uint8_t i2c_Write(uint8_t data);
uint8_t i2c_Read(uint8_t ack);
uint8_t i2c_eeprom_read_block(uint8_t addr, uint32_t pos, uint8_t *buffer, uint32_t cnt) ICACHE_FLASH_ATTR;
uint8_t i2c_eeprom_write_block(uint8_t addr, uint32_t pos, uint8_t *buffer, uint32_t cnt) ICACHE_FLASH_ATTR;

#endif
#endif
