#pragma once
#include "tl_common.h"

/* Low-battery protection, thresholds as pvvx (battery.h, CR2032 build):
 * below BOOT_VBAT_MV at power-on the device sleeps again before touching
 * flash or the radio; below END_VBAT_MV while running it shows "Lo" and
 * sleeps for LOW_VBAT_SLEEP_S, then retries. */
#define BOOT_VBAT_MV		2200
#define END_VBAT_MV		2000
#define LOW_VBAT_SLEEP_S	180

u16 battery_read_mv(void);	/* VDD (= CR2032) in mV */
