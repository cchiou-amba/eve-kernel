// SPDX-License-Identifier: GPL-2.0
/**
 * cdns3-ambarella.c - AMBARELLA Specific Glue layer for Cadence USB Controller
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

#include <linux/bits.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/io.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>
#include <linux/iopoll.h>
#include <linux/pm_runtime.h>
#include <linux/sys_soc.h>
#include "core.h"


#define USB32C_CTRL_OFFSET			0x16c
#define USB32C_RESET_MASK			(0x1)
#define USB32C_MODE_STRAP_MASK 		(0x6)
#define USB32C_MODE_STRAP_SHIFT		(1)
#define USB32C_OTGSESS_VALID_MASK	BIT(3)

#define USBC_CTRL_OFFSET			0x12c
#define USBC_HOST_OCP_MASK			BIT(1)
#define USBC_HOST_OCP_SHIFT			(1)

#define USB_SIDEBAND_REG_OFFSET		0x94

#define USBP_TXPREEMPAMP_MASK		GENMASK(13, 12)
#define USBP_TXRISETUNE0_MASK		GENMASK(21, 20)
#define USBP_TXVREFTUNE0_MASK		GENMASK(25, 22)

#define USBP_TXPREEMPAMP_SHIFT		(12)
#define USBP_TXRISETUNE0_SHIFT		(20)
#define USBP_TXVREFTUNE0_SHIFT		(22)


/* Modestrap modes */
enum modestrap_mode {
	MODE_STRAP_MODE_NONE,
	MODE_STRAP_MODE_HOST,
	MODE_STRAP_MODE_PERIPHERAL
};

static unsigned int mode_strap = MODE_STRAP_MODE_HOST;
module_param(mode_strap, uint, 0644);
MODULE_PARM_DESC(mode_strap, "mode_strap.");

struct cdns_ambarella {
	struct device *dev;
	//void __iomem *usbss;
	struct regmap	*scr_reg;
	struct platform_device *cdns3_pdev;
	int hub_rst_pin;
	int rst_active;
	int hub_pwr_pin;
	int pwr_active;
	u32 ovrcur_pol_inv;
	u32 usb_connect_mask;
	spinlock_t  lock;
	u32 usbp_ctrl_offset;
	u32 usbp_ctrl2_offset;
	u32 usbp_tx_tune[3];
};

static const struct soc_device_attribute ambarella_cdnsp_socinfo[] = {
	{ .soc_id = "cv72" },
	{ .soc_id = "cv75" },
	{ .soc_id = "cv3ad685" },
	{ .soc_id = "cv3ad655" },
	{/* sentinel */}
};

static const struct soc_device_attribute ambarella_cdnsp_connectinfo[] = {
	{ .soc_id = "cv5" },
	{ .soc_id = "cv75" },
	{/* sentinel */}
};

static int cdns_ambarella_platform_suspend(struct device *dev,
	bool suspend, bool wakeup)
{
	/* TODO */
	return 0;
}

static irqreturn_t usb32_connect_chg_irq_handler(int irq, void *devid)
{
	struct cdns_ambarella *data = devid;
	unsigned long	flags;
	u32 val;

	spin_lock_irqsave(&data->lock, flags);
	regmap_read(data->scr_reg, USB_SIDEBAND_REG_OFFSET, &val);
	if(val & data->usb_connect_mask)
		regmap_update_bits(data->scr_reg, USB32C_CTRL_OFFSET,
			USB32C_OTGSESS_VALID_MASK, USB32C_OTGSESS_VALID_MASK);
	else
		regmap_clear_bits(data->scr_reg, USB32C_CTRL_OFFSET, USB32C_OTGSESS_VALID_MASK);

	spin_unlock_irqrestore(&data->lock, flags);

	return IRQ_HANDLED;
}

static int cdns_ambarella_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *node = dev->of_node;
	struct of_dev_auxdata *cdns_ambarella_auxdata;
	struct cdns3_platform_data *cdns_ambarella_pdata;
	struct cdns_ambarella *data;
	u32 flags;
	struct of_phandle_args gpiospec;
	u32 ovrcur_pol, regs[2], val;
	int ret, usb32_connect_chg_irq;

	if (!node)
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	platform_set_drvdata(pdev, data);
	data->dev = dev;

#if 0
	data->usbss = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(data->usbss)) {
		dev_err(dev, "can't map IOMEM resource\n");
		return PTR_ERR(data->usbss);
	}
#endif
	data->scr_reg = syscon_regmap_lookup_by_phandle_args(node, "amb,scr-regmap", 2, regs);
	if (IS_ERR(data->scr_reg)) {
		dev_err(dev, "no scr regmap!\n");
		return PTR_ERR(data->scr_reg);
	}

	data->usbp_ctrl_offset = regs[0];
	data->usbp_ctrl2_offset = regs[1];

	/* Get USB HS PHY tunning setting if necessary */
	ret = of_property_read_u32(node, "amb,tx-preemphasis", &data->usbp_tx_tune[0]);
	if (ret < 0) {
		regmap_read(data->scr_reg, data->usbp_ctrl2_offset, &val);
		data->usbp_tx_tune[0] = (val & USBP_TXPREEMPAMP_MASK) >> USBP_TXPREEMPAMP_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl2_offset, USBP_TXPREEMPAMP_MASK,
						data->usbp_tx_tune[0] << USBP_TXPREEMPAMP_SHIFT);

	ret = of_property_read_u32(node, "amb,tx-risetune", &data->usbp_tx_tune[1]);
	if (ret < 0) {
		regmap_read(data->scr_reg, data->usbp_ctrl_offset, &val);
		data->usbp_tx_tune[1] = (val & USBP_TXRISETUNE0_MASK) >> USBP_TXRISETUNE0_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl_offset, USBP_TXRISETUNE0_MASK,
						data->usbp_tx_tune[1] << USBP_TXRISETUNE0_SHIFT);

	ret = of_property_read_u32(node, "amb,tx-vreftune", &data->usbp_tx_tune[2]);
	if (ret < 0) {
		regmap_read(data->scr_reg, data->usbp_ctrl_offset, &val);
		data->usbp_tx_tune[2] = (val & USBP_TXVREFTUNE0_MASK) >> USBP_TXVREFTUNE0_SHIFT;
	} else
		regmap_update_bits(data->scr_reg, data->usbp_ctrl_offset, USBP_TXVREFTUNE0_MASK,
						data->usbp_tx_tune[2] << USBP_TXVREFTUNE0_SHIFT);

	/* Set default mode (mode_strap) to be actived after power on reset */
	regmap_update_bits(data->scr_reg,
			USB32C_CTRL_OFFSET,
			USB32C_MODE_STRAP_MASK, mode_strap << USB32C_MODE_STRAP_SHIFT);

	if (mode_strap == MODE_STRAP_MODE_PERIPHERAL) {
		if (soc_device_match(ambarella_cdnsp_connectinfo))
			data->usb_connect_mask = BIT(25);
		else
			data->usb_connect_mask = BIT(24);

		regmap_read(data->scr_reg, USB_SIDEBAND_REG_OFFSET, &val);
		if((val & data->usb_connect_mask) == 0)
			regmap_clear_bits(data->scr_reg, USB32C_CTRL_OFFSET, USB32C_OTGSESS_VALID_MASK);
	}

	if (soc_device_match(ambarella_cdnsp_socinfo)) {
		/* Default OCP is low with bit1 = 1; while bit1 = 0, ocp high */
		ret = of_property_read_u32(node, "amb,ocp-polarity", &ovrcur_pol);
		if (ret < 0)
			ovrcur_pol = 0;
		data->ovrcur_pol_inv = !ovrcur_pol;
		regmap_update_bits(data->scr_reg,
			USBC_CTRL_OFFSET,
			USBC_HOST_OCP_MASK, data->ovrcur_pol_inv << USBC_HOST_OCP_SHIFT);
	}

	data->hub_pwr_pin = of_get_named_gpio(node, "hub-pwr-gpios", 0);
	ret = of_parse_phandle_with_args_map(node, "hub-pwr-gpios",
					     "gpio", 0, &gpiospec);
	if (ret) {
		pr_debug("%s: can't parse '%s' property of node '%pOF[%d]'\n",
			__func__, "hub-pwr-gpios", node, 0);
	}
	flags = gpiospec.args[1];
	data->pwr_active = !!(flags & 0x1);

	data->hub_rst_pin = of_get_named_gpio(node, "hub-rst-gpios", 0);
	ret = of_parse_phandle_with_args_map(node, "hub-rst-gpios",
					     "gpio", 0, &gpiospec);
	if (ret) {
		pr_debug("%s: can't parse '%s' property of node '%pOF[%d]'\n",
			__func__, "hub-rst-gpios", node, 0);
	}
	flags = gpiospec.args[1];
	data->rst_active = !!(flags & 0x1);

	/* request gpio for HUB power */
	if (gpio_is_valid(data->hub_pwr_pin)) {
		ret = devm_gpio_request(dev, data->hub_pwr_pin, "usb3 hub power");
		if (ret < 0) {
			dev_err(dev, "Failed to request hub power pin %d\n", ret);
			return ret;
		}
		gpio_direction_output(data->hub_pwr_pin, data->pwr_active);
		msleep(20);
	}

	/* request gpio for HUB reset */
	if (gpio_is_valid(data->hub_rst_pin)) {
		ret = devm_gpio_request(dev, data->hub_rst_pin, "usb3 hub reset");
		if (ret < 0) {
			dev_err(dev, "Failed to request hub reset pin %d\n", ret);
			return ret;
		}
		gpio_direction_output(data->hub_rst_pin, data->rst_active);
		msleep(20);
		gpio_direction_output(data->hub_rst_pin, !data->rst_active);
		msleep(20);
	}

	if (mode_strap == MODE_STRAP_MODE_PERIPHERAL) {
		spin_lock_init (&data->lock);

		usb32_connect_chg_irq = platform_get_irq_byname(pdev, "usb32_connect_chg");
		if (usb32_connect_chg_irq >= 0) {
			ret = devm_request_irq(dev, usb32_connect_chg_irq,
				usb32_connect_chg_irq_handler, IRQF_TRIGGER_RISING, dev_name(dev), data);
			if (ret) {
				dev_err(dev, "failed to request irq: %d\n", ret);
				ret = -ENODEV;
				goto err;
			}
		}
	}
	/* Zero out all data because of_dev_lookup will iterate over the array */
	cdns_ambarella_auxdata =
		devm_kmalloc_array(dev, 2, sizeof(*cdns_ambarella_auxdata), GFP_KERNEL | __GFP_ZERO);
	if (!cdns_ambarella_auxdata)
		return -ENOMEM;

	cdns_ambarella_auxdata[0].compatible = "cdns,usb3";

	/* pdata would be kfree when depopulate, so don't use resource management family */
	cdns_ambarella_pdata =
		kzalloc(sizeof(*cdns_ambarella_pdata), GFP_KERNEL);
	if (!cdns_ambarella_pdata)
		return -ENOMEM;

	cdns_ambarella_pdata->platform_suspend =
		cdns_ambarella_platform_suspend;
	cdns_ambarella_pdata->quirks = CDNS3_DEFAULT_PM_RUNTIME_ALLOW;
	cdns_ambarella_auxdata[0].platform_data = cdns_ambarella_pdata;

	ret = of_platform_populate(node, NULL, cdns_ambarella_auxdata, dev);
	if (ret) {
		dev_err(dev, "failed to create children: %d\n", ret);
		goto err;
	}

	device_set_wakeup_capable(dev, true);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	pm_runtime_set_autosuspend_delay(dev, 2000);
	pm_runtime_use_autosuspend(dev);

err:
	return ret;
}

static int cdns_ambarella_remove(struct platform_device *pdev)
{
	of_platform_depopulate(&pdev->dev);
	return 0;
}


static const struct of_device_id cdns_ambarella_of_match[] = {
	{ .compatible = "ambarella,cdns-usb3", },
	{},
};
MODULE_DEVICE_TABLE(of, cdns_ambarella_of_match);

static struct platform_driver cdns_ambarella_driver = {
	.probe		= cdns_ambarella_probe,
	.remove		= cdns_ambarella_remove,
	.driver		= {
		.name	= "cdns3-ambarella",
		.of_match_table	= cdns_ambarella_of_match,
	},
};
module_platform_driver(cdns_ambarella_driver);

MODULE_ALIAS("platform:cdns3-ambarella");
MODULE_AUTHOR("Ken He <jianhe@ambarella.com>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Cadence USB3 Ambarella Glue Layer");
