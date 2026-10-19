/*
 * VDD measurement. PB0 is not bonded out on the TLSR8251 package; it is
 * kept driven high (= VDD) permanently by gpio_init() (see app_config.h,
 * same as pvvx) and sampled through the 1/8 prescaler against the
 * 1.2 V reference. 8 samples are taken, sorted, and the middle 4 averaged.
 * ADC sequence follows pvvx/ATC_MiThermometer battery.c; the conversion
 * uses the per-chip reference calibration of the official SDK (as its
 * adc_sample_and_get_result() does) unless ADC_VREF_MV fixes it.
 */
#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "battery.h"

#define ADC_SAMPLES	8

/* Set by user_calib_adc_vref() from blc_app_loadCustomizedParameters_normal()
 * (drivers/B85/adc.c, retention RAM, default 1175 mV / 0 mV) */
extern unsigned short adc_gpio_calib_vref;
extern signed char adc_gpio_calib_vref_offset;

/* ADC analog config is not kept across deep-sleep retention: set it up each time */
static void vbat_adc_setup(void)
{
	adc_power_on_sar_adc(0);
	adc_set_sample_clk(5);
	adc_set_left_right_gain_bias(GAIN_STAGE_BIAS_PER100, GAIN_STAGE_BIAS_PER100);
	adc_set_chn_enable_and_max_state_cnt(ADC_MISC_CHN, 2);
	adc_set_state_length(240, 0, 10);
	analog_write(anareg_adc_res_m, RES14 | FLD_ADC_EN_DIFF_CHN_M);
	adc_set_ain_chn_misc(ADC_VBAT_CHN, GND);
	adc_set_ref_voltage(ADC_MISC_CHN, ADC_VREF_1P2V);
	adc_set_tsample_cycle_chn_misc(SAMPLING_CYCLES_6);
	adc_set_ain_pre_scaler(ADC_PRESCALER_1F8);
}

_attribute_ram_code_
u16 battery_read_mv(void)
{
	volatile u32 dat[ADC_SAMPLES];
	u16 s[ADC_SAMPLES];
	u8 rp = 0;

	vbat_adc_setup();
	adc_power_on_sar_adc(1);
	adc_reset_adc_module();
	for (int i = 0; i < ADC_SAMPLES; i++) {
		dat[i] = 0;
	}
	adc_config_misc_channel_buf((u16 *)dat, sizeof(dat));
	dfifo_enable_dfifo2();

	for (int i = 0; i < ADC_SAMPLES; i++) {
		while (rp == reg_dfifo2_wptr) {
		}
		rp = reg_dfifo2_wptr;
		/* 14-bit result, bit 13 = sign in differential mode */
		u16 v = (dat[i] & BIT(13)) ? 0 : (dat[i] & 0x1FFF);
		int j = i - 1;

		while (j >= 0 && s[j] > v) {	/* insertion sort */
			s[j + 1] = s[j];
			j--;
		}
		s[j + 1] = v;
	}

	dfifo_disable_dfifo2();
	adc_power_on_sar_adc(0);

	/* mV = avg/8192 * Vref * 8 + offset = sum4 * Vref / 4096 + offset */
	u32 sum = s[2] + s[3] + s[4] + s[5];

	if (!sum) {
		return 0;
	}
#if ADC_VREF_MV
	return (u16)((sum * ADC_VREF_MV + 2048) >> 12);
#else
	return (u16)((s32)((sum * adc_gpio_calib_vref + 2048) >> 12) +
		     adc_gpio_calib_vref_offset);
#endif
}
