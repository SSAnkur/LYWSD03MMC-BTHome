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
 *
 * Built against the official Telink tc_ble_single_sdk. The makefile
 * force-includes this file into every translation unit (-include), SDK
 * sources included, because the SDK's vendor/common/user_config.h only
 * knows its own demo projects. That is how gpio_init() in the SDK sees the
 * PB0 settings below.
 */
#pragma once

/* System clock. vendor/common/app_common.h derives SYS_CLK_TYPE and
 * CLOCK_SYS_CLOCK_1S/1MS/1US from this; do not define them here. */
#define CLOCK_SYS_CLOCK_HZ	24000000

/* Low-power busy wait: CPU stalled, woken by timer0 */
#define pm_wait_us(t)	cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1US)
#define pm_wait_ms(t)	cpu_stall_wakeup_by_timer0((t) * CLOCK_SYS_CLOCK_1MS)

/* Variables that must survive deep-sleep retention */
#define RAM		_attribute_data_retention_

/* ---- SDK feature switches ---- */
#define BLE_APP_PM_ENABLE		1
#define PM_DEEPSLEEP_RETENTION_ENABLE	1	/* also feeds the boot.link check */
#define BLE_APP_SECURITY_ENABLE		0	/* no pairing: OTA service only */
#define APP_FLASH_PROTECTION_ENABLE	0	/* see flash_clear_write_protection() */
#define UART_PRINT_DEBUG_ENABLE		0
#define MODULE_WATCHDOG_ENABLE		0

/* No Telink evaluation board: a value that matches none of the boards in
 * vendor/common/boards, so default_config.h pulls in no board pin map */
#define BOARD_LYWSD03MMC_B15		0xB150
#define BOARD_SELECT			BOARD_LYWSD03MMC_B15

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

/* Power-on marker. DEEP_ANA_REG1 keeps its value through deep sleep and
 * software reboots and is cleared only by a power cycle. (DEEP_ANA_REG0,
 * used by the pvvx build, is reserved by the official SDK: its bit 1 is
 * the crystal start-up failure flag.) */
#define POWER_ON_FLAG_REG	DEEP_ANA_REG1
#define POWER_ON_FLAG		0x55

/* ---- Connection / OTA (values from pvvx ble.h / app.c) ---- */
#define CONN_INTERVAL		16	/* 16 * 1.25 ms = 20 ms */
#define CONN_LATENCY		49	/* (49 + 1) * 20 ms = 1 s between radio events */
#define CONN_TIMEOUT		400	/* x10 ms = 4 s */
/* Official OTA server timeouts. The packet timeout is an inactivity
 * timeout (1..20 s); the process timeout limits the whole upload
 * (5..1000 s, SDK default only 30 s). */
#define OTA_PACKET_TIMEOUT_S	16	/* abort OTA after 16 s without data */
#define OTA_PROCESS_TIMEOUT_S	1000

/* ---- I2C ---- */
#define I2C_GROUP		I2C_GPIO_GROUP_C2C3
#define I2C_SCL			GPIO_PC2
#define I2C_SDA			GPIO_PC3
#define I2C_SPEED_HZ		700000	/* as pvvx I2C_MAX_SPEED; SHTC3 allows 1 MHz */
#define PULL_I2C		PM_PIN_PULLUP_10K

/* ---- Battery ADC ---- */
#define GPIO_VBAT		GPIO_PB0
#define ADC_VBAT_CHN		B0P
/* 0: use the chip's ADC reference calibration, loaded by the SDK from the
 * calibration sector or eFuse (typical 1175 mV if there is none).
 * Non-zero: fixed reference in mV, to trim against a meter. */
#define ADC_VREF_MV		0
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

#include "vendor/common/default_config.h"
