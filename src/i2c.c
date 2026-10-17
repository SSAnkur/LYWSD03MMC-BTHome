#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "i2c.h"

#define I2C_WAIT()	while (reg_i2c_status & FLD_I2C_CMD_BUSY)

/* The I2C clock and pin mux live in digital registers that are lost in
 * deep-sleep retention, so this is called at the start of every bus session.
 * Clock stretching is disabled: the sensor is polled, never stretched. */
_attribute_ram_code_
void i2c_bus_init(void)
{
	i2c_gpio_set(I2C_GROUP);
	reg_i2c_speed = CLOCK_SYS_CLOCK_HZ / (4 * I2C_SPEED_HZ);
	reg_i2c_mode |= FLD_I2C_MASTER_EN;
	reg_i2c_mode &= ~FLD_I2C_HOLD_MASTER;
	reg_clk_en0 |= FLD_CLK0_I2C_EN;
	reg_spi_sp &= ~FLD_SPI_ENABLE;	/* pads shared with SPI: force I2C */
}

_attribute_ram_code_
int i2c_write(u8 addr, const u8 *buf, u32 len)
{
	int err;

	reg_i2c_id = addr << 1;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
	I2C_WAIT();
	err = reg_i2c_status & FLD_I2C_NAK;
	while (!err && len--) {
		reg_i2c_do = *buf++;
		reg_i2c_ctrl = FLD_I2C_CMD_DO;
		I2C_WAIT();
		err = reg_i2c_status & FLD_I2C_NAK;
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
	I2C_WAIT();
	return err;
}

_attribute_ram_code_
int i2c_read(u8 addr, u8 *buf, u32 len)
{
	int err;

	reg_i2c_id = (addr << 1) | FLD_I2C_WRITE_READ_BIT;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
	I2C_WAIT();
	err = reg_i2c_status & FLD_I2C_NAK;
	while (!err && len--) {
		/* NAK (ACK flag) the last byte to end the read */
		reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID |
			       (len ? 0 : FLD_I2C_CMD_ACK);
		I2C_WAIT();
		*buf++ = reg_i2c_di;
	}
	reg_i2c_ctrl = FLD_I2C_CMD_STOP;
	I2C_WAIT();
	return err;
}
