#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "i2c.h"
#include "sensor.h"

/* SHTV3 / SHTC3 on the B1.5 board */
#define SHTC3_ADDR		0x70
#define SHTC3_WAKEUP_US		240

static const u8 shtc3_wakeup[] = { 0x35, 0x17 };
static const u8 shtc3_sleep[]  = { 0xB0, 0x98 };
static const u8 shtc3_meas[]   = { 0x78, 0x66 }; /* normal mode, T first, no stretching */

_attribute_ram_code_
static u8 crc8(const u8 *p, int len)
{
	u8 crc = 0xFF;

	while (len--) {
		crc ^= *p++;
		for (int i = 0; i < 8; i++) {
			crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
		}
	}
	return crc;
}

/* Wake the sensor once and put it straight back to sleep (~0.3 uA).
 * It powers up in idle mode, which draws far more than sleep. */
int sensor_init(void)
{
	int err;

	i2c_bus_init();
	err = i2c_write(SHTC3_ADDR, shtc3_wakeup, sizeof(shtc3_wakeup));
	if (!err) {
		sleep_us(SHTC3_WAKEUP_US);
		i2c_write(SHTC3_ADDR, shtc3_sleep, sizeof(shtc3_sleep));
	}
	return err;
}

_attribute_ram_code_
int sensor_start(void)
{
	int err;

	i2c_bus_init();
	err = i2c_write(SHTC3_ADDR, shtc3_wakeup, sizeof(shtc3_wakeup));
	if (err) {
		return err;
	}
	sleep_us(SHTC3_WAKEUP_US);
	err = i2c_write(SHTC3_ADDR, shtc3_meas, sizeof(shtc3_meas));
	if (err) {
		i2c_write(SHTC3_ADDR, shtc3_sleep, sizeof(shtc3_sleep));
	}
	return err;
}

/* Raw 16-bit words -> milli-units. The x/65536 scaling is done as
 * (x * k) >> 13, which fits in 32 bits:
 *   175000 / 8 = 21875,  100000 / 8 = 12500 */
_attribute_ram_code_
int sensor_result(s32 *t_mc, s32 *rh_mpct)
{
	u8 buf[6];
	int err;

	i2c_bus_init();	/* I2C registers do not survive deep-sleep retention */
	err = i2c_read(SHTC3_ADDR, buf, sizeof(buf));
	i2c_write(SHTC3_ADDR, shtc3_sleep, sizeof(shtc3_sleep));

	if (err || crc8(&buf[0], 2) != buf[2] || crc8(&buf[3], 2) != buf[5]) {
		return -1;
	}

	u32 t_raw = (buf[0] << 8) | buf[1];
	u32 h_raw = (buf[3] << 8) | buf[4];

	*t_mc = (s32)((t_raw * 21875) >> 13) - 45000;
	*rh_mpct = (s32)((h_raw * 12500) >> 13);
	return 0;
}

/* The sleep command is NAKed while a conversion is running, so allow
 * for one to finish first. */
void sensor_sleep(void)
{
	i2c_bus_init();
	sleep_us(SENSOR_MEAS_US);
	i2c_write(SHTC3_ADDR, shtc3_sleep, sizeof(shtc3_sleep));
}
