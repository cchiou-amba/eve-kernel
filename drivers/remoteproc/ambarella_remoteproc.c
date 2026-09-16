// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 Ambarella International LP
 *
 */

#include "linux/delay.h"
#include "linux/sizes.h"
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/of_device.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/remoteproc.h>
#include "remoteproc_internal.h"

#define CM3_SYS_SW_RST_L		BIT(0)

#define SRAM_SIZE				SZ_32K

struct ambarella_rproc {
	struct device			*dev;
	struct rproc			*rproc;
	struct clk				*clk;
	struct regmap			*scr_regmap;
	struct regmap			*axi_regmap;
	struct regmap			*nscr_regmap;
	int 					chan[2];
	void __iomem			*rsc_table;
	void __iomem			*boot_addr;
};

struct ambarella_rproc_data {
	unsigned int			bootaddr_h_off;
	unsigned int			bootaddr_l_off;
	unsigned int			release_off;
	unsigned int			dram_start_off;
	unsigned int			dram_remap_h_off;
	unsigned int			dram_remap_l_off;
	unsigned int			dram_remap_sz_off;
	unsigned int			ahbsp_data3_off;
};

const struct ambarella_rproc_data cv72_data = {
	.bootaddr_h_off			= 0xD4,
	.bootaddr_l_off			= 0xD8,
	.release_off			= 0x24,
	.dram_start_off			= 0xDC,
	.dram_remap_h_off		= 0xE0,
	.dram_remap_l_off		= 0xE4,
	.dram_remap_sz_off		= 0xE8,
	.ahbsp_data3_off		= 0x1078,
};

const struct ambarella_rproc_data cv75_data = {
	.bootaddr_h_off			= 0x174,
	.bootaddr_l_off			= 0x178,
	.release_off			= 0x170,
	.dram_start_off			= 0x17C,
	.dram_remap_h_off		= 0x180,
	.dram_remap_l_off		= 0x184,
	.dram_remap_sz_off		= 0x188,
	.ahbsp_data3_off		= 0x1078,
};

static int ambarella_rproc_start(struct rproc *rproc)
{
	struct ambarella_rproc *priv = rproc->priv;
	const struct ambarella_rproc_data *data = of_device_get_match_data(priv->dev);
	int ret;

	ret = regmap_update_bits(priv->axi_regmap, data->release_off, CM3_SYS_SW_RST_L, (unsigned int)CM3_SYS_SW_RST_L);
	if (ret)
		dev_err(priv->dev, "Failed to enable remote core!\n");

	/* Make sure remote rpmsg task is scheduled */
	msleep(10);

	return ret;
}

static int ambarella_rproc_stop(struct rproc *rproc)
{
	struct ambarella_rproc *priv = rproc->priv;
	const struct ambarella_rproc_data *data = of_device_get_match_data(priv->dev);
	int ret;

	ret = regmap_update_bits(priv->axi_regmap, data->release_off, CM3_SYS_SW_RST_L, ~(unsigned int)CM3_SYS_SW_RST_L);
	if (ret)
		dev_err(priv->dev, "Failed to stop remote core!\n");

	return 0;
}

static int ambarella_rproc_mem_alloc(struct rproc *rproc,
			       struct rproc_mem_entry *mem)
{
	struct device *dev = rproc->dev.parent;
	void *va;

	dev_dbg(dev, "map memory: %p+%zx\n", &mem->dma, mem->len);
	va = ioremap_wc(mem->dma, mem->len);
	if (IS_ERR_OR_NULL(va)) {
		dev_err(dev, "Unable to map memory region: %p+%zx\n",
			&mem->dma, mem->len);
		return -ENOMEM;
	}

	/* Update memory entry va */
	mem->va = va;

	return 0;
}

static int ambarella_rproc_mem_release(struct rproc *rproc,
				 struct rproc_mem_entry *mem)
{
	dev_dbg(rproc->dev.parent, "unmap memory: %pa\n", &mem->dma);
	iounmap(mem->va);

	return 0;
}

static int ambarella_rproc_prepare(struct rproc *rproc)
{
	struct ambarella_rproc *priv = rproc->priv;
	struct device_node *np = priv->dev->of_node;
	struct of_phandle_iterator it;
	struct rproc_mem_entry *mem;
	struct reserved_mem *rmem;
	u32 da;

	/* Register associated reserved memory regions */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		if (strcmp(it.node->name, "vdev0vring0") && strcmp(it.node->name, "vdev0vring1")) {
			continue;
		}

		rmem = of_reserved_mem_lookup(it.node);
		if (!rmem) {
			of_node_put(it.node);
			dev_err(priv->dev, "unable to acquire memory-region\n");
			return -EINVAL;
		}

		da = rmem->base;

		/* Register memory region */
		mem = rproc_mem_entry_init(priv->dev, NULL, (dma_addr_t)rmem->base, rmem->size, da,
					   ambarella_rproc_mem_alloc, ambarella_rproc_mem_release,
					   it.node->name);

		if (mem) {
			rproc_coredump_add_segment(rproc, da, rmem->size);
		} else {
			of_node_put(it.node);
			return -ENOMEM;
		}

		rproc_add_carveout(rproc, mem);
	}

	return  0;
}

static void ambarella_rproc_kick(struct rproc *rproc, int vqid)
{
	struct ambarella_rproc *priv = rproc->priv;

	if (vqid == 0) {
		regmap_write(priv->scr_regmap, 0x8, 0x9);
	} else if (vqid == 1) {
		regmap_write(priv->scr_regmap, 0x8, 0xa);
	}
}

static int ambarella_rproc_attach(struct rproc *rproc)
{
	return 0;
}

static struct resource_table *ambarella_find_loaded_rsc_table(struct rproc *rproc, const struct firmware *fw)
{
	struct ambarella_rproc *priv = rproc->priv;

	if (!priv->rsc_table)
		return NULL;

	return (struct resource_table *)priv->rsc_table;
}

static struct resource_table *ambarella_get_loaded_rsc_table(struct rproc *rproc, size_t *table_sz)
{
	struct ambarella_rproc *priv = rproc->priv;

	/* The resource table has already been mapped in ambarella_rproc_probe() */
	if (!priv->rsc_table)
		return NULL;

	*table_sz = SZ_1K;

	return (struct resource_table *)priv->rsc_table;
}

static void *ambarella_rproc_da_to_va(struct rproc *rproc, u64 da, size_t len, bool *is_iomem)
{
	struct ambarella_rproc *priv = rproc->priv;

	if (is_iomem) *is_iomem = true;

	return (void *)(da + (u64)priv->boot_addr);
}

static const struct rproc_ops ambarella_rproc_ops = {
	.prepare	= ambarella_rproc_prepare,
	.attach		= ambarella_rproc_attach,
	.start		= ambarella_rproc_start,
	.stop		= ambarella_rproc_stop,
	.kick		= ambarella_rproc_kick,
	.da_to_va	= ambarella_rproc_da_to_va,
	.load		= rproc_elf_load_segments,
	.parse_fw	= rproc_elf_load_rsc_table,
	.find_loaded_rsc_table	= ambarella_find_loaded_rsc_table,
	.get_loaded_rsc_table	= ambarella_get_loaded_rsc_table,
	.sanity_check	= rproc_elf_sanity_check,
	.get_boot_addr	= rproc_elf_get_boot_addr,
};

static irqreturn_t ambarella_vq_interrupt(int irq, void *data)
{
	struct rproc *rproc = data;
	struct ambarella_rproc *priv = rproc->priv;
	int notifyid = (irq == priv->chan[0] ? 0 : 1);

	return rproc_vq_interrupt(rproc, notifyid);
}

static int ambarella_rproc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct of_phandle_iterator it;
	struct resource res;
	struct ambarella_rproc *priv;
	struct rproc *rproc;
	const struct ambarella_rproc_data *data;
	unsigned int args[4];
	int ret;

	data = of_device_get_match_data(dev);
	if (!data)
		return -ENODEV;

	rproc = rproc_alloc(dev, "ambarella-rproc", &ambarella_rproc_ops,
			    NULL, sizeof(*priv));
	if (!rproc)
		return -ENOMEM;

	priv = rproc->priv;
	priv->rproc = rproc;
	priv->dev = dev;

	dev_set_drvdata(dev, rproc);

	/* Resource table */
	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get clock\n");
		return PTR_ERR(priv->clk);
	}

	/* Resource table */
	of_phandle_iterator_init(&it, np, "memory-region", NULL, 0);
	while (of_phandle_iterator_next(&it) == 0) {
		if (!strcmp(it.node->name, "rsc-table")) {
			ret = of_address_to_resource(it.node, 0, &res);
			if (ret) {
				dev_err(dev, "unable to resolve resource table\n");
				goto err_put_rproc;
			}

			priv->rsc_table = devm_ioremap(&pdev->dev, res.start, resource_size(&res));
			if (!priv->rsc_table) {
				dev_err(dev, "failed to remap %pr\n", &res);
				goto err_put_rproc;
			}

			break;
		}
	}

	priv->scr_regmap = syscon_regmap_lookup_by_phandle(np, "amb,scr-regmap");
	if (IS_ERR(priv->scr_regmap)) {
		dev_err(dev, "failed to map scratchpad\n");
		return PTR_ERR(priv->scr_regmap);
	}

	priv->axi_regmap = syscon_regmap_lookup_by_phandle_args(np, "amb,axi-regmap", 4, args);
	if (IS_ERR(priv->axi_regmap)) {
		dev_err(dev, "failed to map AXI\n");
		return PTR_ERR(priv->axi_regmap);
	}
	regmap_write(priv->axi_regmap, data->dram_start_off, args[0]);
	regmap_write(priv->axi_regmap, data->dram_remap_h_off, args[1]);
	regmap_write(priv->axi_regmap, data->dram_remap_l_off, args[2]);
	regmap_write(priv->axi_regmap, data->dram_remap_sz_off, args[3]);

	if (regmap_test_bits(priv->axi_regmap, data->release_off, (unsigned int)CM3_SYS_SW_RST_L) > 0)
		priv->rproc->state = RPROC_DETACHED;

	priv->nscr_regmap = syscon_regmap_lookup_by_phandle(np, "amb,nscr-regmap");
	if (IS_ERR(priv->nscr_regmap)) {
		dev_err(dev, "failed to map non-secure scratchpad\n");
		return PTR_ERR(priv->nscr_regmap);
	}
	regmap_write(priv->nscr_regmap, data->ahbsp_data3_off, clk_get_rate(priv->clk));

	priv->boot_addr = syscon_regmap_lookup_by_phandle(np, "amb,sram");
	if (IS_ERR(priv->boot_addr)) {
		dev_err(dev, "failed to map SRAM\n");
		return PTR_ERR(priv->boot_addr);
	}

	priv->chan[0] = platform_get_irq_byname_optional(pdev, "vq0");
	if (priv->chan[0]) {
		ret = devm_request_threaded_irq(dev, priv->chan[0],
						NULL,
						ambarella_vq_interrupt, IRQF_ONESHOT,
						"vq0", rproc);

		if (ret) {
			dev_err(dev, "couldn't register vq0 handler\n");
		}
	}

	priv->chan[1] = platform_get_irq_byname_optional(pdev, "vq1");
	if (priv->chan[1]) {
		ret = devm_request_threaded_irq(dev, priv->chan[1],
						NULL,
						ambarella_vq_interrupt, IRQF_ONESHOT,
						"vq1", rproc);

		if (ret) {
			dev_err(dev, "couldn't register vq1 handler\n");
		}
	}

	if (priv->rproc->state != RPROC_DETACHED)
		priv->rproc->auto_boot = of_property_read_bool(np, "amb,auto-boot");

	ret = rproc_add(rproc);
	if (ret) {
		dev_err(dev, "rproc_add failed\n");
		goto err_put_rproc;
	}

	return 0;

err_put_rproc:
	rproc_free(rproc);

	return ret;
}

static int ambarella_rproc_remove(struct platform_device *pdev)
{
	struct rproc *rproc = platform_get_drvdata(pdev);

	rproc_del(rproc);
	rproc_free(rproc);

	return 0;
}

static const struct of_device_id ambarella_rproc_of_match[] = {
	{ .compatible = "ambarella,cv72-cm3", .data = &cv72_data },
	{ .compatible = "ambarella,cv75-cm3", .data = &cv75_data },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_rproc_of_match);

static struct platform_driver ambarella_rproc_driver = {
	.probe = ambarella_rproc_probe,
	.remove = ambarella_rproc_remove,
	.driver = {
		.name = "ambarella-rproc",
		.of_match_table = ambarella_rproc_of_match,
	},
};

module_platform_driver(ambarella_rproc_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Ambarella remote processor control driver");
MODULE_AUTHOR("Sheng-Shi Ding <ssding@ambarella.com>");
