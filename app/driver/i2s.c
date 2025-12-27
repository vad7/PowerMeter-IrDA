/*
 * I2S DMA
 *
 */

#include "user_config.h"

#ifdef USE_I2S

#include "driver/i2s.h"
#include "hw/esp8266.h"
#include "hw/pin_mux_register.h"
#include "hw/i2s_reg.h"
#include "hw/slc_register.h"
#include "hw/sdio_slv.h"

#include "sdk/mem_manager.h"

void uart_wait_tx_fifo_empty(void) ICACHE_FLASH_ATTR;

#define BASEFREQ (160000000L)

#ifndef i2c_bbpll
#define i2c_bbpll							0x67
#define i2c_bbpll_en_audio_clock_out		4
#define i2c_bbpll_en_audio_clock_out_msb	7
#define i2c_bbpll_en_audio_clock_out_lsb	7
#define i2c_bbpll_hostid					4

#ifndef ETS_SLC_INUM
#define ETS_SLC_INUM       1
#endif
#endif

#if USE_I2S_DMA

static uint32_t i2s_slc_queue[I2SDMABUFCNT-1];
static uint8_t i2s_slc_queue_len;
//static uint32_t *i2s_slc_buf_pntr[I2SDMABUFCNT]; //Pointer to the I2S DMA buffer data
//static struct sdio_queue i2s_slc_items[I2SDMABUFCNT]; //I2S DMA buffer descriptors
static uint32_t *i2s_curr_slc_buf=NULL;//current buffer for writing
static int i2s_curr_slc_buf_pos=0; //position in the current buffer

bool ICACHE_FLASH_ATTR i2s_is_full(){
  return (i2s_curr_slc_buf_pos == I2SDMABUFLEN || i2s_curr_slc_buf==NULL) && (i2s_slc_queue_len == 0);
}

bool ICACHE_FLASH_ATTR i2s_is_empty(){
  return (i2s_slc_queue_len >= I2SDMABUFCNT-1);
}

uint32_t i2s_slc_queue_next_item(){ //pop the top off the queue
  uint8_t i;
  uint32_t item = i2s_slc_queue[0];
  i2s_slc_queue_len--;
  for(i=0;i<i2s_slc_queue_len;i++)
    i2s_slc_queue[i] = i2s_slc_queue[i+1];
  return item;
}

//This routine is called as soon as the DMA routine has something to tell us. All we
//handle here is the RX_EOF_INT status, which indicate the DMA has sent a buffer whose
//descriptor has the 'EOF' field set to 1.
void i2s_slc_isr(void) {
	struct sdio_queue *finishedDesc;
	uint32 slc_intr_status;

	//__wrap_os_printf_plus("i2s isr\n"); uart_wait_tx_fifo_empty();

	//Grab int status
	slc_intr_status = READ_PERI_REG(SLC_INT_STATUS);
	//clear all intr flags
	WRITE_PERI_REG(SLC_INT_CLR, slc_intr_status); // 0xffffffff);
	if (slc_intr_status & SLC_RX_EOF_INT_ST) {
		ETS_SLC_INTR_DISABLE();
		//The DMA subsystem is done with this block: Push it on the queue so it can be re-used.
		finishedDesc = (struct sdio_queue*)READ_PERI_REG(SLC_RX_EOF_DES_ADDR);
		//os_memset((void *)finishedDesc->buf_ptr, 0x00, I2SDMABUFLEN * 4);//zero the buffer so it is mute in case of underflow
	    if (i2s_slc_queue_len >= I2SDMABUFCNT-1) { //All buffers are empty. This means we have an underflow
			underrunCnt++;
	    	i2s_slc_queue_next_item(); //free space for finished_item
	    }
	    i2s_slc_queue[i2s_slc_queue_len++] = finishedDesc->buf_ptr;
	    ETS_SLC_INTR_ENABLE();
	}
}

//Initialize I2S subsystem for DMA circular buffer use
void ICACHE_FLASH_ATTR i2sInit(int rate, int lockBitcount, uint32 sample) {
	
	underrunCnt = 0;

	//Initialize DMA buffer descriptors in such a way that they will form a circular
	//buffer.Set
	i2s_slc_queue_len = 0;
	int x, y;
	for (x=0; x<I2SDMABUFCNT; x++) {
		i2s_slc_buf_pntr[x] = os_malloc(I2SDMABUFLEN*4);
		for (y=0; y<I2SDMABUFLEN; y++) i2s_slc_buf_pntr[x][y] = sample;

		i2s_slc_items[x].unused = 0;
		i2s_slc_items[x].owner = 1;
		i2s_slc_items[x].eof = 1;
		i2s_slc_items[x].sub_sof = 0;
		i2s_slc_items[x].datalen = I2SDMABUFLEN*4;
		i2s_slc_items[x].blocksize = I2SDMABUFLEN*4;
		i2s_slc_items[x].buf_ptr = (uint32_t)&i2s_slc_buf_pntr[x][0];
		i2s_slc_items[x].next_link_ptr = (int)((x<(I2SDMABUFCNT-1))?(&i2s_slc_items[x+1]):(&i2s_slc_items[0]));
	}

	ETS_SLC_INTR_DISABLE();
	//Feed dma the 1st buffer desc addr
	//To send data to the I2S subsystem, counter-intuitively we use the RXLINK part, not the TXLINK as you might
	//expect. The TXLINK part still needs a valid DMA descriptor, even if it's unused: the DMA engine will throw
	//an error at us otherwise. Just feed it any random descriptor.
	CLEAR_PERI_REG_MASK(SLC_TX_LINK, SLC_TXLINK_DESCADDR_MASK << SLC_TXLINK_ADDR_S);
	SET_PERI_REG_MASK(SLC_TX_LINK, (((uint32)&i2s_slc_items[1]) & SLC_TXLINK_DESCADDR_MASK) << SLC_TXLINK_ADDR_S); //any random desc is OK, we don't use TX but it needs something valid
	CLEAR_PERI_REG_MASK(SLC_RX_LINK,SLC_RXLINK_DESCADDR_MASK);
	SET_PERI_REG_MASK(SLC_RX_LINK, (((uint32)&i2s_slc_items[0]) & SLC_RXLINK_DESCADDR_MASK) << SLC_TXLINK_ADDR_S);
	//Attach the DMA interrupt
	ETS_SLC_INTR_ATTACH(i2s_slc_isr, NULL);

	//Enable DMA operation intr
	WRITE_PERI_REG(SLC_INT_ENA,  SLC_RX_EOF_INT_ENA);


	//clear any interrupt flags that are set
	WRITE_PERI_REG(SLC_INT_CLR, 0xffffffff);

	ETS_SLC_INTR_ENABLE();
	//Reset DMA
	SET_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);
	CLEAR_PERI_REG_MASK(SLC_CONF0, SLC_RXLINK_RST|SLC_TXLINK_RST);
	//Enable and configure DMA
	CLEAR_PERI_REG_MASK(SLC_CONF0, (SLC_MODE<<SLC_MODE_S));
	SET_PERI_REG_MASK(SLC_CONF0,(1<<SLC_MODE_S));

	SET_PERI_REG_MASK(SLC_RX_DSCR_CONF,SLC_INFOR_NO_REPLACE|SLC_TOKEN_NO_REPLACE);
	CLEAR_PERI_REG_MASK(SLC_RX_DSCR_CONF, SLC_RX_FILL_EN|SLC_RX_EOF_MODE | SLC_RX_FILL_MODE);
	
	//We use a queue to keep track of the DMA buffers that are empty. The ISR will push buffers to the back of the queue,
	//the mp3 decode will pull them from the front and fill them. For ease, the queue will contain *pointers* to the DMA
	//buffers, not the data itself. The queue depth is one smaller than the amount of buffers we have, because there's
	//always a buffer that is being used by the DMA subsystem *right now* and we don't want to be able to write to that
	//simultaneously.

	//Start transmission
	SET_PERI_REG_MASK(SLC_TX_LINK, SLC_TXLINK_START);
	SET_PERI_REG_MASK(SLC_RX_LINK, SLC_RXLINK_START);

//----

	//Init pins to i2s functions
	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_U0RXD_U, FUNC_I2SO_DATA);
	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_I2SO_WS);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_I2SO_BCK);

	//Enable clock to i2s subsystem
	i2c_writeReg_Mask_def(i2c_bbpll, i2c_bbpll_en_audio_clock_out, 1);

	//Reset I2S subsystem
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	SET_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);
	CLEAR_PERI_REG_MASK(I2SCONF,I2S_I2S_RESET_MASK);


	//Select 16bits per channel (FIFO_MOD=0), no DMA access (FIFO only)
	CLEAR_PERI_REG_MASK(I2S_FIFO_CONF, I2S_I2S_DSCR_EN|(I2S_I2S_RX_FIFO_MOD<<I2S_I2S_RX_FIFO_MOD_S)|(I2S_I2S_TX_FIFO_MOD<<I2S_I2S_TX_FIFO_MOD_S));
	//Enable DMA in i2s subsystem
	SET_PERI_REG_MASK(I2S_FIFO_CONF, I2S_I2S_DSCR_EN);

	//tx/rx binaureal
	CLEAR_PERI_REG_MASK(I2SCONF_CHAN, (I2S_TX_CHAN_MOD<<I2S_TX_CHAN_MOD_S)|(I2S_RX_CHAN_MOD<<I2S_RX_CHAN_MOD_S));

	//Clear int
	SET_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	CLEAR_PERI_REG_MASK(I2SINT_CLR, I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);

	//No idea if ints are needed...
	//clear int
	SET_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	CLEAR_PERI_REG_MASK(I2SINT_CLR,   I2S_I2S_TX_REMPTY_INT_CLR|I2S_I2S_TX_WFULL_INT_CLR|
			I2S_I2S_RX_WFULL_INT_CLR|I2S_I2S_PUT_DATA_INT_CLR|I2S_I2S_TAKE_DATA_INT_CLR);
	//enable int
	SET_PERI_REG_MASK(I2SINT_ENA,   I2S_I2S_TX_REMPTY_INT_ENA|I2S_I2S_TX_WFULL_INT_ENA|
	I2S_I2S_RX_REMPTY_INT_ENA|I2S_I2S_TX_PUT_DATA_INT_ENA|I2S_I2S_RX_TAKE_DATA_INT_ENA);

	i2sSetRate(rate, lockBitcount);

	//Start transmission
	SET_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_START);
}

//This routine pushes a single, 32-bit sample to the I2S buffers. Call this at (on average)
//at least the current sample rate. You can also call it quicker: it will suspend the calling
//thread if the buffer is full and resume when there's room again.
bool i2sPushSample(unsigned int sample) {
	if (i2s_curr_slc_buf_pos==I2SDMABUFLEN || i2s_curr_slc_buf==NULL) {
		if(i2s_slc_queue_len == 0){
			return false;
		}
		ETS_SLC_INTR_DISABLE();
		i2s_curr_slc_buf = (uint32_t *)i2s_slc_queue_next_item();
		ETS_SLC_INTR_ENABLE();
		i2s_curr_slc_buf_pos=0;
	}
	i2s_curr_slc_buf[i2s_curr_slc_buf_pos++]=sample;
	return true;
}
#endif

#ifdef I2S_CLOCK_OUT

#define ABS(x) (((x)>0)?(x):(-(x)))

//Set the I2S sample rate, in HZ, Find closest divider
void ICACHE_FLASH_ATTR i2sSetRate(int rate, int lockBitcount) {
	/*
		CLK_I2S = 160MHz / I2S_CLKM_DIV_NUM
		BCLK clock out = CLK_I2S / I2S_BCK_DIV_NUM (duty cycle is not stable, maybe +-4%)
		WS clock out = BCLK/ 2 / (16 + I2S_BITS_MOD)
		Note that I2S_CLKM_DIV_NUM must be >5 for I2S data
		I2S_CLKM_DIV_NUM - 5-63
		I2S_BCK_DIV_NUM - 2-63
		
		We also have the option to send out more than 2x16 bit per sample. Most I2S codecs will
		ignore the extra bits and in the case of the 'fake' PWM/delta-sigma outputs, they will just lower the output
		voltage a bit, so we add them when it makes sense. Some of them, however, won't accept it, that's
		why we have the option not to do this.
	*/
	int bestclkmdiv = 0, bestbckdiv = 0, bestbits = 0, bestfreq=0;
	int tstfreq;
	int bckdiv, clkmdiv, bits;
	for(bckdiv=2; bckdiv<64; bckdiv++) {
		for(clkmdiv=5; clkmdiv<64; clkmdiv++) {
			for(bits=16; bits<(lockBitcount?17:20); bits++) {
				tstfreq=BASEFREQ/(bckdiv*clkmdiv*bits*2);
				if (ABS(rate-tstfreq)<ABS(rate-bestfreq)) {
					bestfreq=tstfreq;
					bestclkmdiv=clkmdiv;
					bestbckdiv=bckdiv;
					bestbits=bits;
				}
			}
		}
	}
	#if DEBUGSOO > 0
		os_printf("ReqRate: %d, MDiv: %d, BckDiv: %d, Bits: %d,  Frq: %d\n",
					rate, bestclkmdiv, bestbckdiv, bestbits, (int)(BASEFREQ/(bestbckdiv*bestclkmdiv*bestbits*2)));
		uart_wait_tx_fifo_empty();
	#endif
    WRITE_PERI_REG(I2SCONF, (READ_PERI_REG(I2SCONF) & ~((I2S_BCK_DIV_NUM<<I2S_BCK_DIV_NUM_S) | (I2S_CLKM_DIV_NUM<<I2S_CLKM_DIV_NUM_S) | (I2S_BITS_MOD<<I2S_BITS_MOD_S))) |
    		(((bestclkmdiv)&I2S_CLKM_DIV_NUM)<<I2S_CLKM_DIV_NUM_S)|
			(((bestbckdiv)&I2S_BCK_DIV_NUM )<<I2S_BCK_DIV_NUM_S)|
			((bestbits-16)<<I2S_BITS_MOD_S));
}

// GPIO2 as clock out
void ICACHE_FLASH_ATTR i2s_clock_out(void)
{
	rom_i2c_writeReg_Mask(i2c_bbpll, i2c_bbpll_hostid, i2c_bbpll_en_audio_clock_out,
		  i2c_bbpll_en_audio_clock_out_msb, i2c_bbpll_en_audio_clock_out_lsb, 1);

	CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);
	SET_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);
	CLEAR_PERI_REG_MASK(I2SCONF, I2S_I2S_RESET_MASK);

	//i2sSetRate(153600, 0);

	// 9600 Hz (actual = 9603)
//	WRITE_PERI_REG(I2SCONF, (READ_PERI_REG(I2SCONF) & ((I2S_BCK_DIV_NUM<<I2S_BCK_DIV_NUM_S) | (I2S_CLKM_DIV_NUM<<I2S_CLKM_DIV_NUM_S) | (I2S_BITS_MOD<<I2S_BITS_MOD_S))) |
//		((49 & I2S_CLKM_DIV_NUM) << I2S_CLKM_DIV_NUM_S)|
//		((10 & I2S_BCK_DIV_NUM) << I2S_BCK_DIV_NUM_S)|
//		((17-16) << I2S_BITS_MOD_S));

	// 153600 Hz (actual = 151515)
	WRITE_PERI_REG(I2SCONF, (READ_PERI_REG(I2SCONF) & ((I2S_BCK_DIV_NUM<<I2S_BCK_DIV_NUM_S) | (I2S_CLKM_DIV_NUM<<I2S_CLKM_DIV_NUM_S) | (I2S_BITS_MOD<<I2S_BITS_MOD_S))) |
		((11 & I2S_CLKM_DIV_NUM) << I2S_CLKM_DIV_NUM_S)|
		((3 & I2S_BCK_DIV_NUM) << I2S_BCK_DIV_NUM_S)|
		((16-16) << I2S_BITS_MOD_S));

	//PIN_FUNC_SELECT(PERIPHS_IO_MUX_MTDO_U, FUNC_I2SO_BCK);
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO2_U, FUNC_I2SO_WS);
	SET_PERI_REG_MASK(I2SCONF, I2S_I2S_TX_START);
}
#endif

#endif
