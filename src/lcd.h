#pragma once
#include "tl_common.h"

/* Segment LCD of the LYWSD03MMC 2025 "B1.5" board (pvvx: HW B1.1):
 * controller on bit-banged SPI, PD7 CLK / PB7 SDI, write-only. It holds
 * the image by itself, so the CPU only talks to it when the content
 * changes. */
void lcd_init(void);			/* shows "-- --" */
void lcd_show(int valid, s16 t10, int rh, int batt_low);
void lcd_show_ble(int on);		/* BLE symbol while connected */
void lcd_show_low_battery(u16 mv);	/* "x.x Lo" + battery icon */
