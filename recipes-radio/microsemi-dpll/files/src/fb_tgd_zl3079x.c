/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fb_tgd_zl3079x.h"

#include <linux/miscdevice.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/of.h>
#include <linux/i2c.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/timekeeping.h>
#include <linux/delay.h>
#include <linux/uaccess.h>

/* DPLL index (range: 0-2) */
#define DPLL_INDEX_INVALID (-1)
#define DPLL_INDEX_0 0
#define DPLL_INDEX_1 1
#define DPLL_INDEX_2 2

//
// Registers
//

/* Page selection (0-12) for SPI/I2C access */
#define ZL_REG_PAGE_SEL 0x7f

/* Chip identification number */
#define ZL_REG_ID 0x1

/*
 * DPLL i status registers are at
 * ZL_REG_DPLL_MON_STATUS_0 + i*ZL_DPLL_MON_STATUS_REGISTERS_STRIDE
 */
#define ZL_REG_DPLL_MON_STATUS_0 0x118
#define ZL_DPLL_MON_STATUS_REGISTERS_STRIDE 0x1

/*
 * DPLL i refsel state registers are at
 * ZL_REG_DPLL_STATE_REFSEL_0 + i*ZL_DPLL_STATE_REFSEL_REGISTERS_STRIDE
 */
#define ZL_REG_DPLL_STATE_REFSEL_0 0x120
#define ZL_DPLL_STATE_REFSEL_REGISTERS_STRIDE 0x1

/*
 * DPLL i config registers are at
 * ZL_REG_DPLL_MODE_REFSEL_0 + i*ZL_CONFIG_REGISTERS_STRIDE
 */
#define ZL_REG_DPLL_MODE_REFSEL_0 0x210
#define ZL_CONFIG_REGISTERS_STRIDE 0x4

/*
 * DPLL i registers are at
 * ZL_REG_DPLL_DF_OFFSET_0 + i*ZL_DPLL_REGISTERS_STRIDE
 */
#define ZL_REG_DPLL_DF_OFFSET_0 0x300
#define ZL_DPLL_REGISTERS_STRIDE 0x20

/* Synthesizer config */
#define ZL_REG_HP_CTRL_1 0x0480
#define ZL_HP_CTRL_DPLL_MASK 0x30
#define ZL_HP_CTRL_DPLL0 0x00
#define ZL_HP_CTRL_DPLL1 0x10

/* DPLL mailbox access */
#define ZL_REG_DPLL_MB_MASK 0x0602
#define ZL_REG_DPLL_MB_SEM 0x0604
#define ZL_REG_DPLL_BW_FIXED 0x0605
#define ZL_REG_DPLL_BW_VAR 0x0606
#define ZL_REG_DPLL_CONFIG 0x0607
#define ZL_REG_DPLL_PSL 0x0608

#define ZL_DPLL_MB_SEM_WRITE 0x01
#define ZL_DPLL_MB_SEM_READ 0x02
#define ZL_DPLL_BW_VARIABLE 0x7

// DPLL bandwidth below computed as:
// dpll_bw_var = round(32 * log(bandwidth[Hz] * 10^4))

// 30 mHz bandwidth, 885 ns/s phase slope limiting
#define ZL_DPLL_BW_GPS 0x4f
#define ZL_DPLL_PSL_GPS 0x0375

// 5 Hz bandwidth, G.8262 Option 1 phase slope limiting of 7.5 us/s
#define ZL_DPLL_BW_SYNCE 0x96
#define ZL_DPLL_PSL_SYNCE 0x1d4c

/* Width of GPOUT{X} high pulse, in units of Synth0 VCO cycles */
#define ZL_REG_DPLL_GP_OUT_WIDTH_0 0x0426
#define ZL_REG_DPLL_GP_OUT_WIDTH_1 0x0436

//
// Register-related constants
//

/* Chip identification numbers from ZL_REG_ID */
#define ZL_CHIP_ID_30791 0x0ECF
#define ZL_CHIP_ID_30793 0x0ED1
#define ZL_CHIP_ID_30795 0x0ED3

/* DPLL modes from ZL_REG_DPLL_MODE_REFSEL_X */
#define ZL_DPLL_MODE_FREERUN 0x0
#define ZL_DPLL_MODE_HOLDOVER 0x1
#define ZL_DPLL_MODE_AUTOMATIC 0x3
#define ZL_DPLL_MODE_NCO 0x4
// Forced reference lock to REF0P (GPS PPS input)
#define ZL_DPLL_MODE_REFLOCK_GPS 0x02
// Forced reference lock to REF3P (SyncE input from 10G PHY)
#define ZL_DPLL_MODE_REFLOCK_SYNCE 0x62

//
// Constants for second order PLL implemented as PI controller
// Input timestamps are in ns and output is fractional frequency offset to apply
// in steps of +/- 2^-48
//

// round(2^40/10^6)
#define PLL_DELTA_T_SCALE 1099512UL
#define PLL_DELTA_T_SHIFT 40UL
// Damping factor ~ 10, 3 dB frequency ~ 1.9 Hz
#define PLL_KP 1495060
#define PLL_KI 19853
// round(7.5e-6*2^48)
#define PLL_MAX_FFO 2111062325LL
// round(-7.5e-6*2^48)
#define PLL_MIN_FFO (-2111062325LL)
// Number of received HTSF to consider "locked" (~10 seconds)
#define PLL_LOCK_COUNT 390

// PLL reset timer
#define PLL_RESET_TIME_US 1000000

// 20/80 duty cycle on 1 PPS
// GPOUT width in units of 750MHz = dec2hex(750e6*.2, 8) = 0x08f0d180
#define ZL_GP_OUT_WIDTH_20_80 0x08f0d180

//
// Module params
//

/* Proportional gain (K_p) */
int pll_kp = PLL_KP;
module_param(pll_kp, int, 0644);

/* Integral gain (K_i) */
int pll_ki = PLL_KI;
module_param(pll_ki, int, 0644);

/* Baseband device index (0-3) to process HTSF from, or -1 to drop */
int tgd_device = -1;
module_param(tgd_device, int, 0644);

/* PPS source is GPS */
int pps_source_gps = 1;
module_param(pps_source_gps, int, 0644);

// From fb_tgd_fw_if.c
typedef void (*tgd_htsf_info_handler_t)(int devidx, uint64_t macaddr,
					int32_t txRxDiffNs, int32_t delayEstNs,
					uint32_t rxStartUs);
int tgd_register_htsf_info_handler(tgd_htsf_info_handler_t handler);
int tgd_unregister_htsf_info_handler(tgd_htsf_info_handler_t handler);

//
// Private data
//

struct tgd_zl_priv_data {
	struct i2c_client *client;
	struct tgd_zl_stats stats;
	uint32_t last_rxStartUs;
	u64 last_htsf_ns; // ktime associated with last_rxStartUs
	int32_t txRxDiffInit;
	int16_t initDone;
	int tgd_device;
	uint64_t tgd_device_mac;
	// Fractional frequency offset estimate in signed Q48 format
	s64 ffo_est_q48;
	uint32_t htsf_rx_count;
	int gps_dpll_index;
	int synce_dpll_index;
	u8 synce_dpll_mode;
};

static struct tgd_zl_priv_data *g_priv;

// hack for ktime_get_coarse_ns() introduced in linux kernel 5.3
static u64 tgd_zl_get_ktime_ns(void)
{
	// deprecated for ktime_get_coarse_ts64()
	struct timespec64 ts = get_monotonic_coarse64();
	return ktime_to_ns(timespec64_to_ktime(ts));
}

//
// DPLL operations
//

static s32 tgd_zl_page_sel(struct i2c_client *client, u16 reg, u8 *reg_in_page)
{
	u8 page = reg >> 7;
	*reg_in_page = reg & 0x7f;
	return i2c_smbus_write_byte_data(client, ZL_REG_PAGE_SEL, page);
}

static s32 tgd_zl_read_reg8(struct i2c_client *client, u16 reg)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	return i2c_smbus_read_byte_data(client, reg_in_page);
}

static s32 tgd_zl_read_reg16(struct i2c_client *client, u16 reg)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	if ((ret = i2c_smbus_read_word_data(client, reg_in_page)) < 0) {
		return ret;
	}
	return be16_to_cpu(ret);
}

static s32 tgd_zl_write_reg8(struct i2c_client *client, u16 reg, u8 value)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	return i2c_smbus_write_byte_data(client, reg_in_page, value);
}

static s32 tgd_zl_write_reg16(struct i2c_client *client, u16 reg, u32 value)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	value = cpu_to_be16(value);
	return i2c_smbus_write_i2c_block_data(client, reg_in_page, 2,
					      (const u8 *)&value);
}

static s32 tgd_zl_write_reg32(struct i2c_client *client, u16 reg, u32 value)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	value = cpu_to_be32(value);
	return i2c_smbus_write_i2c_block_data(client, reg_in_page, 4,
					      (const u8 *)&value);
}

/* Data to write is the 48 LSBs of value */
static s32 tgd_zl_write_reg48(struct i2c_client *client, u16 reg, u64 value)
{
	u8 reg_in_page;
	s32 ret;

	if ((ret = tgd_zl_page_sel(client, reg, &reg_in_page)) < 0) {
		return ret;
	}
	value = cpu_to_be64(value);
	return i2c_smbus_write_i2c_block_data(client, reg_in_page, 6,
					      (const u8 *)&value + 2);
}

static s32 tgd_zl_set_mode(struct i2c_client *client, int dpll, u8 mode)
{
	return tgd_zl_write_reg8(client,
				 ZL_REG_DPLL_MODE_REFSEL_0 +
				     dpll * ZL_CONFIG_REGISTERS_STRIDE,
				 mode);
}

static s32 tgd_zl_get_dpll_state_refsel(struct i2c_client *client, int dpll)
{
	return tgd_zl_read_reg8(
	    client, ZL_REG_DPLL_STATE_REFSEL_0 +
			dpll * ZL_DPLL_STATE_REFSEL_REGISTERS_STRIDE);
}

static s32 tgd_zl_get_dpll_status(struct i2c_client *client, int dpll)
{
	return tgd_zl_read_reg8(client,
				ZL_REG_DPLL_MON_STATUS_0 +
				    dpll * ZL_DPLL_MON_STATUS_REGISTERS_STRIDE);
}

static s32 tgd_zl_set_synth1_dpll(struct i2c_client *client, int dpll)
{
	s32 ret;
	u32 value;

	// Do read-modify-write for hp_ctrl_1 register to set dpll field
	if ((ret = tgd_zl_read_reg8(client, ZL_REG_HP_CTRL_1)) < 0) {
		return ret;
	}
	value = ret & (~ZL_HP_CTRL_DPLL_MASK);
	value |= (dpll == DPLL_INDEX_0) ? ZL_HP_CTRL_DPLL0 : ZL_HP_CTRL_DPLL1;
	return tgd_zl_write_reg8(client, ZL_REG_HP_CTRL_1, value);
}

static s32 tgd_zl_dpll_mailbox_wait(struct i2c_client *client)
{
	s32 ret;

	// Use fixed delay instead of polling until ZL_REG_DPLL_MB_SEM cleared
	msleep(30);
	ret = tgd_zl_read_reg8(client, ZL_REG_DPLL_MB_SEM);
	if (ret > 0) {
		// Log value, then return negative so caller treats as error
		dev_err(&client->dev, "Mailbox read timeout! ret:%d\n", ret);
		return -1;
	}
	return ret;
}

static s32 tgd_zl_set_dpll_config(struct i2c_client *client, int dpll,
				  u8 bw_var, u16 psl)
{
	s32 ret;

	// Set DPLL mailbox mask with DPLL index to modify
	ret = tgd_zl_write_reg16(client, ZL_REG_DPLL_MB_MASK, (1 << dpll));
	if (ret < 0) {
		return ret;
	}
	// Request mailbox read
	ret =
	    tgd_zl_write_reg8(client, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_READ);
	if (ret < 0) {
		return ret;
	}
	if ((ret = tgd_zl_dpll_mailbox_wait(client)) < 0) {
		return ret;
	}

	// Set DPLL bandwidth
	ret = tgd_zl_write_reg8(client, ZL_REG_DPLL_BW_FIXED,
				ZL_DPLL_BW_VARIABLE);
	if (ret < 0) {
		return ret;
	}
	ret = tgd_zl_write_reg8(client, ZL_REG_DPLL_BW_VAR, bw_var);
	if (ret < 0) {
		return ret;
	}

	// Set phase slope limiting
	ret = tgd_zl_write_reg16(client, ZL_REG_DPLL_PSL, psl);
	if (ret < 0) {
		return ret;
	}

	// Request mailbox write
	ret =
	    tgd_zl_write_reg8(client, ZL_REG_DPLL_MB_SEM, ZL_DPLL_MB_SEM_WRITE);
	if (ret < 0) {
		return ret;
	}
	if ((ret = tgd_zl_dpll_mailbox_wait(client)) < 0) {
		return ret;
	}

	return ret;
}

/* The 48 LSBs are the signed offset value to send */
static s32 tgd_zl_send_offset(struct i2c_client *client, int dpll, s64 offset)
{
	struct tgd_zl_priv_data *priv = i2c_get_clientdata(client);
	struct tgd_zl_stats *stats = &priv->stats;
	s32 ret;

	if ((ret = tgd_zl_write_reg48(client,
				      ZL_REG_DPLL_DF_OFFSET_0 +
					  dpll * ZL_DPLL_REGISTERS_STRIDE,
				      offset) < 0)) {
		stats->nco_update_errors++;
	} else {
		stats->nco_update_count++;
	}
	return ret;
}

//
// Loop filter
//

static s64 tgd_zl_get_freq_offset(struct tgd_zl_priv_data *priv, u32 delta_t_us,
				  s32 phase_offset_ns)
{
	// Fractional frequency offset estimate in signed Q48 format
	s64 ffo_est_q48 = priv->ffo_est_q48;
	s64 ffo_out_q48 = 0;

	// Compute y[k] = (Kp + Ki/(1 + z^-1)) * x[k], where x[k] is the TX-RX
	// timing offset and y[k] is the fractional frequency offset to apply
	s64 ki =
	    (pll_ki * (u64)delta_t_us * PLL_DELTA_T_SCALE) >> PLL_DELTA_T_SHIFT;
	ffo_est_q48 = ki * (s64)phase_offset_ns + ffo_est_q48;
	ffo_out_q48 = pll_kp * (s64)phase_offset_ns + ffo_est_q48;

	// Saturate with anti-windup
	if (ffo_out_q48 > PLL_MAX_FFO) {
		ffo_est_q48 += PLL_MAX_FFO - ffo_out_q48;
		ffo_out_q48 = PLL_MAX_FFO;
	}
	if (ffo_out_q48 < PLL_MIN_FFO) {
		ffo_est_q48 += PLL_MIN_FFO - ffo_out_q48;
		ffo_out_q48 = PLL_MIN_FFO;
	}

	priv->ffo_est_q48 = ffo_est_q48;
	dev_dbg(&priv->client->dev, "ffo_est_q48: %lld\n", ffo_est_q48);

	return ffo_out_q48;
}

static void tgd_zl_htsf_info_handler(int devidx, uint64_t macaddr,
				     int32_t txRxDiffNs, int32_t delayEstNs,
				     uint32_t rxStartUs)
{
	struct tgd_zl_priv_data *priv = g_priv;
	int ret;
	int64_t offset;
	u64 now_ns;
	u32 deltaUs;

	// Ignore events from other basebands
	if (devidx != tgd_device && macaddr != priv->tgd_device_mac) {
		return;
	}

	// Reset PLL state when device changes or no messages received for
	// awhile (e.g. because link was down)
	deltaUs = rxStartUs - priv->last_rxStartUs;
	if ((priv->initDone && deltaUs > PLL_RESET_TIME_US) ||
	    priv->tgd_device != tgd_device) {
		dev_dbg(&priv->client->dev,
			"Reset PLL: rxStartUs:%u deltaUs:%u devidx:%d "
			"tgd_device:%d --> %d\n",
			rxStartUs, deltaUs, devidx, priv->tgd_device,
			tgd_device);
		priv->tgd_device = tgd_device;
		priv->last_rxStartUs = 0;
		priv->last_htsf_ns = 0;
		priv->txRxDiffInit = 0;
		priv->initDone = 0;
		priv->htsf_rx_count = 0;
	}

	dev_dbg(&priv->client->dev,
		"Raw data: txRxDiffNs=%d, rxStartUs=%u, last_rxStartUs=%u, "
		"txRxDiffInit=%d, initDone=%d\n",
		txRxDiffNs, rxStartUs, priv->last_rxStartUs, priv->txRxDiffInit,
		priv->initDone);

	now_ns = tgd_zl_get_ktime_ns();

	// Skip first update
	if (priv->initDone == 0) {
		// Set new phase reference
		priv->txRxDiffInit = txRxDiffNs;
		priv->last_rxStartUs = rxStartUs;
		priv->last_htsf_ns = now_ns;
		priv->initDone = 1;
		return;
	}

	// Calculate offset
	offset = -tgd_zl_get_freq_offset(priv, rxStartUs - priv->last_rxStartUs,
					 priv->txRxDiffInit - txRxDiffNs);

	dev_dbg(&priv->client->dev, "Sending offset %lld\n", offset);

	// Send offset to PLL chip
	if ((ret = tgd_zl_send_offset(priv->client, priv->synce_dpll_index,
				      offset)) < 0) {
		dev_err(&priv->client->dev, "Can't send offset: %d\n", ret);
	}

	priv->last_rxStartUs = rxStartUs;
	priv->last_htsf_ns = now_ns;
	if (priv->htsf_rx_count <= PLL_LOCK_COUNT) {
		priv->htsf_rx_count++;
	}
}

//
// miscdevice (for ioctl commands)
//

#define ZL_IOCTL_SET_MODE _IOW(0xfb, 1, int)
#define ZL_IOCTL_SET_DEVICE _IOW(0xfb, 2, uint64_t)
#define ZL_IOCTL_GET_LOCKED _IOR(0xfb, 3, int)

static long tgd_zl_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct tgd_zl_priv_data *priv = g_priv;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case ZL_IOCTL_SET_MODE: {
		u8 mode = arg;
		if (tgd_zl_set_mode(priv->client, priv->synce_dpll_index,
				    mode) < 0) {
			dev_err(&priv->client->dev,
				"Failed to set DPLL mode to 0x%x\n", mode);
			return -EFAULT;
		}
		priv->synce_dpll_mode = mode;
		dev_info(&priv->client->dev, "Set DPLL mode to 0x%x\n", mode);
		break;
	}
	case ZL_IOCTL_SET_DEVICE: {
		uint64_t macaddr;
		if (copy_from_user(&macaddr, argp, sizeof(uint64_t))) {
			return -EFAULT;
		}
		if (priv->tgd_device_mac != macaddr) {
			priv->tgd_device_mac = macaddr;
			// Reset PLL state
			priv->last_rxStartUs = 0;
			priv->last_htsf_ns = 0;
			priv->txRxDiffInit = 0;
			priv->initDone = 0;
			priv->htsf_rx_count = 0;
			dev_info(&priv->client->dev,
				 "Set DPLL interface to MAC 0x%012llx\n",
				 macaddr);
		}
		break;
	}
	case ZL_IOCTL_GET_LOCKED: {
		u8 locked = 0;

		if (priv->synce_dpll_mode == ZL_DPLL_MODE_NCO) {
			// wireless interface, infer lock via # HTSF messages
			if (priv->tgd_device != -1 ||
			    priv->tgd_device_mac != 0) {
				u64 delta = 0;
				if (priv->htsf_rx_count > PLL_LOCK_COUNT) {
					// verify we received a message recently
					u64 now_ns = tgd_zl_get_ktime_ns();
					delta = now_ns - priv->last_htsf_ns;
					if (delta < PLL_RESET_TIME_US * 1000) {
						locked = 1;
					}
				}
				dev_dbg(&priv->client->dev,
					"Lock to WiGig device => %d (MAC = "
					"0x%012llx, "
					"HTSF count = %d, time delta = %lld)\n",
					locked, priv->tgd_device_mac,
					priv->htsf_rx_count, delta);
			}
		} else if (priv->synce_dpll_mode ==
			   ZL_DPLL_MODE_REFLOCK_SYNCE) {
			// wired interface, check lock to SyncE input (REF3P)
			s32 dpll_state, dpll_status;

			if ((dpll_state = tgd_zl_get_dpll_state_refsel(
				 priv->client, priv->synce_dpll_index)) < 0) {
				dev_err(&priv->client->dev,
					"Failed to read DPLL state\n");
				return -EFAULT;
			}
			if ((dpll_status = tgd_zl_get_dpll_status(
				 priv->client, priv->synce_dpll_index)) < 0) {
				dev_err(&priv->client->dev,
					"Failed to read DPLL status\n");
				return -EFAULT;
			}

			locked = (dpll_state == 0x64 /* REF3P locked */) &&
				 (dpll_status ==
				  0x5 /* holdover ready and locked */);

			dev_dbg(&priv->client->dev,
				"Lock to SyncE => %d (DPLL state = 0x%x, "
				"status = 0x%x)\n",
				locked, dpll_state, dpll_status);
		}

		if (copy_to_user(argp, &locked, sizeof(locked))) {
			return -EFAULT;
		}
		break;
	}
	default:
		return -ENOTTY;
	}

	return 0;
}

static const struct file_operations tgd_zl_fops = {
    .owner = THIS_MODULE,
    .unlocked_ioctl = tgd_zl_ioctl,
};

static struct miscdevice tgd_zl_miscdev = {
    .minor = MISC_DYNAMIC_MINOR,
    .name = "zl3079x",
    .fops = &tgd_zl_fops,
};

//
// i2c driver
//

static int tgd_zl_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	struct tgd_zl_priv_data *priv = NULL;
	int ret = 0;
	int chip_id;
	char chip_id_char = '\0';

	// i2c init
	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_BYTE_DATA |
					 I2C_FUNC_SMBUS_WORD_DATA |
					 I2C_FUNC_SMBUS_I2C_BLOCK)) {
		dev_err(&client->dev, "i2c_check_functionality failed\n");
		ret = -ENODEV;
		goto out;
	}

	// alloc priv data
	priv = devm_kzalloc(&client->dev, sizeof(struct tgd_zl_priv_data),
			    GFP_KERNEL);
	if (priv == NULL) {
		dev_err(&client->dev, "Failed to allocate memory\n");
		ret = -ENOMEM;
		goto out;
	}
	i2c_set_clientdata(client, priv);
	priv->client = client;

	// read chip id
	chip_id = tgd_zl_read_reg16(client, ZL_REG_ID);
	if (chip_id < 0) {
		dev_err(&client->dev, "Couldn't read register (%#x)\n",
			ZL_REG_ID);
		ret = -EIO;
		goto out;
	}
	switch (chip_id) {
	case ZL_CHIP_ID_30791:
		chip_id_char = '1';
		break;
	case ZL_CHIP_ID_30793:
		chip_id_char = '3';
		break;
	case ZL_CHIP_ID_30795:
		chip_id_char = '5';
		break;
	}
	if (chip_id_char == '\0') {
		dev_err(&client->dev, "Unknown chip id (%#x)\n", chip_id);
		ret = -ENODEV;
		goto out;
	}
	dev_info(&client->dev, "Found device: ZL3079%c at address %#x\n",
		 chip_id_char, client->addr & 0x7f);

	// Determine DPLL config to use
	if (pps_source_gps) {
		// Use two clock domains (DPLL0 and DPLL1)
		priv->gps_dpll_index = DPLL_INDEX_0;
		priv->synce_dpll_index = DPLL_INDEX_1;
	} else {
		// Use one clock domain (all clocks use DPLL0)
		priv->gps_dpll_index = DPLL_INDEX_INVALID;
		priv->synce_dpll_index = DPLL_INDEX_0;
	}
	dev_info(&client->dev,
		 "DPLL config: pps_source_gps:%d gps_dpll:%d synce_dpll:%d\n",
		 pps_source_gps, priv->gps_dpll_index, priv->synce_dpll_index);

	// Configure GPOUT0 duty cycle (pulse width)
	if ((ret = tgd_zl_write_reg32(client, ZL_REG_DPLL_GP_OUT_WIDTH_0,
				      ZL_GP_OUT_WIDTH_20_80)) < 0) {
		dev_err(&client->dev, "Can't set GPOUT0 width\n");
		goto out;
	}

	// If GPS DPLL enabled, set to GPS (REF0P) input instead of free running
	if (priv->gps_dpll_index != DPLL_INDEX_INVALID) {
		if ((ret = tgd_zl_set_mode(client, priv->gps_dpll_index,
					   ZL_DPLL_MODE_REFLOCK_GPS)) < 0) {
			dev_err(&client->dev, "Can't set GPS DPLL mode\n");
			goto out;
		}

		if ((ret = tgd_zl_set_dpll_config(client, priv->gps_dpll_index,
						  ZL_DPLL_BW_GPS,
						  ZL_DPLL_PSL_GPS)) < 0) {
			dev_err(&client->dev, "Can't configure GPS DPLL\n");
			goto out;
		}
	}

	// Set SyncE DPLL to NCO mode
	priv->synce_dpll_mode = ZL_DPLL_MODE_NCO;
	if ((ret = tgd_zl_set_mode(client, priv->synce_dpll_index,
				   priv->synce_dpll_mode)) < 0) {
		dev_err(&client->dev, "Can't set SyncE DPLL into NCO mode\n");
		goto out;
	}
	priv->ffo_est_q48 = 0;

	if ((ret = tgd_zl_set_dpll_config(client, priv->synce_dpll_index,
					  ZL_DPLL_BW_SYNCE,
					  ZL_DPLL_PSL_SYNCE)) < 0) {
		dev_err(&client->dev, "Can't configure SyncE DPLL\n");
		goto out;
	}

	// Configure HP-Synth1 to use SyncE DPLL
	if ((ret = tgd_zl_set_synth1_dpll(client, priv->synce_dpll_index)) <
	    0) {
		dev_err(&client->dev, "Can't set Synth1 to use SyncE DPLL\n");
		goto out;
	}

	// register miscdevice
	if (misc_register(&tgd_zl_miscdev)) {
		dev_err(&client->dev, "misc_register failed\n");
	}

	priv->tgd_device_mac = 0;
	priv->htsf_rx_count = 0;
	g_priv = priv;
	tgd_register_htsf_info_handler(tgd_zl_htsf_info_handler);
	return 0;

out:
	devm_kfree(&client->dev, priv);
	return ret;
}

static int tgd_zl_remove(struct i2c_client *client)
{
	struct tgd_zl_priv_data *priv = i2c_get_clientdata(client);

	misc_deregister(&tgd_zl_miscdev);
	tgd_unregister_htsf_info_handler(tgd_zl_htsf_info_handler);
	devm_kfree(&client->dev, priv);
	dev_info(&client->dev, "Device removed\n");
	return 0;
}

#ifdef CONFIG_OF
static struct of_device_id tgd_zl_ids[] = {{.compatible = "facebook,zl3079x"},
					   {}};
MODULE_DEVICE_TABLE(of, tgd_zl_ids);
#endif

static const struct i2c_device_id tgd_zl_id_table[] = {{"zl3079x", 0}, {}};

MODULE_DEVICE_TABLE(i2c, tgd_zl_id_table);

static struct i2c_driver tgd_zl_driver = {
    .driver = {.owner = THIS_MODULE, .name = "fb_tgd_zl3079x"},
    .id_table = tgd_zl_id_table,
    .probe = tgd_zl_probe,
    .remove = tgd_zl_remove,
};

module_i2c_driver(tgd_zl_driver);

MODULE_DESCRIPTION("Facebook Microsemi ZL3079x Network Synchronizer Driver");
MODULE_AUTHOR("Alex Landau <alandau@fb.com>");
MODULE_LICENSE("Dual MIT/GPL");
