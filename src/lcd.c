/*
 * LYWSD03MMC segment LCD driver for the 2025 "B1.5" board (pvvx reports it
 * as HW B1.1): LCD controller on bit-banged SPI, PD7 = CLK, PB7 = SDI.
 * Write-only, no acknowledge.
 *
 * The frame format and segment map are from pvvx/ATC_MiThermometer
 * (lcd_lywsd03mmc.c), used under that project's MIT-style license:
 *   Permission is hereby granted, free of charge, to any person obtaining
 *   a copy of this software ... to deal in the Software without restriction.
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 *
 * Display buffer, byte.bit:
 *
 *        --5.4--     --4.4--       --3.4--        BLE 2.4
 *   |   5.5   5.0  4.5   4.0     3.5   3.0        o 2.5  (temp unit
 *  5.3   --5.1--     --4.1--       --3.1--          2.5..2.7  symbols)
 *   |   5.6   5.2  4.6   4.2     3.6   3.2
 *        --5.7--     --4.7--   *   --3.7--
 *                             4.3
 *    smiley 2.0..2.2                 --1.4--     --0.4--
 *                                  1.5   1.0   0.5   0.0
 *                                    --1.1--     --0.1--
 *                                  1.6   1.2   0.6   0.2   % 0.3
 *                    BAT 1.3         --1.7--     --0.7--
 */
#include "tl_common.h"
#include "drivers.h"
#include "drivers/8258/gpio_8258.h"
#include "app_config.h"
#include "lcd.h"

#define SEG_MINUS	0x02	/* middle segment */
#define SYM_POINT	0x08	/* 4.3 */
#define SYM_ONE		0x08	/* 5.3: leading "1" for 100..199 */
#define SYM_PERCENT	0x08	/* 0.3 */
#define SYM_BAT		0x08	/* 1.3 */
#define SYM_DEG_C	0xA0	/* 2.5 + 2.7 */
#define SYM_BLE		0x10	/* 2.4: shown while a BLE central is connected */
#define SYM_H		0x67
#define SYM_i		0x40
#define SYM_L		0xE0
#define SYM_o		0xC6

#define SPI_CLK_DELAY_US	32	/* half clock period: ~15 kHz */

static const u8 digits[10] = {
	0xF5, 0x05, 0xD3, 0x97, 0x27, 0xB6, 0xF6, 0x15, 0xF7, 0xB7
};

/* Frame: AA d5..d0 xor(d) 55, sent byte by byte, LSB first */
RAM static u8 buf[6];
RAM static u8 sent[6];
RAM static u8 frame[9];

static void frame_fill(void)
{
	u8 chk = 0;

	frame[0] = 0xAA;
	for (int i = 0; i < 6; i++) {
		frame[1 + i] = buf[5 - i];
		chk ^= buf[i];
	}
	frame[7] = chk;
	frame[8] = 0x55;
}

/* Data changes while CLK is low and is latched on the rising edge */
_attribute_ram_code_
static void spi_send_byte(u8 b)
{
	for (int i = 0; i < 8; i++) {
		BM_CLR(reg_gpio_out(GPIO_LCD_CLK), GPIO_LCD_CLK & 0xFF);
		if (b & 0x01) {
			BM_SET(reg_gpio_out(GPIO_LCD_SDI), GPIO_LCD_SDI & 0xFF);
		} else {
			BM_CLR(reg_gpio_out(GPIO_LCD_SDI), GPIO_LCD_SDI & 0xFF);
		}
		sleep_us(SPI_CLK_DELAY_US);
		BM_SET(reg_gpio_out(GPIO_LCD_CLK), GPIO_LCD_CLK & 0xFF);
		sleep_us(SPI_CLK_DELAY_US);
		b >>= 1;
	}
	sleep_us(SPI_CLK_DELAY_US);
}

/* ~5 ms with interrupts off, only when the content changed. gpio_init()
 * makes both pins inputs again after every deep-sleep retention wake-up,
 * so they are switched to outputs on each send. */
_attribute_ram_code_
static void lcd_send(void)
{
	BM_CLR(reg_gpio_oen(GPIO_LCD_CLK), GPIO_LCD_CLK & 0xFF);
	BM_CLR(reg_gpio_oen(GPIO_LCD_SDI), GPIO_LCD_SDI & 0xFF);

	u8 r = irq_disable();

	for (int i = 0; i < (int)sizeof(frame); i++) {
		spi_send_byte(frame[i]);
	}
	irq_restore(r);
}

static void lcd_flush(void)
{
	if (!memcmp(sent, buf, sizeof(buf))) {
		return;		/* nothing changed: no LCD traffic at all */
	}
	frame_fill();
	lcd_send();
	memcpy(sent, buf, sizeof(buf));
}

void lcd_init(void)
{
	memset(buf, SEG_MINUS, sizeof(buf));		/* "-- --" */
	buf[2] = 0;

	pm_wait_ms(30);		/* controller start-up, as pvvx (>= 30 ms) */
	frame_fill();
	lcd_send();
	memcpy(sent, buf, sizeof(buf));
}

/* Big digits: temperature in 0.1 degC.
 * -9.9 .. 199.9 shown with one decimal, -99 .. -10 as integers. */
static void show_big_x10(s16 n)
{
	buf[5] = buf[4] = 0;
	if (n > 1999) {
		buf[3] = 0; buf[4] = SYM_i; buf[5] = SYM_H;	/* "Hi" */
		return;
	}
	if (n <= -995) {
		buf[3] = 0; buf[4] = SYM_o; buf[5] = SYM_L;	/* "Lo" */
		return;
	}
	if (n < 0) {
		n = -n;
		buf[5] = SEG_MINUS;
	}
	if (buf[5] && n > 99) {
		n = (n + 5) / 10;		/* -10 .. -99: integer */
	} else {
		buf[4] = SYM_POINT;
	}
	if (n > 999) {
		buf[5] |= SYM_ONE;
	}
	if (n > 99) {
		buf[5] |= digits[n / 100 % 10];
	}
	buf[4] |= digits[n / 10 % 10];	/* always: "0.x" */
	buf[3] = digits[n % 10];
}

/* Small digits: humidity in %. */
static void show_small_pct(int n)
{
	buf[1] &= SYM_BAT;
	buf[0] = SYM_PERCENT;
	if (n > 99) {
		buf[0] |= SYM_i; buf[1] |= SYM_H;
		return;
	}
	if (n < 0) {
		n = 0;
	}
	if (n > 9) {
		buf[1] |= digits[n / 10];
	}
	buf[0] |= digits[n % 10];
}

void lcd_show(int valid, s16 t10, int rh, int batt_low)
{
	if (valid) {
		show_big_x10(t10);
		show_small_pct(rh);
		buf[2] = (buf[2] & SYM_BLE) | SYM_DEG_C;
	}
	if (batt_low) {
		buf[1] |= SYM_BAT;
	} else {
		buf[1] &= ~SYM_BAT;
	}
	lcd_flush();
}

void lcd_show_ble(int on)
{
	if (on) {
		buf[2] |= SYM_BLE;
	} else {
		buf[2] &= ~SYM_BLE;
	}
	lcd_flush();
}

/* Dead-battery screen (as pvvx low_vbat()): battery voltage in the big
 * digits ("1.9"), "Lo" in the small ones, battery icon on. */
void lcd_show_low_battery(u16 mv)
{
	show_big_x10(mv / 100);
	buf[1] = SYM_L | SYM_BAT;
	buf[0] = SYM_o;
	buf[2] = 0;
	lcd_flush();
}
