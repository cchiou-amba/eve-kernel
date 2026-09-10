// SPDX-License-Identifier: GPL-2.0
/*
 * Ambarella Temperature Sensor Driver
 *
 * Copyright (C) 2021 by Ambarella, Inc.
 * http://www.ambarella.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2  of
 * the License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/mutex.h>
#include <linux/delay.h>
#include <linux/jiffies.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/sys_soc.h>
#include <linux/mfd/syscon.h>

/* External function declaration: exported from ambarella_scm.c */
extern int ambarella_otp_get_ts_params(u32 *ts_cbuf);

#define DRIVER_NAME "ambarella_ts"

/* Temperature sensor register address (modify according to your hardware) */
#define TEMP_SENSOR_REG_OFFSET	0x00000004

#define SEC_BOOT_STATUS			BIT(0)

#define READ_FSM_ENABLE			BIT(23)
#define READ_ENABLE			BIT(22)
#define DBG_READ_MODE			BIT(21)
#define FSM_WRITE_MODE			BIT(19)
#define PROG_ENABLE			BIT(18)
#define PROG_FSM_ENABLE			BIT(17)

/* OTP related definitions */
#define OTP_BIT_SIZE			(0x1000)
#define OTP_BIT_MASK			(OTP_BIT_SIZE - 1)
#define OBSV_READ_RDY			BIT(1)
#define OBSV_READ_DONE			BIT(0)


#define TEMP_SENSOR_EXT_CLK_MODE	BIT(22)
#define TEMP_SENSOR_2_AVG_MODE		BIT(21)

#define OTP_SL		5
#define OTP_HSL 	9
#define OTP_LSL		5

/* Default register offsets (used if platform-specific data is not matched) */
#define TEMP_SENSOR_ENABLE_OFFSET_DEFAULT0		(0x80)
#define TEMP_SENSOR_ENABLE_OFFSET_DEFAULT1		(0x0)

typedef int (*temp_convert_func_t)(struct device *dev, int raw, int channel, int cal_high, int cal_low,
				   int *temp_mc);

struct ambarella_ts_platform_data {
	u32 enable_offset;
	u32 otp_ts_offset;
	u32 otp_ts_num;
	u32 otp_ctrl1_offset;
	u32 otp_obsv_offset;
	u32 otp_read_dout_offset;
	u32 otp_area_use;
	temp_convert_func_t convert_func;
};

#define TEMP_SENSOR_GROUP_CTL0_OFFSET			(0x00)
#define TEMP_SENSOR_GROUP_CTL1_OFFSET			(0x04)
#define TEMP_SENSOR_GROUP_SAMPLE_NUM_OFFSET		(0x08)
#define TEMP_SENSOR_GROUP_PROBE0_OFFSET			(0x0c)
#define TEMP_SENSOR_GROUP_PROBE1_OFFSET			(0x10)
#define TEMP_SENSOR_GROUP_DATA_BASE_OFFSET		(0x14)
#define TEMP_SENSOR_GROUP_DATA_OFFSET(n)		(TEMP_SENSOR_GROUP_DATA_BASE_OFFSET + ((n) * 0x4))

#define TEMP_SENSOR_TS0_CTL0_OFFSET_FROM_ENABLE		(0x04)
#define TEMP_SENSOR_TS1_CTL0_OFFSET_FROM_ENABLE		(0x38)


#define TEMP_SENSOR_TIMEOUT_US	1000000
#define TEMP_SENSOR_POLL_US	500

#define TEMP_SENSOR_CACHE_MS	100

#define MAX_TEMP_SENSORS	8
#define MAX_TEMP_GROUPS		4

struct temp_sensor_group {
	void __iomem *base;
	unsigned long reg_base_offset;
	int start_channel;
	int num_channels;
	u32 start_bit;
};

struct ambarella_ts_data {
	struct device *dev;
	struct device *hwmon_dev;
	void __iomem *base;
	struct regmap	*scr_regmap;
	u32	boot_sts_offset;
	struct regmap	*otp_regmap;
	struct mutex lock;
	const struct ambarella_ts_platform_data *pdata;
	int num_sensors;
	int num_groups;
	struct temp_sensor_group groups[MAX_TEMP_GROUPS];
	int temp[MAX_TEMP_SENSORS];
	int sensor_group[MAX_TEMP_SENSORS];
	int sensor_index_in_group[MAX_TEMP_SENSORS];
	int cached_raw[MAX_TEMP_SENSORS];
	unsigned long cache_timestamp[MAX_TEMP_GROUPS];
	int otp_cal_high[MAX_TEMP_SENSORS];
	int otp_cal_low[MAX_TEMP_SENSORS];
};

static int convert_temp_fun0(struct device *dev, int raw, int channel, int cal_high, int cal_low, int *temp_mc)
{
	int val;
	int otp_cali;
	int otp_sl = OTP_SL;

	otp_cali = cal_high - cal_low;

	if (otp_cali == 0 || cal_low == 0) {
		dev_err(dev, "Invalid OTP calibration values (high=%d, low=%d) for sensor %d\n",
			cal_high, cal_low, channel);
		return -EINVAL;
	}

	if (raw >= cal_low) {
		val = ((raw - cal_low) * 60 * 1000 / otp_cali) + 25000;
	} else {
		val = ((raw - cal_low) * 60 * 1000 / otp_cali * 65 / (57 + otp_sl)) + 25000;
	}

	*temp_mc = val;
	return 0;
}

static int convert_temp_fun1(struct device *dev, int raw, int channel, int cal_high, int cal_low, int *temp_mc)
{
	int val;
	int otp_cali;
	int otp_lsl = OTP_LSL;
	int otp_hsl = OTP_HSL;

	otp_cali = cal_high - cal_low;

	if (otp_cali == 0 || cal_low == 0) {
		dev_err(dev, "Invalid OTP calibration values (high=%d, low=%d) for sensor %d\n",
			cal_high, cal_low, channel);
		return -EINVAL;
	}

	if (raw <= cal_low) {
		val = ((raw - cal_low) * 80 * 65 * 1000 / (otp_cali * (57 + otp_lsl))) + 25000;
	} else if (raw >= cal_high) {
		val = ((raw - cal_high) * 80 * 45 * 1000 / (otp_cali * (37 + otp_hsl))) + 105000;
	} else {
		val = ((raw - cal_low) * 80 * 1000 / otp_cali) + 25000;
	}

	*temp_mc = val;
	return 0;
}

static const struct ambarella_ts_platform_data amba_ts_pdata0 = {
	.enable_offset = TEMP_SENSOR_ENABLE_OFFSET_DEFAULT0,
	.otp_ts_offset = 0x240,
	.otp_ts_num = 2,
	.otp_ctrl1_offset = 0xA0,
	.otp_obsv_offset = 0xA4,
	.otp_read_dout_offset = 0xA8,
	.otp_area_use = 0,
	.convert_func = convert_temp_fun0,
};

static const struct ambarella_ts_platform_data amba_ts_pdata1 = {
	.enable_offset = TEMP_SENSOR_ENABLE_OFFSET_DEFAULT0,
	.otp_ts_offset = 0x440,
	.otp_ts_num = 8,
	.otp_ctrl1_offset = 0x0,
	.otp_obsv_offset = 0x4,
	.otp_read_dout_offset = 0x8,
	.otp_area_use = 1,
	.convert_func = convert_temp_fun1,
};

static const struct soc_device_attribute ambarella_ts_socinfo[] = {
	{ .soc_id = "cv75", .data = &amba_ts_pdata0 },
	{ .soc_id = "cv3ad655", .data = &amba_ts_pdata1 },
	{ /* sentinel */ }
};

static int ambarella_ts_config_probe_id(struct ambarella_ts_data *data)
{
	int i, j;
	u32 ctl0_val;

	if (!data->base)
		return 0;

	ctl0_val = TEMP_SENSOR_EXT_CLK_MODE | TEMP_SENSOR_2_AVG_MODE;

	for (i = 0; i < data->num_groups; i++) {
		struct temp_sensor_group *group = &data->groups[i];
		u32 probe0_val = 0;
		u32 probe1_val = 0;
		int bit_pos = 0;
		u32 sample_num;

		for (j = 0; j < group->num_channels; j++) {
			u32 probe_id;

			probe_id = j & 0xF;

			if (bit_pos < 32) {
				probe0_val |= (probe_id << bit_pos);
			} else {
				probe1_val |= (probe_id << (bit_pos - 32));
			}
			bit_pos += 4;

			if (bit_pos < 32) {
				probe0_val |= (probe_id << bit_pos);
			} else {
				probe1_val |= (probe_id << (bit_pos - 32));
			}
			bit_pos += 4;

			if (bit_pos >= 64) {
				dev_warn(data->dev, "Group %d probe ID overflow, truncating\n", i);
				break;
			}
		}

		sample_num = group->num_channels * 2 - 1;

		if (group->base) {
			writel(ctl0_val, group->base + TEMP_SENSOR_GROUP_CTL0_OFFSET);
			writel(sample_num, group->base + TEMP_SENSOR_GROUP_SAMPLE_NUM_OFFSET);
			writel(probe0_val, group->base + TEMP_SENSOR_GROUP_PROBE0_OFFSET);
			if (bit_pos > 32) {
				writel(probe1_val, group->base + TEMP_SENSOR_GROUP_PROBE1_OFFSET);
				dev_dbg(data->dev, "Group %d: CTL0=0x%08x, SAMPLE_NUM=%u, PROBE0=0x%08x, PROBE1=0x%08x\n",
					 i, ctl0_val, sample_num, probe0_val, probe1_val);
			} else {
				dev_dbg(data->dev, "Group %d: CTL0=0x%08x, SAMPLE_NUM=%u, PROBE0=0x%08x (PROBE1 not needed)\n",
					 i, ctl0_val, sample_num, probe0_val);
			}
		} else {
			writel(ctl0_val, data->base + group->reg_base_offset + TEMP_SENSOR_GROUP_CTL0_OFFSET);
			writel(sample_num, data->base + group->reg_base_offset + TEMP_SENSOR_GROUP_SAMPLE_NUM_OFFSET);
			writel(probe0_val, data->base + group->reg_base_offset + TEMP_SENSOR_GROUP_PROBE0_OFFSET);
			if (bit_pos > 32) {
				writel(probe1_val, data->base + group->reg_base_offset + TEMP_SENSOR_GROUP_PROBE1_OFFSET);
				dev_dbg(data->dev, "Group %d: CTL0=0x%08x, SAMPLE_NUM=%u, PROBE0=0x%08x, PROBE1=0x%08x\n",
					 i, ctl0_val, sample_num, probe0_val, probe1_val);
			} else {
				dev_dbg(data->dev, "Group %d: CTL0=0x%08x, SAMPLE_NUM=%u, PROBE0=0x%08x (PROBE1 not needed)\n",
					 i, ctl0_val, sample_num, probe0_val);
			}
		}
	}

	return 0;
}

static int ambarella_ts_config(struct ambarella_ts_data *data)
{
	if (!data->base)
		return -ENODEV;

	ambarella_ts_config_probe_id(data);

	udelay(100);

	return 0;
}

static int otp_read32(struct ambarella_ts_data *data, int addr, u32 *value)
{
	u32 obsv_val;
	int ret;
	unsigned long timeout;
	struct regmap *regmap;
	struct device *dev;
	u32 ctrl1_offset, obsv_offset, read_dout_offset;
	bool use_scr_regmap;

	if (!data || !value) {
		return -EINVAL;
	}

	dev = data->dev;

	if (!data->pdata) {
		dev_err(dev, "Platform data not available\n");
		return -ENODEV;
	}

	use_scr_regmap = (data->pdata->otp_area_use == 0);

	if (use_scr_regmap) {
		regmap = data->scr_regmap;
		if (!regmap) {
			dev_err(dev, "scr_regmap not available\n");
			return -ENODEV;
		}
	} else {
		regmap = data->otp_regmap;
		if (!regmap) {
			dev_err(dev, "otp_regmap not available\n");
			return -ENODEV;
		}
	}

	ctrl1_offset = data->pdata->otp_ctrl1_offset;
	obsv_offset = data->pdata->otp_obsv_offset;
	read_dout_offset = data->pdata->otp_read_dout_offset;

	if (addr < 0 || addr >= OTP_BIT_SIZE || (addr & 0x1F)) {
		dev_err(dev, "otp read address 0x%x is invalid\n", addr);
		return -EINVAL;
	}

	ret = regmap_write(regmap, ctrl1_offset, 0);
	if (ret)
		return ret;

	ret = regmap_clear_bits(regmap, ctrl1_offset, FSM_WRITE_MODE);
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, ctrl1_offset, DBG_READ_MODE);
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, ctrl1_offset, READ_FSM_ENABLE);
	if (ret)
		return ret;

	timeout = jiffies + msecs_to_jiffies(100);
	do {
		ret = regmap_read(regmap, obsv_offset, &obsv_val);
		if (ret)
			return ret;
		if (obsv_val & OBSV_READ_RDY)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "otp read timeout waiting for READ_RDY\n");
			return -ETIMEDOUT;
		}
		udelay(10);
	} while (1);

	ret = regmap_update_bits(regmap, ctrl1_offset, OTP_BIT_MASK, addr & OTP_BIT_MASK);
	if (ret)
		return ret;

	ret = regmap_set_bits(regmap, ctrl1_offset, READ_ENABLE);
	if (ret)
		return ret;

	timeout = jiffies + msecs_to_jiffies(100);
	do {
		ret = regmap_read(regmap, obsv_offset, &obsv_val);
		if (ret)
			return ret;
		if (obsv_val & OBSV_READ_DONE)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "otp read timeout waiting for READ_DONE\n");
			return -ETIMEDOUT;
		}
		udelay(10);
	} while (1);

	ret = regmap_read(regmap, read_dout_offset, value);
	if (ret)
		return ret;

	ret = regmap_clear_bits(regmap, ctrl1_offset, READ_ENABLE);
	if (ret)
		return ret;

	timeout = jiffies + msecs_to_jiffies(100);
	do {
		ret = regmap_read(regmap, obsv_offset, &obsv_val);
		if (ret)
			return ret;
		if (obsv_val & OBSV_READ_RDY)
			break;
		if (time_after(jiffies, timeout)) {
			dev_err(dev, "otp read timeout waiting for READ_RDY after read\n");
			return -ETIMEDOUT;
		}
		udelay(10);
	} while (1);

	return 0;
}

static int ambarella_ts_get_otp_cal(struct device *dev, struct ambarella_ts_data *data)
{
	int i;
	int ret;
	u32 boot_sts;
	bool is_secure_boot;

	if (!data || !data->scr_regmap)
		return -ENODEV;

	ret = regmap_read(data->scr_regmap, data->boot_sts_offset, &boot_sts);
	if (ret) {
		dev_err(dev, "Failed to read boot status register\n");
		return ret;
	}

	is_secure_boot = !!(boot_sts & SEC_BOOT_STATUS);

	if (is_secure_boot) {
		u32 ts_cbuf[8];
		int num_otp_sensors;

		ret = ambarella_otp_get_ts_params(ts_cbuf);
		if (ret) {
			dev_warn(dev, "Failed to get OTP TS params via SMC, using defaults\n");
			for (i = 0; i < data->num_sensors; i++) {
				data->otp_cal_high[i] = 0;
				data->otp_cal_low[i] = 0;
			}
			return 0;
		}

		dev_dbg(dev, "OTP raw data from SMC (secure boot mode):\n");
		for (i = 0; i < 8; i++) {
			dev_dbg(dev, "  ts_cbuf[%d] = 0x%08x\n", i, ts_cbuf[i]);
		}

		num_otp_sensors = data->pdata ? data->pdata->otp_ts_num : 0;
		if (num_otp_sensors == 0 || num_otp_sensors > 8) {
			dev_warn(dev, "Invalid OTP TS num: %d, using defaults\n", num_otp_sensors);
			num_otp_sensors = 0;
		}

		dev_dbg(dev, "Parsed OTP calibration values (secure boot mode):\n");
		for (i = 0; i < data->num_sensors; i++) {
			if (i < num_otp_sensors && i < 8) {
				data->otp_cal_low[i] = ts_cbuf[i] & 0x1FF;
				data->otp_cal_high[i] = (ts_cbuf[i] >> 9) & 0x1FF;
				dev_dbg(dev, "  Sensor %d: raw=0x%08x, low=%d, high=%d\n",
					 i, ts_cbuf[i], data->otp_cal_low[i], data->otp_cal_high[i]);
			} else {
				data->otp_cal_high[i] = 0;
				data->otp_cal_low[i] = 0;
				dev_dbg(dev, "  Sensor %d: using defaults (low=0, high=0)\n", i);
			}
		}
	} else {
		u32 otp_addr_base;
		u32 otp_val;

		otp_addr_base = data->pdata ? data->pdata->otp_ts_offset : 0;
		if (otp_addr_base == 0) {
			dev_warn(dev, "OTP TS offset not configured, using defaults\n");
			for (i = 0; i < data->num_sensors; i++) {
				data->otp_cal_high[i] = 0;
				data->otp_cal_low[i] = 0;
			}
			return 0;
		}

		dev_dbg(dev, "Reading OTP calibration values (non-secure boot mode):\n");
		dev_dbg(dev, "  OTP base address: 0x%x\n", otp_addr_base);
		for (i = 0; i < data->num_sensors; i++) {
			u32 otp_addr = otp_addr_base + (i * 32);

			ret = otp_read32(data, otp_addr, &otp_val);
			if (ret) {
				dev_warn(dev, "  Sensor %d: Failed to read OTP at 0x%x, using defaults\n",
					 i, otp_addr);
				data->otp_cal_high[i] = 0;
				data->otp_cal_low[i] = 0;
				continue;
			}

			data->otp_cal_low[i] = otp_val & 0x1FF;
			data->otp_cal_high[i] = (otp_val >> 9) & 0x1FF;
			dev_dbg(dev, "  Sensor %d: addr=0x%x, raw=0x%08x, low=%d, high=%d\n",
				 i, otp_addr, otp_val, data->otp_cal_low[i], data->otp_cal_high[i]);
		}
	}

	return 0;
}

static int ambarella_ts_init_sensor(struct ambarella_ts_data *data)
{
	int ret;

	ret = ambarella_ts_config(data);
	if (ret < 0) {
		dev_err(data->dev, "Failed to config temperature sensor\n");
		return ret;
	}

	udelay(1000);

	return 0;
}

static int ambarella_ts_start_conversion(struct ambarella_ts_data *data, int group_id)
{
	struct temp_sensor_group *group;
	void __iomem *enable_base;
	u32 enable_val;
	u32 start_mask;

	if (group_id < 0 || group_id >= data->num_groups)
		return -EINVAL;

	group = &data->groups[group_id];

	if (!data->base || !data->pdata)
		return -ENODEV;

	enable_base = data->base;

	enable_val = readl(enable_base + data->pdata->enable_offset);

	start_mask = BIT(group->start_bit);

	if (enable_val & start_mask) {
		dev_dbg(data->dev, "Temperature sensor group %d is still busy (bit %u)\n",
			 group_id, group->start_bit);
		return -EBUSY;
	}

	enable_val |= start_mask;
	writel(enable_val, enable_base + data->pdata->enable_offset);

	return 0;
}

static int ambarella_ts_wait_conversion(struct ambarella_ts_data *data, int group_id)
{
	struct temp_sensor_group *group;
	void __iomem *enable_base;
	u32 enable_val;
	u32 start_mask;
	unsigned long timeout;

	if (group_id < 0 || group_id >= data->num_groups)
		return -EINVAL;

	group = &data->groups[group_id];

	if (!data->base || !data->pdata)
		return -ENODEV;

	enable_base = data->base;

	start_mask = BIT(group->start_bit);

	timeout = jiffies + usecs_to_jiffies(TEMP_SENSOR_TIMEOUT_US);

	do {
		enable_val = readl(enable_base + data->pdata->enable_offset);

		if (!(enable_val & start_mask))
			return 0;

		if (time_after(jiffies, timeout)) {
			dev_err(data->dev, "Temperature sensor group %d (bit %u) conversion timeout\n",
			       group_id, group->start_bit);
			return -ETIMEDOUT;
		}

		udelay(TEMP_SENSOR_POLL_US);

	} while (1);
}

static struct temp_sensor_group *get_sensor_group(struct ambarella_ts_data *data, int channel)
{
	int group_id;

	if (channel < 0 || channel >= data->num_sensors)
		return NULL;

	group_id = data->sensor_group[channel];
	if (group_id < 0 || group_id >= data->num_groups)
		return NULL;

	return &data->groups[group_id];
}

static int read_temp_raw(struct ambarella_ts_data *data, int channel)
{
	u32 reg_val;
	int ret;
	struct temp_sensor_group *group;
	int index_in_group;
	int group_id;
	int sensor_ch;
	unsigned long cache_age;
	unsigned long now = jiffies;

	if (channel < 0 || channel >= data->num_sensors) {
		dev_err(data->dev, "Invalid sensor channel: %d (max: %d)\n", channel, data->num_sensors - 1);
		return -EINVAL;
	}

	group = get_sensor_group(data, channel);
	if (!group) {
		dev_err(data->dev, "Invalid group for sensor %d\n", channel);
		return -EINVAL;
	}

	group_id = data->sensor_group[channel];

	if (data->cache_timestamp[group_id] != 0) {
		cache_age = jiffies_to_msecs(now - data->cache_timestamp[group_id]);
		if (cache_age < TEMP_SENSOR_CACHE_MS) {
			return data->cached_raw[channel];
		}
	}

	ret = ambarella_ts_start_conversion(data, group_id);
	if (ret < 0) {
		if (ret == -EBUSY) {
			ret = ambarella_ts_wait_conversion(data, group_id);
			if (ret < 0)
				return ret;
		} else {
			return ret;
		}
	} else {
		ret = ambarella_ts_wait_conversion(data, group_id);
		if (ret < 0)
			return ret;
	}

	for (sensor_ch = 0; sensor_ch < data->num_sensors; sensor_ch++) {
		if (data->sensor_group[sensor_ch] == group_id) {
			index_in_group = data->sensor_index_in_group[sensor_ch];

			if (group->base) {
				reg_val = readl(group->base + TEMP_SENSOR_GROUP_DATA_OFFSET(index_in_group));
			} else {
				reg_val = readl(data->base + group->reg_base_offset + TEMP_SENSOR_GROUP_DATA_OFFSET(index_in_group));
			}

			reg_val = (reg_val >> 16) & 0x1FF;

			data->cached_raw[sensor_ch] = reg_val;
		}
	}

	data->cache_timestamp[group_id] = now;

	return data->cached_raw[channel];
}

static int convert_raw_to_temp(int raw, int channel, struct ambarella_ts_data *data, int *temp_mc)
{
	int cal_high, cal_low;
	temp_convert_func_t convert_func;

	cal_high = data->otp_cal_high[channel];
	cal_low = data->otp_cal_low[channel];

	if (!data->pdata || !data->pdata->convert_func) {
		dev_err(data->dev, "No temperature conversion function specified for sensor %d\n", channel);
		return -EINVAL;
	}

	convert_func = data->pdata->convert_func;

	return convert_func(data->dev, raw, channel, cal_high, cal_low, temp_mc);
}

static int ambarella_ts_read(struct device *dev, enum hwmon_sensor_types type,
			    u32 attr, int channel, long *val)
{
	struct ambarella_ts_data *data = dev_get_drvdata(dev);
	int ret;
	int raw;

	if (type != hwmon_temp)
		return -EOPNOTSUPP;

	if (channel < 0 || channel >= data->num_sensors)
		return -EINVAL;

	switch (attr) {
	case hwmon_temp_input:
		mutex_lock(&data->lock);
		ret = read_temp_raw(data, channel);
		if (ret < 0) {
			mutex_unlock(&data->lock);
			return ret;
		}
		raw = ret;

		ret = convert_raw_to_temp(raw, channel, data, &data->temp[channel]);
		if (ret < 0) {
			dev_err(data->dev, "Temperature conversion failed for sensor %d (raw=%d), check OTP calibration\n",
				channel, raw);
			mutex_unlock(&data->lock);
			return ret;
		}

		*val = data->temp[channel];
		mutex_unlock(&data->lock);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static ssize_t temp_raw_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct ambarella_ts_data *data = dev_get_drvdata(dev);
	struct sensor_device_attribute *sensor_attr = to_sensor_dev_attr(attr);
	int channel = sensor_attr->index;
	int ret;

	mutex_lock(&data->lock);
	ret = read_temp_raw(data, channel);
	mutex_unlock(&data->lock);

	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", ret);
}

static int ambarella_ts_create_temp_attrs(struct device *dev, struct ambarella_ts_data *data)
{
	struct sensor_device_attribute *attrs;
	char name[16];
	int i, ret;
	int attr_count = 0;

	attrs = devm_kcalloc(dev, data->num_sensors, sizeof(*attrs), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	for (i = 0; i < data->num_sensors; i++) {
		snprintf(name, sizeof(name), "temp%d_raw", i + 1);

		sysfs_attr_init(&attrs[attr_count].dev_attr.attr);
		attrs[attr_count].dev_attr.attr.name = devm_kstrdup(dev, name, GFP_KERNEL);
		if (!attrs[attr_count].dev_attr.attr.name)
			goto err_cleanup;

		attrs[attr_count].dev_attr.attr.mode = 0444;
		attrs[attr_count].dev_attr.show = temp_raw_show;
		attrs[attr_count].index = i;

		ret = device_create_file(dev, &attrs[attr_count].dev_attr);
		if (ret < 0) {
			dev_err(dev, "Failed to create %s attribute\n", name);
			goto err_cleanup;
		}
		attr_count++;
	}

	return 0;

err_cleanup:
	while (--attr_count >= 0)
		device_remove_file(dev, &attrs[attr_count].dev_attr);
	return -ENOMEM;
}


static umode_t ambarella_ts_is_visible(const void *data,
				      enum hwmon_sensor_types type,
				      u32 attr, int channel)
{
	const struct ambarella_ts_data *ts_data = data;

	if (type != hwmon_temp)
		return 0;

	if (channel < 0 || channel >= ts_data->num_sensors)
		return 0;

	switch (attr) {
	case hwmon_temp_input:
		return 0444;
	default:
		return 0;
	}
}

static struct hwmon_channel_info *ambarella_ts_info_temp = NULL;
static const struct hwmon_channel_info *ambarella_ts_info_array[2] = { NULL, NULL };

static int ambarella_ts_init_channel_info(struct ambarella_ts_data *data)
{
	u32 *temp_config;
	int i;

	temp_config = kcalloc(data->num_sensors + 1, sizeof(u32), GFP_KERNEL);
	if (!temp_config)
		return -ENOMEM;

	for (i = 0; i < data->num_sensors; i++)
		temp_config[i] = HWMON_T_INPUT;

	ambarella_ts_info_temp = kzalloc(sizeof(*ambarella_ts_info_temp), GFP_KERNEL);
	if (!ambarella_ts_info_temp) {
		kfree(temp_config);
		return -ENOMEM;
	}

	ambarella_ts_info_temp->type = hwmon_temp;
	ambarella_ts_info_temp->config = temp_config;

	ambarella_ts_info_array[0] = ambarella_ts_info_temp;
	ambarella_ts_info_array[1] = NULL;

	return 0;
}

static void ambarella_ts_free_channel_info(void)
{
	if (ambarella_ts_info_temp) {
		kfree(ambarella_ts_info_temp->config);
		kfree(ambarella_ts_info_temp);
		ambarella_ts_info_temp = NULL;
		ambarella_ts_info_array[0] = NULL;
	}
}

static const struct hwmon_ops ambarella_ts_hwmon_ops = {
	.is_visible = ambarella_ts_is_visible,
	.read = ambarella_ts_read,
};

static const struct hwmon_chip_info *ambarella_ts_get_chip_info(struct ambarella_ts_data *data)
{
	static const struct hwmon_chip_info chip_info = {
		.ops = &ambarella_ts_hwmon_ops,
		.info = ambarella_ts_info_array,
	};

	return &chip_info;
}

static int ambarella_ts_parse_groups(struct device *dev, struct ambarella_ts_data *data)
{
	struct device_node *np = dev->of_node;
	struct device_node *group_node;
	u32 num_sensors = 0;
	u32 num_groups = 0;
	int ret, i, j;
	int channel = 0;

	if (!np) {
		dev_err(dev, "No device tree node found, sensor configuration required\n");
		return -ENODEV;
	}

	group_node = of_get_child_by_name(np, "sensor-groups");
	if (!group_node) {
		ret = of_property_read_u32(np, "num-sensors", &num_sensors);
		if (ret < 0)
			num_sensors = 1;

		if (num_sensors < 1 || num_sensors > MAX_TEMP_SENSORS) {
			dev_err(dev, "Invalid number of sensors: %u (max: %d)\n",
				num_sensors, MAX_TEMP_SENSORS);
			return -EINVAL;
		}

		data->num_groups = 1;
		data->num_sensors = num_sensors;
		data->groups[0].base = NULL;
		data->groups[0].reg_base_offset = data->pdata->enable_offset + TEMP_SENSOR_REG_OFFSET;
		data->groups[0].start_channel = 0;
		data->groups[0].num_channels = num_sensors;
		data->groups[0].start_bit = 0;

		for (i = 0; i < num_sensors; i++) {
			data->sensor_group[i] = 0;
			data->sensor_index_in_group[i] = i;
		}

		return 0;
	}

	num_groups = of_get_child_count(group_node);
	if (num_groups == 0 || num_groups > MAX_TEMP_GROUPS) {
		dev_err(dev, "Invalid number of groups: %u (max: %d)\n",
			num_groups, MAX_TEMP_GROUPS);
		of_node_put(group_node);
		return -EINVAL;
	}

	data->num_groups = num_groups;

	i = 0;
	for_each_child_of_node(group_node, group_node) {
		struct temp_sensor_group *group;
		u32 group_id, num_ch, reg_base;
		void __iomem *group_base = NULL;

		ret = of_property_read_u32(group_node, "group-id", &group_id);
		if (ret < 0 || group_id >= num_groups) {
			dev_err(dev, "Invalid or missing group-id\n");
			of_node_put(group_node);
			return -EINVAL;
		}

		group = &data->groups[group_id];

		ret = of_property_read_u32(group_node, "num-channels", &num_ch);
		if (ret < 0) {
			dev_err(dev, "Missing num-channels for group %u\n", group_id);
			of_node_put(group_node);
			return -EINVAL;
		}

		ret = of_property_read_u32(group_node, "reg-base-offset", &reg_base);
		if (ret < 0) {
			dev_err(dev, "Missing reg-base-offset for group %u\n", group_id);
			of_node_put(group_node);
			return -EINVAL;
		}

		ret = of_property_read_u32(group_node, "start-bit", &group->start_bit);
		if (ret < 0)
			group->start_bit = group_id;

		{
			struct resource res;
			ret = of_address_to_resource(group_node, 0, &res);
			if (!ret) {
				group_base = devm_ioremap_resource(dev, &res);
				if (IS_ERR(group_base))
					group_base = NULL;
			}
		}

		group->base = group_base;
		group->reg_base_offset = reg_base;
		group->start_channel = 0;
		group->num_channels = num_ch;

		for (j = 0; j < num_ch; j++) {
			int sensor_ch = channel + j;
			if (sensor_ch >= MAX_TEMP_SENSORS) {
				dev_warn(dev, "Sensor channel %d exceeds max, skipping\n", sensor_ch);
				continue;
			}
			data->sensor_group[sensor_ch] = group_id;
			data->sensor_index_in_group[sensor_ch] = j;
		}

		channel += num_ch;
		num_sensors += num_ch;
	}

	of_node_put(group_node);

	data->num_sensors = num_sensors;

	dev_info(dev, "Parsed %d groups, %d total sensors\n",
		 data->num_groups, data->num_sensors);

	return 0;
}

static int ambarella_ts_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ambarella_ts_data *data;
	struct device *hwmon_dev;
	const struct soc_device_attribute *soc_dev_attr;
	const struct ambarella_ts_platform_data *pdata = NULL;
	int ret;

	soc_dev_attr = soc_device_match(ambarella_ts_socinfo);
	if (soc_dev_attr && soc_dev_attr->data)
		pdata = soc_dev_attr->data;
	else
		pdata = &amba_ts_pdata0;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_init(&data->lock);
	data->dev = dev;
	data->pdata = pdata;

	for (ret = 0; ret < MAX_TEMP_SENSORS; ret++) {
		data->sensor_group[ret] = -1;
		data->sensor_index_in_group[ret] = -1;
		data->cached_raw[ret] = 0;
	}

	for (ret = 0; ret < MAX_TEMP_GROUPS; ret++) {
		data->cache_timestamp[ret] = 0;
	}

	data->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->base)) {
		dev_err(dev, "Failed to get and ioremap I/O resource\n");
		return PTR_ERR(data->base);
	}

	data->scr_regmap = syscon_regmap_lookup_by_phandle_args(dev->of_node, "amb,scr-regmap",
				1, &data->boot_sts_offset);
	if (IS_ERR(data->scr_regmap)) {
		dev_err(dev, "scr-regmap lookup failed.\n");
		return PTR_ERR(data->scr_regmap);
	}

	if (pdata->otp_area_use != 0) {
		data->otp_regmap = syscon_regmap_lookup_by_phandle(dev->of_node, "amb,otp-regmap");
		if (IS_ERR(data->otp_regmap)) {
			dev_err(dev, "otp-regmap lookup failed.\n");
			return PTR_ERR(data->otp_regmap);
		}
	} else {
		data->otp_regmap = NULL;
	}

	ret = ambarella_ts_parse_groups(dev, data);
	if (ret < 0) {
		dev_err(dev, "Failed to parse sensor groups\n");
		return ret;
	}

	if (data->num_sensors < 1 || data->num_sensors > MAX_TEMP_SENSORS) {
		dev_err(dev, "Invalid sensor count: %d\n", data->num_sensors);
		return -EINVAL;
	}

	dev_info(dev, "Number of temperature sensors: %d (in %d groups)\n",
		 data->num_sensors, data->num_groups);

	platform_set_drvdata(pdev, data);

	ret = ambarella_ts_get_otp_cal(dev, data);
	if (ret < 0) {
		dev_warn(dev, "Failed to get OTP calibration values, using defaults\n");
	}

	ret = ambarella_ts_init_channel_info(data);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize channel info\n");
		return ret;
	}

	ret = ambarella_ts_init_sensor(data);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize temperature sensor\n");
		ambarella_ts_free_channel_info();
		return ret;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, DRIVER_NAME,
							 data,
							 ambarella_ts_get_chip_info(data),
							 NULL);
	if (IS_ERR(hwmon_dev)) {
		dev_err(dev, "Failed to register hwmon device\n");
		ambarella_ts_free_channel_info();
		return PTR_ERR(hwmon_dev);
	}

	data->hwmon_dev = hwmon_dev;

	ret = ambarella_ts_create_temp_attrs(hwmon_dev, data);
	if (ret < 0) {
		dev_err(dev, "Failed to create temperature attributes\n");
		ambarella_ts_free_channel_info();
		return ret;
	}

	dev_info(dev, "Ambarella temperature sensor registered with %d sensors\n",
		 data->num_sensors);

	return 0;
}

static int ambarella_ts_remove(struct platform_device *pdev)
{
	ambarella_ts_free_channel_info();
	return 0;
}

static const struct of_device_id ambarella_ts_of_match[] = {
	{ .compatible = "ambarella,temp-sensor" },
	{ },
};
MODULE_DEVICE_TABLE(of, ambarella_ts_of_match);

static struct platform_driver ambarella_ts_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ambarella_ts_of_match,
	},
	.probe = ambarella_ts_probe,
	.remove = ambarella_ts_remove,
};

module_platform_driver(ambarella_ts_driver);

MODULE_AUTHOR("Ambarella");
MODULE_DESCRIPTION("Ambarella Temperature Sensor Driver");
MODULE_LICENSE("GPL v2");

