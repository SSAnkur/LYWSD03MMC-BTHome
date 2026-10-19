/*
 * BTHome v2 temperature / humidity beacon with LCD and OTA - Xiaomi
 * LYWSD03MMC B1.5 (Telink TLSR8251, official Telink tc_ble_single_sdk,
 * no RTOS)
 *
 * Every 60 s: measure T/RH (SHTV3) and VDD, update the segment LCD and the
 * BTHome v2 advertisement sent every 10 s. For OTA_WINDOW_S after the
 * battery is inserted the advertising is connectable (1 s interval) so the
 * Telink OTA service can be reached; after that it is non-connectable.
 *
 * Power handling follows pvvx/ATC_MiThermometer:
 *  - no wake-ups of its own: a measurement is started right after every
 *    6th advertising event, while the chip is awake anyway
 *  - the CPU sleeps (suspend) during the ~12 ms sensor conversion instead
 *    of stalling; the result is collected by an app wake-up timer
 *  - VDD is sampled while the sensor converts, i.e. under load
 *  - deep-sleep retention between events, also while connected
 *    (slave latency 49 -> one connection event per second)
 *  - battery protection: < 2.2 V at power-on or < 2.0 V while running ->
 *    "Lo" on the LCD and 3 min deep sleep, before any flash write
 */
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "app_config.h"
#include "sensor.h"
#include "battery.h"
#include "lcd.h"
#include "ble.h"

#define MEAS_INTERVAL_TICKS	((u32)MEAS_INTERVAL_S * CLOCK_16M_SYS_TIMER_CLK_1S)
#define NAME_LEN		(sizeof(DEV_NAME) - 1)

/* Advertising payload (objects in ascending ID order: 0x0C, 0x2E, 0x45) */
enum {
	OFS_VOLT = 9,	/* voltage, 1 mV, uint16 */
	OFS_HUM  = 12,	/* humidity, 1 %, uint8 */
	OFS_TEMP = 14,	/* temperature, 0.1 degC, sint16 */
};

#if ADV_NAME_IN_ADV
#define ADV_LEN		(18 + NAME_LEN)
#else
#define ADV_LEN		16
#endif

RAM static u8 adv[ADV_LEN] = {
	0x02, 0x01, 0x06,		/* flags: LE general, no BR/EDR */
	0x0C, 0x16,			/* service data, 16-bit UUID */
	0xD2, 0xFC,			/* UUID 0xFCD2 */
	0x40,				/* BTHome v2, unencrypted, regular interval */
	0x0C, 0, 0,			/* voltage */
	0x2E, 0,			/* humidity */
	0x45, 0, 0,			/* temperature */
#if ADV_NAME_IN_ADV
	NAME_LEN + 1, 0x09,		/* complete local name, filled in at boot */
#endif
};
typedef char adv_fits_31_bytes[(sizeof(adv) <= 31) ? 1 : -1];

/* Measurement state, kept through deep-sleep retention */
RAM static u8 adv_count;	/* advertising events since last measurement */
RAM static u8 start_measure;	/* measurement due */
RAM static u8 publish_pending;	/* new values to put on air / on the LCD */
RAM static u8 valid;		/* at least one good sensor reading */
RAM static u32 time_measure;	/* != 0: sensor converting since this tick */
RAM static u32 meas_tick;	/* last measurement start (used when connected) */
RAM static s16 t10;
RAM static u8 rh_pct;
RAM static u16 vbat_mv;

static inline void put_le16(u8 *p, u16 v)
{
	p[0] = v & 0xFF;
	p[1] = v >> 8;
}

/* Deep sleep without retention: the next wake-up is a fresh boot */
static void deep_sleep_s(u32 s)
{
	cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_TIMER,
			 clock_time() + s * CLOCK_16M_SYS_TIMER_CLK_1S);
	while (1) {
	}
}

/* pvvx low_vbat(): stop everything, show it, retry in 3 minutes */
static void low_battery(u16 mv)
{
	sensor_sleep();
	lcd_show_low_battery(mv);
	deep_sleep_s(LOW_VBAT_SLEEP_S);
}

/* The OTA server writes flash: clear block protection left by other
 * firmware, once per power-on, as pvvx does. Same operation as the pvvx
 * SDK's flash_unlock() (status register := 0), done with the official
 * driver, but only when a protection bit is actually set. Works on any
 * SPI flash, unlike the per-chip flash_prot.c, which only knows the
 * flash types listed in the SDK. */
#define FLASH_SR_PROTECT_BITS	0xFC	/* BP0..BP4, SRP0 */

static void flash_clear_write_protection(void)
{
	if (flash_read_status(FLASH_READ_STATUS_CMD_LOWBYTE) &
	    FLASH_SR_PROTECT_BITS) {
		flash_write_status(FLASH_TYPE_8BIT_STATUS, 0);
	}
}

_attribute_ram_code_
static void store_result(s32 mc, s32 mrh)
{
	s32 pct = (mrh + 500) / 1000;

	t10 = (mc + (mc < 0 ? -50 : 50)) / 100;
	rh_pct = pct < 0 ? 0 : (pct > 100 ? 100 : pct);
	valid = 1;
}

/* App wake-up timer: the sensor conversion is finished (pvvx
 * WakeupLowPowerCb). Also called from main_loop() if the stack did not
 * sleep at all. A failed read keeps the last good T/RH values. */
_attribute_ram_code_
static void measure_done_cb(int unused)
{
	s32 mc, mrh;

	(void)unused;
	if (!time_measure) {
		return;
	}
	time_measure = 0;
	bls_pm_setAppWakeupLowPower(0, 0);
	if (!sensor_result(&mc, &mrh)) {
		store_result(mc, mrh);
	}
	publish_pending = 1;
}

/* Start a measurement: trigger the sensor, read VDD while it converts,
 * then let the stack sleep until the result is ready. */
_attribute_ram_code_
static void measure_start(void)
{
	meas_tick = clock_time();
	if (!sensor_start()) {
		time_measure = clock_time() | 1;
	}
	vbat_mv = battery_read_mv();
	if (vbat_mv < END_VBAT_MV) {
		low_battery(vbat_mv);
	}
	if (time_measure) {
		bls_pm_setAppWakeupLowPower(time_measure + SENSOR_MEAS_TICKS, 1);
	} else {
		publish_pending = 1;	/* sensor absent: voltage only */
	}
}

_attribute_ram_code_
static void publish(void)
{
	publish_pending = 0;
	if (valid) {
		put_le16(&adv[OFS_TEMP], (u16)t10);
		adv[OFS_HUM] = rh_pct;
	}
	put_le16(&adv[OFS_VOLT], vbat_mv);
	bls_ll_setAdvData(adv, sizeof(adv));
	lcd_show(valid, t10, rh_pct, vbat_mv < LOW_BATT_MV);
}

/* Called by the stack right before every advertising event (pvvx
 * app_advertise_prepare_handler): count events, flag a measurement on
 * every MEAS_EVERY_N_ADV-th one. The measurement itself starts in
 * main_loop() once the event is done. While the OTA window runs at the
 * fast interval, main_loop() uses a timer instead. */
_attribute_ram_code_
static int adv_prepare_cb(void *p)
{
	(void)p;
	if (!ota_window && ++adv_count >= MEAS_EVERY_N_ADV) {
		adv_count = 0;
		start_measure = 1;
	}
	return 1;	/* send this advertising packet */
}

static void user_init_normal(void)
{
	/* Must come before any BLE stack initialisation (official order) */
	random_generator_init();

	/* Flash size -> sector map (MAC 0x76000, calibration 0x77000 on the
	 * 512 KiB part), then the factory calibration from that sector:
	 * crystal trim, flash supply and ADC reference. Read-only, so it is
	 * safe before the battery check, which needs the ADC calibration. */
	blc_readFlashSize_autoConfigCustomFlashSector();
	blc_app_loadCustomizedParameters_normal();

	blc_ll_initBasicMCU();
	adc_power_on_sar_adc(0);
	lpc_power_down();

	/* Analog pull resistors are kept through deep sleep: set once */
	gpio_setup_up_down_resistor(I2C_SCL, PULL_I2C);
	gpio_setup_up_down_resistor(I2C_SDA, PULL_I2C);
	gpio_setup_up_down_resistor(GPIO_LCD_PB6, PULL_LCD_PB6);
	gpio_setup_up_down_resistor(GPIO_LCD_CLK, PULL_LCD_SER);
	gpio_setup_up_down_resistor(GPIO_LCD_SDI, PULL_LCD_SER);
	gpio_setup_up_down_resistor(GPIO_KEY, PULL_KEY);

	/* Battery first: nothing below may write flash on a dying cell
	 * (pvvx start_tst_battery) */
	vbat_mv = battery_read_mv();
	if (vbat_mv < BOOT_VBAT_MV) {
		sensor_init();		/* idle -> sleep */
		lcd_init();
		low_battery(vbat_mv);
	}

	flash_clear_write_protection();

#if ADV_NAME_IN_ADV
	memcpy(&adv[18], DEV_NAME, NAME_LEN);
#endif
	lcd_init();		/* shows "-- --" until the first good reading */
	sensor_init();		/* put the SHTV3 to sleep */

	/* First measurement, blocking, so the first packet carries data */
	if (!sensor_start()) {
		s32 mc, mrh;

		vbat_mv = battery_read_mv();
		pm_wait_us(SENSOR_MEAS_US);
		if (!sensor_result(&mc, &mrh)) {
			store_result(mc, mrh);
		}
	}
	publish();

	/* Cleared only by a power cycle, see app_config.h */
	int power_on = analog_read(POWER_ON_FLAG_REG) != POWER_ON_FLAG;

	analog_write(POWER_ON_FLAG_REG, POWER_ON_FLAG);

	bls_pm_registerAppWakeupLowPowerCb(measure_done_cb);
	ble_init(adv, sizeof(adv), adv_prepare_cb, power_on);
	meas_tick = clock_time();
}

_attribute_ram_code_
static void user_init_deepRetn(void)
{
	blc_app_loadCustomizedParameters_deepRetn();	/* crystal trim, VDD_F */
	blc_ll_initBasicMCU();
	ble_init_deep_retention();
}

_attribute_ram_code_
static void main_loop(void)
{
	blt_sdk_main_loop();

	if (ble_task()) {
		return;		/* OTA in progress: no measurements */
	}

	if (time_measure &&
	    clock_time() - time_measure > SENSOR_MEAS_TICKS) {
		measure_done_cb(0);	/* stack did not sleep in between */
	}

	/* While connected there are no advertising events to count, and in
	 * the OTA window they come every second */
	if ((ble_connected || ota_window) &&
	    clock_time() - meas_tick >= MEAS_INTERVAL_TICKS) {
		start_measure = 1;
	}

	if (start_measure && !time_measure) {
		start_measure = 0;
		measure_start();
	}

	if (publish_pending) {
		publish();
	}
}

_attribute_ram_code_
void irq_handler(void)
{
	irq_blt_sdk_handler();
}

_attribute_ram_code_
int main(void)
{
	blc_pm_select_internal_32k_crystal();	/* no 32 kHz crystal fitted */
	cpu_wakeup_init();

	int deep_ret = pm_is_MCU_deepRetentionWakeup();

	/* Same order as the SDK's ble_sample */
	rf_drv_ble_init();
	gpio_init(!deep_ret);
	clock_init(SYS_CLK_TYPE);
	reg_clk_en0 = FLD_CLK0_SWIRE_EN;	/* gate unused peripheral clocks */

	if (deep_ret) {
		user_init_deepRetn();
	} else {
		user_init_normal();
	}

	irq_enable();
	for (;;) {
		main_loop();
	}
}
