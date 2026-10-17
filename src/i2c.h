#pragma once
#include "tl_common.h"

/* Polled I2C master on the TLSR825x hardware block. Addresses are 7-bit.
 * All functions return 0 on success, non-zero on NAK. */
void i2c_bus_init(void);
int  i2c_write(u8 addr, const u8 *buf, u32 len);
int  i2c_read(u8 addr, u8 *buf, u32 len);
