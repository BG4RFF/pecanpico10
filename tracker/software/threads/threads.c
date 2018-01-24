#include "ch.h"
#include "hal.h"

#include "tracking.h"
#include "watchdog.h"
#include "pi2c.h"
#include "pac1720.h"

sysinterval_t watchdog_tracking;

void start_essential_threads(void) {
	init_watchdog();				// Init watchdog
	pi2cInit();						// Initialize I2C
	pac1720_init();					// Initialize current measurement
	init_tracking_manager(false);	// Initialize tracking manager (without GPS, GPS is initialized if needed by position thread)
	//startReceiver();				// Start APRS receiver
	chThdSleep(TIME_MS2I(300));		// Wait for tracking manager to initialize
}

