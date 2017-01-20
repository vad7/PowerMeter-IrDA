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
#include "hw/pin_mux_register.h"
#include "driver/spi.h"
#include "sdk/rom2ram.h"
#include "hw/esp8266.h"

#if DEBUGSOO > 4
#include "web_utils.h"
#include "hw/uart_register.h"
#endif

#define WAIT_HSPI_IDLE() 	while(READ_PERI_REG(SPI_EXT2(HSPI))||(READ_PERI_REG(SPI_CMD(HSPI))&0xfffc0000));

////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_init
//   Description: Wrapper to setup HSPI/SPI GPIO pins and default SPI clock
//				 
////////////////////////////////////////////////////////////////////////////////

// SPI clock - freq (kHz). if freq is more than (USE_FIX_QSPI_FLASH / 2) than spi clock equal USE_FIX_QSPI_FLASH
void ICACHE_FLASH_ATTR spi_init(uint32 freq){

	WAIT_HSPI_IDLE();
#ifdef SPI_OVERLAP
	 //hspi overlap to spi, two spi masters on cspi
	SET_PERI_REG_MASK(0x3ff00028, PERI_IO_CSPI_OVERLAP); //	SET_PERI_REG_MASK(HOST_INF_SEL, PERI_IO_CSPI_OVERLAP);

	//set higher priority for spi than hspi
	SET_PERI_REG_MASK(SPI_EXT3(SPI), 0x1);
	SET_PERI_REG_MASK(SPI_EXT3(HSPI), 0x3);
	//SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP);

	//select HSPI CS2 ,disable HSPI CS0 and CS1
	CLEAR_PERI_REG_MASK(SPI_PIN(HSPI), SPI_CS2_DIS);
	SET_PERI_REG_MASK(SPI_PIN(HSPI), SPI_CS0_DIS | SPI_CS1_DIS);

#if SPI_NOT_USE_CS == 0
	//SET IO MUX FOR GPIO0 , SELECT PIN FUNC AS SPI CS2
	//IT WORK AS HSPI CS2 AFTER OVERLAP(THERE IS NO PIN OUT FOR NATIVE HSPI CS1/2)
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_SPICS2);
#endif

#endif

#if spi_no == SPI
		WRITE_PERI_REG(PERIPHS_IO_MUX, (READ_PERI_REG(PERIPHS_IO_MUX) & ~(SPI0_CLK_EQU_SYS_CLK)) | (SPI0_CLK_EQU_SYS_CLK * SPI_CLK_80MHZ_NODIV)); // Set bit if 80MHz sysclock required
#ifndef SPI_OVERLAP
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA0_U, FUNC_SPIQ_MISO);
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_DATA1_U, FUNC_SPID_MOSI);
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_CLK_U, FUNC_SPICLK);
#if SPI_NOT_USE_CS == 0
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_SD_CMD_U, FUNC_SPICS0);
#endif
#endif
#else
		WRITE_PERI_REG(PERIPHS_IO_MUX, (READ_PERI_REG(PERIPHS_IO_MUX) & ~(SPI1_CLK_EQU_SYS_CLK)) | (SPI1_CLK_EQU_SYS_CLK * SPI_CLK_80MHZ_NODIV)); // Set bit if 80MHz sysclock required
#ifndef SPI_OVERLAP
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDI_U, FUNC_HSPIQ_MISO); //GPIO12 is HSPI MISO pin (Master Data In)
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTCK_U, FUNC_HSPID_MOSI); //GPIO13 is HSPI MOSI pin (Master Data Out)
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTMS_U, FUNC_HSPI_CLK); //GPIO14 is HSPI CLK pin (Clock)
#if SPI_NOT_USE_CS == 0
		PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_HSPI_CS0); //GPIO15 is HSPI CS pin (Chip Select / Slave Select)
#endif
#endif
#endif

	spi_clock(USE_FIX_QSPI_FLASH * (1000/2) / freq, 2); // USE_FIX_QSPI_FLASH: 20, 26, 40, 80

#ifndef SPI_TINY
#ifndef SPI_BLOCK
	spi_tx_byte_order(SPI_BYTE_ORDER_HIGH_TO_LOW);
	spi_rx_byte_order(SPI_BYTE_ORDER_HIGH_TO_LOW);
#endif
#endif

#ifdef SPI_TINY
#ifndef SPI_BLOCK
	//disable MOSI, MISO, ADDR, COMMAND, DUMMY, etc in case previously set.
	CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY|SPI_WR_BYTE_ORDER|SPI_RD_BYTE_ORDER);

	//########## Setup Bitlengths ##########//
	WRITE_PERI_REG(SPI_USER1(spi_no), ((0-1)&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S | //Number of bits in Address
									  ((8-1)&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S | //Number of bits to Send = 8
									  ((0-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S |  //Number of bits to receive
									  ((0-1)&SPI_USR_DUMMY_CYCLELEN)<<SPI_USR_DUMMY_CYCLELEN_S); //Number of Dummy bits to insert
	//########## END SECTION ##########//

	//########## Setup DOUT data ##########//
	//enable MOSI function in SPI module, full-duplex mode
	SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI | SPI_DOUTDIN);
#endif
#endif

#ifdef SPI_QIO
	SET_PERI_REG_MASK(SPI_USER(HSPI), SPI_CS_SETUP|SPI_CS_HOLD|SPI_USR_COMMAND);
	CLEAR_PERI_REG_MASK(SPI_USER(HSPI), SPI_FLASH_MODE);
	uint8 cmd[1] = { 0x38 }; // Enter QPI mode
	spi_write_read_block(SPI_SEND+SPI_RECEIVE, 0, cmd, 1);

	SET_PERI_REG_MASK(SPI_CTRL(HSPI), SPI_QIO_MODE|SPI_FASTRD_MODE);
	SET_PERI_REG_MASK(SPI_USER(HSPI),SPI_FWRITE_QIO);
#endif

}

////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_mode
//   Description: Configures SPI mode parameters for clock edge and clock polarity.
//    Parameters:
//				  spi_cpha - (0) Data is valid on clock leading (rising) edge
//				             (1) Data is valid on clock trailing (falling) edge
//				  spi_cpol - (0) Clock is low when inactive
//				             (1) Clock is high when inactive
//
////////////////////////////////////////////////////////////////////////////////
/*
void ICACHE_FLASH_ATTR spi_mode(uint8 spi_cpha,uint8 spi_cpol){
	if(spi_cpha) {
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_CK_OUT_EDGE);
	} else {
		CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_CK_OUT_EDGE);
	}

	if (spi_cpol) {
		SET_PERI_REG_MASK(SPI_PIN(spi_no), SPI_IDLE_EDGE);
	} else {
		CLEAR_PERI_REG_MASK(SPI_PIN(spi_no), SPI_IDLE_EDGE);
	}
}
*/
////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_clock
//   Description: sets up the control registers for the SPI clock (when sysclk_as_spiclk = 0)
//    Parameters:
//				  prediv - predivider value (actual division value)
//				  cntdiv - postdivider value (actual division value)
//				  Set either divider to 0 to disable all division (80MHz sysclock)
//				 
////////////////////////////////////////////////////////////////////////////////

void ICACHE_FLASH_ATTR spi_clock(uint16 prediv, uint8 cntdiv){
	
	if((prediv==0)|(cntdiv==0)){

		WRITE_PERI_REG(SPI_CLOCK(spi_no), SPI_CLK_EQU_SYSCLK);

	} else {
	
		WRITE_PERI_REG(SPI_CLOCK(spi_no), 
					(((prediv-1)&SPI_CLKDIV_PRE)<<SPI_CLKDIV_PRE_S)|
					(((cntdiv-1)&SPI_CLKCNT_N)<<SPI_CLKCNT_N_S)|
					(((cntdiv>>1)&SPI_CLKCNT_H)<<SPI_CLKCNT_H_S)|
					((0&SPI_CLKCNT_L)<<SPI_CLKCNT_L_S));
	}

}

////////////////////////////////////////////////////////////////////////////////

#ifdef SPI_BLOCK

// Send (sr & SPI_SEND), Read (sr & SPI_RECEIVE): <SPI_ADDR_BITS> command + data(max 64 bytes), HSPI
// if SPI_SEND + SPI_RECEIVE = full-duplex (addr ignored)
void spi_write_read_block(uint8 sr, uint32 addr, uint8 * data, uint8 data_size)
{
	if(data_size > 64) return;
	while(spi_busy(spi_no)); //wait for SPI to be ready

#if SPI_NOT_USE_CS == 0 && DELAY_BEFORE_CHANGE_CS
	SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_CS_SETUP|SPI_CS_HOLD); // set delay before and after CS change
#endif
	CLEAR_PERI_REG_MASK(SPI_CTRL(spi_no), SPI_QIO_MODE|SPI_DIO_MODE|SPI_DOUT_MODE|SPI_QOUT_MODE);
	CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_FLASH_MODE|SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY|SPI_WR_BYTE_ORDER|SPI_RD_BYTE_ORDER);

	//########## Setup Bit-lengths ##########//
	uint32 d = ((SPI_ADDR_BITS-1)&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S; // Number of bits in Address
	if(sr & SPI_SEND) { // send
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI | (sr & SPI_RECEIVE ? SPI_DOUTDIN : 0)); // +receive full-duplex if set
		d |= ((data_size*8-1)&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S; // Number of bits to Send
		copy_s1d4((void *)SPI_W0(spi_no), data, data_size);
	} else {
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MISO);
		d |= ((data_size*8-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S; // Number of bits to receive
	}
	WRITE_PERI_REG(SPI_USER1(spi_no), d);

	if(sr != SPI_SEND + SPI_RECEIVE) {
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_ADDR); //enable ADDRess function in SPI module
		WRITE_PERI_REG(SPI_ADDR(spi_no), addr<<(32-SPI_ADDR_BITS)); //align address data to high bits
	}

	//########## Begin SPI Transaction ##########//
	SET_PERI_REG_MASK(SPI_CMD(spi_no), SPI_USR);

	while(spi_busy(spi_no));	//wait for SPI transaction to complete

	if(sr & SPI_RECEIVE) { // receive
		copy_s4d1(data, (void *)SPI_W0(spi_no), data_size);
		#if DEBUGSOO > 5
			os_printf("SPI_R: ");
			print_hex_dump(data, data_size, ' ');
			os_printf("\n");
		#endif
	}
}
#endif

#ifdef SPI_TINY

// Write & Read 8 bit in full duplex, HSPI
uint8 ICACHE_FLASH_ATTR spi_write_read_byte(uint8 dout_data)
{

	while(spi_busy(spi_no)); //wait for SPI to be ready

#ifdef SPI_BLOCK
	CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY|SPI_WR_BYTE_ORDER|SPI_RD_BYTE_ORDER);

	//########## Setup Bitlengths ##########//
	WRITE_PERI_REG(SPI_USER1(spi_no), ((0-1)&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S | //Number of bits in Address
									  ((8-1)&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S | //Number of bits to Send = 8
									  ((0-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S |  //Number of bits to receive
									  ((0-1)&SPI_USR_DUMMY_CYCLELEN)<<SPI_USR_DUMMY_CYCLELEN_S); //Number of Dummy bits to insert
	//########## END SECTION ##########//

	//########## Setup DOUT data ##########//
	//enable MOSI function in SPI module, full-duplex mode
	SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI | SPI_DOUTDIN);
#endif

	//copy data to W0
	WRITE_PERI_REG(SPI_W0(spi_no), dout_data); // 8 bits out
	//########## END SECTION ##########//

	//########## Begin SPI Transaction ##########//
	SET_PERI_REG_MASK(SPI_CMD(spi_no), SPI_USR);
	//########## END SECTION ##########//

	//########## Return DIN data ##########//
	while(spi_busy(spi_no));	//wait for SPI transaction to complete

#if DEBUGSOO > 4
	uint8 i = READ_PERI_REG(SPI_W0(spi_no)); // 8 bits.
	os_printf(" SPI: %x/%x ", dout_data, i);
	return i;
#else
	return READ_PERI_REG(SPI_W0(spi_no)); // 8 bits.
#endif
}

#endif
#ifndef SPI_TINY
#ifndef SPI_BLOCK

////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_tx_byte_order
//   Description: Setup the byte order for shifting data out of buffer
//    Parameters:
//				  byte_order - SPI_BYTE_ORDER_HIGH_TO_LOW (1) 
//							   Data is sent out starting with Bit31 and down to Bit0
//
//							   SPI_BYTE_ORDER_LOW_TO_HIGH (0)
//							   Data is sent out starting with the lowest BYTE, from 
//							   MSB to LSB, followed by the second lowest BYTE, from
//							   MSB to LSB, followed by the second highest BYTE, from
//							   MSB to LSB, followed by the highest BYTE, from MSB to LSB
//							   0xABCDEFGH would be sent as 0xGHEFCDAB
//
//				 
////////////////////////////////////////////////////////////////////////////////

void ICACHE_FLASH_ATTR spi_tx_byte_order(uint8 byte_order){

	if(byte_order){
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_WR_BYTE_ORDER);
	} else {
		CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_WR_BYTE_ORDER);
	}
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_rx_byte_order
//   Description: Setup the byte order for shifting data into buffer
//    Parameters:
//				  byte_order - SPI_BYTE_ORDER_HIGH_TO_LOW (1) 
//							   Data is read in starting with Bit31 and down to Bit0
//
//							   SPI_BYTE_ORDER_LOW_TO_HIGH (0)
//							   Data is read in starting with the lowest BYTE, from 
//							   MSB to LSB, followed by the second lowest BYTE, from
//							   MSB to LSB, followed by the second highest BYTE, from
//							   MSB to LSB, followed by the highest BYTE, from MSB to LSB
//							   0xABCDEFGH would be read as 0xGHEFCDAB
//
//				 
////////////////////////////////////////////////////////////////////////////////

void ICACHE_FLASH_ATTR spi_rx_byte_order(uint8 spi_no, uint8 byte_order)
{
	if(byte_order){
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_RD_BYTE_ORDER);
	} else {
		CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_RD_BYTE_ORDER);
	}
}
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
//
// Function Name: spi_transaction
//   Description: SPI transaction function
//    Parameters:
//				  cmd_bits - actual number of bits to transmit
//				  cmd_data - command data
//				  addr_bits - actual number of bits to transmit
//				  addr_data - address data
//				  dout_bits - actual number of bits to transmit
//				  dout_data - output data
//				  din_bits - actual number of bits to receive
//				  
//		 Returns: read data - uint32 containing read in data only if RX was set 
//				  0 - something went wrong (or actual read data was 0)
//				  1 - data sent ok (or actual read data is 1)
//				  Note: all data is assumed to be stored in the lower bits of
//				  the data variables (for anything <32 bits). 
//
////////////////////////////////////////////////////////////////////////////////

uint32 ICACHE_FLASH_ATTR spi_transaction(uint8 cmd_bits, uint16 cmd_data, uint32 addr_bits, uint32 addr_data, uint32 dout_bits, uint32 dout_data,
				uint32 din_bits, uint32 dummy_bits)
{

	//code for custom Chip Select as GPIO PIN here

	while(spi_busy(spi_no)); //wait for SPI to be ready	

//########## Enable SPI Functions ##########//
	//disable MOSI, MISO, ADDR, COMMAND, DUMMY in case previously set.
	CLEAR_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI|SPI_USR_MISO|SPI_USR_COMMAND|SPI_USR_ADDR|SPI_USR_DUMMY);

	//enable functions based on number of bits. 0 bits = disabled. 
	//This is rather inefficient but allows for a very generic function.
	//CMD ADDR and MOSI are set below to save on an extra if statement.
//	if(cmd_bits) {SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_COMMAND);}
//	if(addr_bits) {SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_ADDR);}
	if(din_bits) {SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MISO);}
	if(dummy_bits) {SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_DUMMY);}
//########## END SECTION ##########//

//########## Setup Bitlengths ##########//
	WRITE_PERI_REG(SPI_USER1(spi_no), ((addr_bits-1)&SPI_USR_ADDR_BITLEN)<<SPI_USR_ADDR_BITLEN_S | //Number of bits in Address
									  ((dout_bits-1)&SPI_USR_MOSI_BITLEN)<<SPI_USR_MOSI_BITLEN_S | //Number of bits to Send
									  ((din_bits-1)&SPI_USR_MISO_BITLEN)<<SPI_USR_MISO_BITLEN_S |  //Number of bits to receive
									  ((dummy_bits-1)&SPI_USR_DUMMY_CYCLELEN)<<SPI_USR_DUMMY_CYCLELEN_S); //Number of Dummy bits to insert
//########## END SECTION ##########//

//########## Setup Command Data ##########//
	if(cmd_bits) {
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_COMMAND); //enable COMMAND function in SPI module
		uint16 command = cmd_data << (16-cmd_bits); //align command data to high bits
		command = ((command>>8)&0xff) | ((command<<8)&0xff00); //swap byte order
		WRITE_PERI_REG(SPI_USER2(spi_no), ((((cmd_bits-1)&SPI_USR_COMMAND_BITLEN)<<SPI_USR_COMMAND_BITLEN_S) | (command&SPI_USR_COMMAND_VALUE)));
	}
//########## END SECTION ##########//

//########## Setup Address Data ##########//
	if(addr_bits){
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_ADDR); //enable ADDRess function in SPI module
		WRITE_PERI_REG(SPI_ADDR(spi_no), addr_data<<(32-addr_bits)); //align address data to high bits
	}
	

//########## END SECTION ##########//	

//########## Setup DOUT data ##########//
	if(dout_bits) {
		SET_PERI_REG_MASK(SPI_USER(spi_no), SPI_USR_MOSI); //enable MOSI function in SPI module
	//copy data to W0
	if(READ_PERI_REG(SPI_USER(spi_no))&SPI_WR_BYTE_ORDER) {
		WRITE_PERI_REG(SPI_W0(spi_no), dout_data<<(32-dout_bits));
	} else {

		uint8 dout_extra_bits = dout_bits%8;

		if(dout_extra_bits){
			//if your data isn't a byte multiple (8/16/24/32 bits)and you don't have SPI_WR_BYTE_ORDER set, you need this to move the non-8bit remainder to the MSBs
			//not sure if there's even a use case for this, but it's here if you need it...
			//for example, 0xDA4 12 bits without SPI_WR_BYTE_ORDER would usually be output as if it were 0x0DA4, 
			//of which 0xA4, and then 0x0 would be shifted out (first 8 bits of low byte, then 4 MSB bits of high byte - ie reverse byte order). 
			//The code below shifts it out as 0xA4 followed by 0xD as you might require. 
			WRITE_PERI_REG(SPI_W0(spi_no), ((0xFFFFFFFF<<(dout_bits - dout_extra_bits)&dout_data)<<(8-dout_extra_bits) | ((0xFFFFFFFF>>(32-(dout_bits - dout_extra_bits)))&dout_data)));
		} else {
			WRITE_PERI_REG(SPI_W0(spi_no), dout_data);
		}
	}
	}
//########## END SECTION ##########//

//########## Begin SPI Transaction ##########//
	SET_PERI_REG_MASK(SPI_CMD(spi_no), SPI_USR);
//########## END SECTION ##########//

//########## Return DIN data ##########//
	if(din_bits) {
		while(spi_busy(spi_no));	//wait for SPI transaction to complete
		
		if(READ_PERI_REG(SPI_USER(spi_no))&SPI_RD_BYTE_ORDER) {
			return READ_PERI_REG(SPI_W0(spi_no)) >> (32-din_bits); //Assuming data in is written to MSB. TBC
		} else {
			return READ_PERI_REG(SPI_W0(spi_no)); //Read in the same way as DOUT is sent. Note existing contents of SPI_W0 remain unless overwritten! 
		}

		return 0; //something went wrong
	}
//########## END SECTION ##########//
	while(spi_busy(spi_no)); //prevents GPIO pin state change while SPI operation is in process
	//Transaction completed
	return 1; //success
}

////////////////////////////////////////////////////////////////////////////////

#endif
#endif

#endif
