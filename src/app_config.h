/*
 * Board / build configuration: Xiaomi LYWSD03MMC, hardware B1.5 ONLY
 * as sold since 2025 (pvvx reports it as HW B1.1): TLSR8251F512ET24,
 * SHTV3 sensor, SPI-driven LCD controller
 *
 * Pin map (from pvvx/ATC_MiThermometer reverse engineering):
 *   PC2  I2C SCL (SHTV3 sensor, 0x70), 10k pull-up
 *   PC3  I2C SDA, 10k pull-up
 *   PB6  LCD controller pin, must be pulled up (10k)
 *   PB0  not bonded on this package: driven high and sampled by the ADC
 *        to measure VDD (= CR2032 voltage)
 *   PD7  LCD SPI-CLK
 *   PB7  LCD SPI-SDI
 */
#pragma once

#define CLOCK_SYS_CLOCK_HZ	24000000
#define SYS_CLK_TYPE		SYS_CLK_24M_Crystal
enum {
	CLOCK_SYS_CLOCK_1S  = CLOCK_SYS_CLOCK_HZ,
	CLOCK_SYS_CLOCK_1MS = (CLOCK_SYS_CLOCK_1S / 1000),
	CLOCK_SYS_CLOCK_1US = (CLOCK_SYS_CLOCK_1S / 1000000),
};

/* Low-power busy wait: CPU stalled, woken by timer0 */
#define pm_wait_us(t)	cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1US)
#define pm_wait_ms(t)	cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1MS)

/* Variables that must survive deep-sleep retention */
#define RAM		_attribute_data_retention_

/* ---- Application settings (equivalents of the Zephyr build) ---- */
#define DEV_NAME		"LYWSD03MMC"	/* CONFIG_BT_DEVICE_NAME */
#define ADV_INTERVAL		16000		/* 10000 ms / 0.625 ms */
#define ADV_INTERVAL_DELAY	10		/* + up to 6.25 ms, as pvvx */
#define MEAS_EVERY_N_ADV	6		/* measure every 6th adv = 60 s */
#define MEAS_INTERVAL_S		((ADV_INTERVAL * MEAS_EVERY_N_ADV) / 1600)
#define RF_TX_POWER		RF_POWER_P0p04dBm /* CONFIG_BT_CTLR_TX_PWR_0 */
#define LOW_BATT_MV		2400		/* show battery icon below this */

/* Local name in the advertising packet as well as the scan response.
 * 1: passive scanners (e.g. ESPHome with active: false) see the name.
 * 0 (pvvx default): name only in the scan response; every advertising
 * packet is 12 bytes shorter, but only active scanners see the name. */
#define ADV_NAME_IN_ADV		1

/* ---- OTA window ----
 * Connections (and with them OTA) are only accepted for OTA_WINDOW_S after
 * the battery is inserted. Then the device switches to non-connectable
 * advertising until the next power cycle. A reboot without a power cycle
 * (low-battery sleep, OTA reboot, crash) does not reopen the window. */
#define OTA_WINDOW_S		120
#define OTA_WINDOW_ADV_INTERVAL	1600		/* 1 s while the window is open */

/* ---- Connection / OTA (values from pvvx ble.h / app.c) ---- */
#define CONN_INTERVAL		16	/* 16 * 1.25 ms = 20 ms */
#define CONN_LATENCY		49	/* (49 + 1) * 20 ms = 1 s between radio events */
#define CONN_TIMEOUT		400	/* x10 ms = 4 s */
#define OTA_IDLE_TIMEOUT_US	(16 * 1000000)	/* abort OTA after 16 s without data */


/* ---- I2C ---- */
#define I2C_GROUP		I2C_GPIO_GROUP_C2C3
#define I2C_SCL			GPIO_PC2
#define I2C_SDA			GPIO_PC3
#define I2C_SPEED_HZ		700000	/* as pvvx I2C_MAX_SPEED; SHTC3 allows 1 MHz */
#define PULL_I2C		PM_PIN_PULLUP_10K

/* ---- Battery ADC ---- */
#define GPIO_VBAT		GPIO_PB0
#define ADC_VBAT_CHN		B0P
#define ADC_VREF_MV		1175	/* typical internal 1.2 V ref, mV */
/* PB0 (not bonded) is driven high permanently, as in pvvx: no toggling
 * around each ADC read, and the pad never floats. Applied by gpio_init()
 * on every wake-up, including from deep-sleep retention. */
#define PB0_INPUT_ENABLE	0
#define PB0_OUTPUT_ENABLE	1
#define PB0_DATA_OUT		1
#define PB0_FUNC		AS_GPIO

/* ---- LCD ---- */
#define GPIO_LCD_PB6		GPIO_PB6
#define PULL_LCD_PB6		PM_PIN_PULLUP_10K
#define GPIO_LCD_CLK		GPIO_PD7	/* SPI clock to the LCD controller */
#define GPIO_LCD_SDI		GPIO_PB7	/* SPI data to the LCD controller */
#define PULL_LCD_SER		PM_PIN_PULLUP_1M

/* Unused "reset" pad: keep it from floating */
#define GPIO_KEY		GPIO_PA5
#define PULL_KEY		PM_PIN_PULLUP_1M

#define MODULE_WATCHDOG_ENABLE	0

/* Stock Xiaomi flash layout: factory MAC address lives here */
#define CFG_ADR_MAC		0x76000

#include "vendor/common/default_config.h"
