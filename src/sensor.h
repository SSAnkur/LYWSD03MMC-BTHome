#pragma once
#include "tl_common.h"

/* Temperature/humidity sensor of the LYWSD03MMC B1.5:
 * SHTV3 (SHTC3-compatible) at I2C address 0x70.
 *
 * A measurement is split in two so the CPU can sleep while the sensor
 * converts (as pvvx SENSOR_SLEEP_MEASURE does):
 *   sensor_start()  wake the sensor and trigger a conversion
 *   ... SENSOR_MEAS_TICKS later ...
 *   sensor_result() read the result and put the sensor back to sleep */
#define SENSOR_MEAS_US		12500	/* SHTC3 normal mode: max 12.1 ms */
#define SENSOR_MEAS_TICKS	(SENSOR_MEAS_US * CLOCK_16M_SYS_TIMER_CLK_1US)

int sensor_init(void);				/* 0 = sensor answered */
int sensor_start(void);				/* 0 = conversion started */
int sensor_result(s32 *t_mc, s32 *rh_mpct);	/* milli-degC, milli-%; 0 = ok */
void sensor_sleep(void);			/* force sleep (e.g. low battery) */
