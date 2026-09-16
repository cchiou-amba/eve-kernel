// SPDX-License-Identifier: GPL-2.0
/*
 * pci-ambarella - PCIe controller driver for Ambarella SoCs
 *
 * Copyright (C) 2022 by Ambarella, Inc.
 * Author: Li Chen <lchen@ambarella.com>
 */

#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/pm_runtime.h>
#include <linux/of_device.h>
#include <linux/sys_soc.h>
#include <linux/regmap.h>
#include <linux/of_reserved_mem.h>
#include <linux/irqdomain.h>
#include "pcie-cadence.h"

#define LINK_STATUS_MASK_V1		GENMASK(3, 2)
#define LINK_STATUS_MASK_V2		GENMASK(1, 0)

struct ambarella_cdns_pcie {
	struct cdns_pcie *pcie;
	bool is_rc;
	struct regmap *regmap;
	u32 link_status_offset;
	unsigned long link_status_mask;
	u64 fixup_cpu_addr_offset;
	void *cdns_pcie_rc;
	void *cdns_pcie_ep;
};

static u64 ambarella_cdns_cpu_addr_fixup(struct cdns_pcie *pcie, u64 cpu_addr)
{
	struct ambarella_cdns_pcie *ambarella_pcie = dev_get_drvdata(pcie->dev);

	return cpu_addr - ambarella_pcie->fixup_cpu_addr_offset;
}

static bool ambarella_cdns_pcie_link_up(struct cdns_pcie *pcie)
{
	struct ambarella_cdns_pcie *ambarella_pcie = dev_get_drvdata(pcie->dev);

	/*
	 * pciec_link_status, Status of the PCI Express link
	 * 0b00 = No receivers detected
	 * 0b01 = Link training in progress
	 * 0b10 = Link up, DL initialization in progress
	 * 0b11 = linkup, DL initialization completed
	 */
	return regmap_test_bits(ambarella_pcie->regmap,
				ambarella_pcie->link_status_offset,
				ambarella_pcie->link_status_mask) > 0;
}

static const struct cdns_pcie_ops ambarella_cdns_ops = {
	.cpu_addr_fixup = ambarella_cdns_cpu_addr_fixup,
	.link_up = ambarella_cdns_pcie_link_up,
};

static const struct soc_device_attribute ambarella_soc_info[] = {
	{ .soc_id = "cv5", .data = (void *)LINK_STATUS_MASK_V1 },
	{ .soc_id = "n1", .data = (void *)LINK_STATUS_MASK_V1 },
	{ .soc_id = "cv72", .data = (void *)LINK_STATUS_MASK_V1 },
	{ /* sentinel */ }
};

static struct platform_device *ambarella_cdns_create_udma(struct platform_device *pdev)
{
	struct platform_device_info pdevinfo;
	struct resource res[2], *__res;

	__res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "udma");
	if (__res)
		memcpy(&res[0], __res, sizeof(*__res));
	else
		dev_warn(&pdev->dev, "udma can't get mem resource \n");

	__res = platform_get_resource_byname(pdev, IORESOURCE_IRQ, "udma");
	if (__res)
		memcpy(&res[1], __res, sizeof(*__res));
	else {
		memset(&res[1], 0, sizeof(res[1]));
		res[1].start = platform_get_irq_byname(pdev,"udma");
		res[1].end = res[1].start;
		res[1].name = "udma";
		res[1].flags = IORESOURCE_IRQ;
	}

	/* Set up platform device info */
	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = &pdev->dev;
	pdevinfo.name = "cdns_udma";
	pdevinfo.id = PLATFORM_DEVID_AUTO;
	pdevinfo.res = res;
	pdevinfo.num_res = ARRAY_SIZE(res);
	pdevinfo.dma_mask = pdev->platform_dma_mask;

	return platform_device_register_full(&pdevinfo);
}

static int ambarella_cdns_pcie_probe(struct platform_device *pdev)
{
	const struct soc_device_attribute *soc;
	struct ambarella_cdns_pcie *ambarella_pcie;
	struct pci_host_bridge *bridge;
	struct cdns_pcie_ep *ep;
	struct cdns_pcie_rc *rc;
	struct device *dev = &pdev->dev;
	int phy_count;
	bool is_rc;
	int ret;

	soc = soc_device_match(ambarella_soc_info);
	if (!soc || !soc->data) {
		dev_err(&pdev->dev, "Unknown SoC!\n");
		return -ENODEV;
	}

	ambarella_pcie = devm_kzalloc(dev, sizeof(*ambarella_pcie), GFP_KERNEL);
	if (!ambarella_pcie)
		return -ENOMEM;

	ambarella_pcie->regmap = syscon_regmap_lookup_by_phandle_args(
					dev->of_node, "amb,scr-regmap", 1,
					&ambarella_pcie->link_status_offset);
	if (IS_ERR(ambarella_pcie->regmap)) {
		dev_err(dev, "regmap lookup failed.\n");
		return PTR_ERR(ambarella_pcie->regmap);
	}

	ambarella_pcie->link_status_mask = (unsigned long)soc->data;

	of_property_read_u64(dev->of_node, "amb,fixup-cpu-addr-offset",
					&ambarella_pcie->fixup_cpu_addr_offset);

	platform_set_drvdata(pdev, ambarella_pcie);

	is_rc = of_device_is_compatible(dev->of_node, "ambarella,cdns-pcie-host");
	if (is_rc) {
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_HOST))
			return -ENODEV;

		bridge = devm_pci_alloc_host_bridge(dev, sizeof(*rc));
		if (!bridge)
			return -ENOMEM;

		rc = pci_host_bridge_priv(bridge);
		rc->pcie.dev = dev;
		rc->pcie.ops = &ambarella_cdns_ops;
		ambarella_pcie->pcie = &rc->pcie;
		ambarella_pcie->is_rc = is_rc;
		ambarella_pcie->cdns_pcie_rc = rc;

		ret = cdns_pcie_init_phy(dev, ambarella_pcie->pcie);
		if (ret) {
			dev_err(dev, "failed to init phy, errno: %d\n", ret);
			return ret;
		}

		pm_runtime_enable(dev);
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "pm_runtime_get_sync() failed\n");
			goto err_get_sync;
		}

		ret = cdns_pcie_host_setup(rc);
		if (ret)
			goto err_init;
	} else {
		if (!IS_ENABLED(CONFIG_PCIE_CADENCE_EP))
			return -ENODEV;

		ep = devm_kzalloc(dev, sizeof(*ep), GFP_KERNEL);
		if (!ep)
			return -ENOMEM;

		ep->pcie.dev = dev;
		ep->pcie.ops = &ambarella_cdns_ops;
		ambarella_pcie->pcie = &ep->pcie;
		ambarella_pcie->is_rc = is_rc;
		ambarella_pcie->cdns_pcie_ep = ep;

		ret = cdns_pcie_init_phy(dev, ambarella_pcie->pcie);
		if (ret) {
			dev_err(dev, "failed to init phy\n");
			return ret;
		}

		pm_runtime_enable(dev);
		ret = pm_runtime_get_sync(dev);
		if (ret < 0) {
			dev_err(dev, "pm_runtime_get_sync() failed\n");
			goto err_get_sync;
		}

#if 0	/* TODO */
		/*
		 * If we want bar to bind large phy continuous
		 * memory(see pci_epf_alloc_space in pci-epf-core.c)
		 * like 64MB, we need reserve memory at early boot time.
		 * I use "memory-region = <&reservedBar>;" in PCIe
		 * controller's dts to do this, so of_reserved_mem_device_init is needed.
		 */
		if (of_reserved_mem_device_init(dev))
			dev_warn(dev,
				"device failed to get specific reserved mem pool, bar allocation may fail\n");

#endif
		ret = cdns_pcie_ep_setup(ep);
		if (ret)
			goto err_init;

		/* The reset value of BAR0 and BAR1 are non-zero. Host will allocate resource for BAR1 even though
		 * BAR1 is not used. To avoid this, set BAR0 and BAR1 to 0 here.
		 */
		ret = cdns_pcie_readl(ambarella_pcie->pcie, CDNS_PCIE_LM_EP_FUNC_BAR_CFG(0, 0));
		if (ret) {
			dev_dbg(dev, "BAR0/3 has non-zero value: 0x%08x\n", ret);
			cdns_pcie_writel(ambarella_pcie->pcie, CDNS_PCIE_LM_EP_FUNC_BAR_CFG(0, 0), 0);
		}
	}

	if (is_rc) /* cleanup spurious pme for rc */
		writel(0, ambarella_pcie->pcie->reg_base + CDNS_PCIE_RP_ROOT_STATUS);

	ambarella_cdns_create_udma(pdev);

	dev_info(dev, " Started %s with %s\n", __func__, is_rc ? "RC" : "EP");

	return 0;

err_init:
	pm_runtime_put_sync(dev);
err_get_sync:
	pm_runtime_disable(dev);
	cdns_pcie_disable_phy(ambarella_pcie->pcie);
	phy_count = ambarella_pcie->pcie->phy_count;
	while (phy_count--)
		device_link_del(ambarella_pcie->pcie->link[phy_count]);

	return 0;
}

static void ambarella_cdns_pcie_shutdown(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ambarella_cdns_pcie *ambarella_pcie = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_put_sync(dev);
	if (ret < 0)
		dev_dbg(dev, "pm_runtime_put_sync failed\n");

	pm_runtime_disable(dev);

	cdns_pcie_disable_phy(ambarella_pcie->pcie);
}

static const struct of_device_id ambarella_cdns_pcie_of_match[] = {
	{
		.compatible = "ambarella,cdns-pcie-host",
	},
	{
		.compatible = "ambarella,cdns-pcie-ep",
	},
	{},
};

#ifdef CONFIG_PM_SLEEP
extern int ambarella_pcie_host_init(struct device *dev,
		struct cdns_pcie_rc *rc);

static int ambarella_cdns_pcie_suspend_noirq(struct device *dev)
{
	struct ambarella_cdns_pcie *amba_pcie = dev_get_drvdata(dev);

	cdns_pcie_disable_phy(amba_pcie->pcie);

	return 0;
}

static int ambarella_cdns_pcie_resume_noirq(struct device *dev)
{
	int ret;

	enum cdns_pcie_rp_bar bar;
	struct cdns_pcie_rc *rc = NULL;
	struct cdns_pcie_ep *ep = NULL;
	struct ambarella_cdns_pcie *amba_pcie = NULL;

	amba_pcie = dev_get_drvdata(dev);
	ret = cdns_pcie_enable_phy(amba_pcie->pcie);
	if (ret) {
		dev_err(dev, "failed to enable phy\n");
		return ret;
	}

	if (amba_pcie->is_rc) {
		rc = amba_pcie->cdns_pcie_rc;
		for (bar = RP_BAR0; bar <= RP_NO_BAR; bar++)
			rc->avail_ib_bar[bar] = true;

		if (rc->quirk_detect_quiet_flag)
			cdns_pcie_detect_quiet_min_delay_set(amba_pcie->pcie);

		ret = cdns_pcie_start_link(amba_pcie->pcie);
		if (ret) {
			dev_err(dev, "Failed to start link\n");
			return ret;
		}
		ret = ambarella_pcie_host_init(dev, rc);
		if (ret) {
			dev_err(dev, "cdns_pcie_host_init failed \n");
			return ret;
		}
	} else {
		ep = amba_pcie->cdns_pcie_ep;
		/* Disable all but function 0 (anyway BIT(0) is hardwired to 1). */
		cdns_pcie_writel(amba_pcie->pcie, CDNS_PCIE_LM_EP_FUNC_CFG, BIT(0));
		if (ep->quirk_detect_quiet_flag)
			cdns_pcie_detect_quiet_min_delay_set(&ep->pcie);
	}

	return 0;
}

const struct dev_pm_ops ambarella_cdns_pcie_pcie_pm_ops = {
	SET_NOIRQ_SYSTEM_SLEEP_PM_OPS(ambarella_cdns_pcie_suspend_noirq,
				      ambarella_cdns_pcie_resume_noirq)
};
#endif

static struct platform_driver ambarella_cdns_pcie_driver = {
	.driver = {
		.name = "ambarella-cdns-pcie",
		.of_match_table = ambarella_cdns_pcie_of_match,
		.pm	= &cdns_pcie_pm_ops,
#ifdef CONFIG_PM_SLEEP
		.pm	= &ambarella_cdns_pcie_pcie_pm_ops,
#endif
	},
	.probe = ambarella_cdns_pcie_probe,
	.shutdown = ambarella_cdns_pcie_shutdown,
};
builtin_platform_driver(ambarella_cdns_pcie_driver);
