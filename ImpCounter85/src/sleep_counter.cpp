
#include <Arduino.h>

#include <avr/sleep.h>
#include <avr/power.h>    
#include <avr/wdt.h>
#include "sleep_counter.h"

volatile int16_t wdt_count; //not unsigned, cause timer can lost 0 

/* Watchdog interrupt vector */
ISR( WDT_vect ) { 
	wdt_count--;
}  

/* Makes a deep sleep for X second using as little power as possible */
// 32767 minutes ~ 22 days
void gotoDeepSleep(uint16_t minutes, Counter *counter, Counter *counter2) 
{
	//pinMode( ESP_RESET_PIN, OUTPUT );  //for test
	power_all_disable();  // power off ADC, Timer 0 and 1, serial interface

	set_sleep_mode( SLEEP_MODE_PWR_DOWN );

	resetWatchdog();      // get watchdog ready

	//30 = 2c сон
	//60 * 4 = 240 раз/сек при 250мс сон (лучше 239, т.к. 1.2мс * 240 = 288мс будем считать)
	//62.5 * 60 раз - 16мс
	//31.25 * 60 раз - 32мс
	for (uint16_t i = 0; i < 240UL; ++i)  
	{
		wdt_count = minutes; 
		while ( wdt_count > 0 ) 
		{
			counter->check();
			#ifdef BUTTON2_PIN
				counter2->check();
			#endif

			//digitalWrite( ESP_RESET_PIN, LOW );
			sleep_mode();
			//digitalWrite( ESP_RESET_PIN, HIGH );
		}
	}
		
	wdt_disable();        // disable watchdog
	MCUSR = 0;
	//noInterrupts();       // timed sequence coming up
	power_all_enable();   // power everything back on

}

/* Prepare watchdog */
void resetWatchdog() 
{
	MCUSR = 0; // clear various "reset" flags 
	// MCUSR нужно ли ? MCU Status Register
	WDTCR = bit( WDCE ) | bit( WDE ); // allow changes, disable reset, clear existing interrupt
	// set interrupt mode and an interval (WDE must be changed from 1 to 0 here)
	//WDTCR = bit( WDIE );    // set WDIE, and 16 ms
	//WDTCR = bit( WDIE ) | bit( WDP0 );    // set WDIE, and 32 ms
	WDTCR = bit( WDIE ) | bit( WDP2 );    // set WDIE, and 0.25 seconds delay
	//WDTCR = bit( WDIE ) | bit( WDP2 ) | bit( WDP0 );    // set WDIE, and 0.5 seconds delay
	//WDTCR = bit( WDIE ) | bit( WDP2 ) | bit( WDP1 );    // set WDIE, and 1 seconds delay
	//WDTCR = bit( WDIE ) | bit( WDP2 ) | bit( WDP1 ) | bit( WDP0 );    // set WDIE, and 2 seconds delay
	//WDTCR = bit( WDIE ) | bit( WDP3 ) | bit( WDP0 );    // set WDIE, and 8 seconds delay
														
	wdt_reset(); // pat the dog
} 


Counter::Counter()
	: i(0)
	, state(false)
{
	debounce.setSensitivity(4);
}

void Counter::check()
{
	pinMode(pin, INPUT_PULLUP);

	if (debounce.pin(pin) == LOW)
	{
		if (state == false)
		{
			delayMicroseconds(30000UL);  //delay doesn't work cause power_all_disable !!
			if (debounce.pin(pin) == LOW)
			{
				i++;
				state = true;
			}
		}
	}
	else
	{
		state = false;
	}

	pinMode(pin, INPUT);
}
