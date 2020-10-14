/*
 * WatchdogESP01.c
 *
 * Created: 13.10.2020 13:21:00
 *  Author: Vadim Kulakov, vad7@yahoo.com
 * 
 * ATTiny13A
 *
 * Connections:
 * pin 3 (PB4) -> to POWER switch ESP-01 board (high level = ON)
 * pin 5 (PB0) <- input signal esp8266 GPIO0
 * pin 6 (PB1) <- input signal esp8266 GPIO2
 *
 * When GPIO0 and GPIO2 low level for 1 sec - POWER OFF for 5 sec
 */ 
#define F_CPU 128000UL
// Fuses(0=active): BODLEVEL = 1.8V (BODLEVEL[1:0] = 10), RSTDISBL=0, CKDIV8=1, CKSEL[1:0]=00

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#define POWER_PIN		(1<<PORTB4)
#define POWER_ON		PORTB |= POWER_PIN
#define POWER_OFF		PORTB &= ~POWER_PIN

#define CHECK_GPIO		((PINB & ((1<<PORTB1)|(1<<PORTB0))) == 0)

#define PULSE_TYPE		1 // 0 - low level, 1 - any change, 2 - falling edge, 3 - rising edge
#if PULSE_TYPE == 0
#define PULSE_SETUP		((0<<ISC01) | (0<<ISC00)) // low level
#error not implemented yet!
#elif PULSE_TYPE == 1
#define PULSE_SETUP		((0<<ISC01) | (1<<ISC00)) // any change
#elif PULSE_TYPE == 2
#define PULSE_SETUP		((1<<ISC01) | (0<<ISC00)) // falling edge
#elif PULSE_TYPE == 3
#define PULSE_SETUP		((1<<ISC01) | (1<<ISC00)) // rising edge
#endif

uint8_t watchdog_setup;

struct _EEPROM {
	uint8_t  watchdog_setup;	// ms: 0=16,1=32,2=64,3=125,4=250,5=500,6=1000,7=2000,32=4000,33=8000
	uint16_t power_off_time;	// ms
	uint16_t check_time;		// ms, Time when CHECK_GPIO is true to POWER OFF
	uint16_t check_delay;		// s, Delay after switch power
} __attribute__ ((packed));
struct _EEPROM EEMEM EEPROM;

//ISR(INT0_vect) 
//{
//	wdt_reset();
//}

ISR(WDT_vect)
{
//	POWER_OFF;
}

void Delay1ms(uint16_t ms)
{
	while(ms-- > 0) {
		_delay_ms(1);
		wdt_reset();
	}
}

void Delay1s(uint16_t s)
{
	if(s == 0xFFFF) s = 600;
	while(s-- > 0) {
		Delay1ms(1000);
	}
}

int main(void)
{
	CLKPR = (1<<CLKPCE); CLKPR = (0<<CLKPS3) | (0<<CLKPS2) | (0<<CLKPS1) | (0<<CLKPS0); // Clock prescaler: 1
	DDRB = POWER_PIN;
	PORTB = (1<<PORTB5) | (1<<PORTB3) | (1<<PORTB2); // unused pullup
	PRR = (1<<PRADC) | (1<<PRTIM0); // ADC,Timer power off
	MCUCR = (1<<SE) | (1<<SM1) | (0<<SM0) | PULSE_SETUP; // Sleep idle enable, Power-Down mode, Any logical change on INT0 generates an interrupt
	watchdog_setup = eeprom_read_byte(&EEPROM.watchdog_setup);
	if(watchdog_setup == 0xFF) watchdog_setup = (0<<WDP3) | (1<<WDP2) | (1<<WDP1) | (1<<WDP0); // 2 s
	watchdog_setup |= (1<<WDE) | (1<<WDTIE);
	WDTCR = (1<<WDCE) | (1<<WDE); WDTCR = watchdog_setup; //  Watchdog setup
	//GIMSK |= (1<<INT0); // INT0: External Interrupt Request 0 Enable
	//GIFR = (1<<INTF0);
	sei();
	Delay1ms(100);
	POWER_ON;
	Delay1s(eeprom_read_word(&EEPROM.check_delay));
    while(1)
	{
		sleep_cpu();	// power down
		WDTCR = (1<<WDCE) | (1<<WDE); WDTCR = watchdog_setup; //  Watchdog setup
		if(CHECK_GPIO) {
		 	uint16_t ms = eeprom_read_word(&EEPROM.check_time);
			if(ms == 0xFFFF) ms = 5000;
		 	while(ms-- > 0) {
			 	wdt_reset();
				_delay_ms(1);
				if(!CHECK_GPIO) break;
		 	}
			if(CHECK_GPIO) {
				POWER_OFF;
				ms = eeprom_read_word(&EEPROM.power_off_time);
				if(ms == 0xFFFF) ms = 10000;
				Delay1ms(ms);
				POWER_ON;
				Delay1ms(eeprom_read_word(&EEPROM.check_delay));
			}
		}
    }
}
