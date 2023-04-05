#ifndef _IMX319_H
#define _IMX319_H

/* Chip ID */
#define IMX319_REG_CHIP_ID		0x0016
#define IMX319_CHIP_ID			0x0319

/* V_TIMING internal */
#define IMX319_REG_FLL			0x0340
#define IMX319_FLL_MAX			0xffff

/* Exposure control */
#define IMX319_REG_EXPOSURE		0x0202
#define IMX319_EXPOSURE_MIN		1
#define IMX319_EXPOSURE_STEP		1
#define IMX319_EXPOSURE_DEFAULT		0x04f6

/*
 *  the digital control register for all color control looks like:
 *  +-----------------+------------------+
 *  |      [7:0]      |       [15:8]     |
 *  +-----------------+------------------+
 *  |	  0x020f      |       0x020e     |
 *  --------------------------------------
 *  it is used to calculate the digital gain times value(integral + fractional)
 *  the [15:8] bits is the fractional part and [7:0] bits is the integral
 *  calculation equation is:
 *      gain value (unit: times) = REG[15:8] + REG[7:0]/0x100
 *  Only value in 0x0100 ~ 0x0FFF range is allowed.
 *  Analog gain use 10 bits in the registers and allowed range is 0 ~ 960
 */
 /* Analog gain control */
#define IMX319_REG_ANALOG_GAIN		0x0204
#define IMX319_ANA_GAIN_MIN		0
#define IMX319_ANA_GAIN_MAX		960
#define IMX319_ANA_GAIN_STEP		1
#define IMX319_ANA_GAIN_DEFAULT		0

/* Digital gain control */
#define IMX319_REG_DPGA_USE_GLOBAL_GAIN	0x3ff9
#define IMX319_REG_DIG_GAIN_GLOBAL	0x020e
#define IMX319_DGTL_GAIN_MIN		256
#define IMX319_DGTL_GAIN_MAX		4095
#define IMX319_DGTL_GAIN_STEP		1
#define IMX319_DGTL_GAIN_DEFAULT	256

/* Test Pattern Control */
#define IMX319_REG_TEST_PATTERN		0x0600
#define IMX319_TEST_PATTERN_DISABLED		0
#define IMX319_TEST_PATTERN_SOLID_COLOR		1
#define IMX319_TEST_PATTERN_COLOR_BARS		2
#define IMX319_TEST_PATTERN_GRAY_COLOR_BARS	3
#define IMX319_TEST_PATTERN_PN9			4

/* Flip Control */
#define IMX319_REG_ORIENTATION		0x0101

/* default link frequency and external clock */
#define IMX319_LINK_FREQ_DEFAULT	482400000
#define IMX319_EXT_CLK			19200000
#define IMX319_LINK_FREQ_INDEX		0

#endif