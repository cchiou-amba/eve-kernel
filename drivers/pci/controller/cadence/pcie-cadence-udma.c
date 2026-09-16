// SPDX-License-Identifier: GPL-2.0
/*
 * Platform driver for the Cadence uDMA Controller
 *
 * Copyright (C) 2022 Ambarella.Inc
 * Author: Li Chen <lchen@ambarella.com>
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/dmapool.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/of_platform.h>
#include <linux/bitfield.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/sys_soc.h>
#include <linux/regmap.h>
#include <linux/platform_device.h>
#include <linux/mfd/syscon.h>
#include <linux/debugfs.h>
#include "../../../dma/virt-dma.h"

#define CDNS_UDMA_CTRL_OFFSET(id)		(0x0 + 0x14 * id)
#define CDNS_UDMA_SP_L_OFFSET(id)		(0x4 + 0x14 * id)
#define CDNS_UDMA_SP_U_OFFSET(id)		(0x8 + 0x14 * id)
#define CDNS_UDMA_ATTR_L_OFFSET(id)		(0xc + 0x14 * id)
#define CDNS_UDMA_ATTR_U_OFFSET(id)		(0x10 + 0x14 * id)

#define CDNS_UDMA_INT_OFFSET			0xa0
#define CDNS_UDMA_INT_ENA_OFFSET		0xa4
#define CDNS_UDMA_INT_DIS_OFFSET		0xa8

#define CDNS_UDMA_IB_ECC_UNCO_OFFSET		0xac
#define CDNS_UDMA_IB_ECC_CORR_OFFSET		0xb0

#define CDNS_UDMA_OB_ECC_UNCO_OFFSET		0xb4
#define CDNS_UDMA_OB_ECC_CORR_OFFSET		0xb8

#define CDNS_UDMA_CAP_VER_OFFSET		0xf8
#define CDNS_UDMA_CONFIG_OFFSET			0xfc

#define CDNS_UDMA_NUM_CHANNELS_MASK		GENMASK(3, 0)
#define CDNS_UDMA_NUM_PARTITIONS_MASK		GENMASK(7, 4)
#define CDNS_UDMA_PARTITIONS_SIZE_MASK		GENMASK(11, 8)

#define CDNS_UDMA_BULK_MAX_SIZE			SZ_16M
#define CDNS_UDMA_MAX_CHANNELS			8

/*
 * CDNS uDMA needs the direction info, but mem2mem operation doesn't
 * have it. So we use memcpy_channel_dir as the direction info. In
 * other words, each channel has been specfied for IB or OB.
 * However, users can also call dmaengine_slave_config() to provide
 * the direction info and overwrite the direction info specified by
 * memcpy_channel_dir.
 */
static unsigned int memcpy_channel_dir = 0xaa;
module_param(memcpy_channel_dir, uint, 0644);
MODULE_PARM_DESC(memcpy_channel_dir,
		"The direction of channel for memcpy, bitwise, 0 - rx, 1 - tx.");

struct cdns_udma_lli {
	u32 sys_lo_addr; /* local-axi-addr */
	u32 sys_hi_addr;
	u32 sys_attr;

	u32 ext_lo_addr; /* ext-pci-bus-addr */
	u32 ext_hi_addr;
	u32 ext_attr;

	u32 length : 24;
	u32 interrupt : 1;
	u32 type : 2;
	u32 rsvd0 : 2;
	u32 continuity : 1;
	u32 rsvd1 : 2;

	u32 sys_status : 8;
	u32 ext_status : 8;
	u32 chan_status : 8;
	u32 rsvd2 : 8;

	u64 p_lli_next;

	/*
	 * This field is not used by the uDMA controller, but will be
	 * used by the CPU to go through the list (mostly for dumping
	 * or freeing it).
	 */
	struct cdns_udma_lli *v_lli_next;
};

struct cdns_udma_desc {
	struct virt_dma_desc vd;
	enum dma_transfer_direction dir;

	struct cdns_udma_lli *v_lli;
	dma_addr_t p_lli;
};

struct cdns_udma_chan {
	struct device *dev;
	struct virt_dma_chan vc;
	struct cdns_udma_dev *udma;
	struct cdns_udma_desc *desc; /* active desc */
	struct dma_slave_config config;
	u32 dir;
};

struct cdns_udma_dev {
	void __iomem *regbase;
	int irq;
	struct dma_device dma_dev;
	struct cdns_udma_chan *chan;
	struct dma_pool *lli_pool;
	u32 nr_channels;
	u32 align;

	struct dma_slave_map slave_map[CDNS_UDMA_MAX_CHANNELS];
};

static inline struct cdns_udma_chan *to_cdns_udma_chan(struct dma_chan *c)
{
	return container_of(c, struct cdns_udma_chan, vc.chan);
}

static inline struct cdns_udma_desc *to_cdns_udma_desc(struct virt_dma_desc *vd)
{
	return container_of(vd, struct cdns_udma_desc, vd);
}

static void cdns_udma_disable_irq(struct cdns_udma_chan *chan)
{
	struct cdns_udma_dev *udma = chan->udma;
	u32 val, chan_id = chan->vc.chan.chan_id;

	BUG_ON(chan_id >= udma->nr_channels);

	val = BIT(chan_id) | BIT(chan_id + CDNS_UDMA_MAX_CHANNELS);
	writel(val, udma->regbase + CDNS_UDMA_INT_DIS_OFFSET);
}

static void cdns_udma_enable_irq(struct cdns_udma_chan *chan)
{
	struct cdns_udma_dev *udma = chan->udma;
	u32 val, chan_id = chan->vc.chan.chan_id;

	BUG_ON(chan_id >= udma->nr_channels);

	val = BIT(chan_id) | BIT(chan_id + CDNS_UDMA_MAX_CHANNELS);
	writel(val, udma->regbase + CDNS_UDMA_INT_ENA_OFFSET);
}

static void cdns_udma_vchan_free_desc(struct virt_dma_desc *vd)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(vd->tx.chan);
	struct cdns_udma_desc *desc = to_cdns_udma_desc(vd);
	struct cdns_udma_lli *v_lli, *v_next;
	dma_addr_t p_lli, p_next;

	p_lli = desc->p_lli;
	v_lli = desc->v_lli;

	while (v_lli) {
		v_next = v_lli->v_lli_next;
		p_next = v_lli->p_lli_next;

		dma_pool_free(chan->udma->lli_pool, v_lli, p_lli);

		v_lli = v_next;
		p_lli = p_next;
	}

	kfree(desc);
}

static void cdns_udma_setup_lli(struct cdns_udma_lli *prev,
	struct cdns_udma_lli *curr, dma_addr_t curr_phys,
	dma_addr_t sys_addr, dma_addr_t ext_addr, u32 len)
{
	curr->sys_lo_addr = lower_32_bits(sys_addr);
	curr->sys_hi_addr = upper_32_bits(sys_addr);

	curr->ext_lo_addr = lower_32_bits(ext_addr);
	curr->ext_hi_addr = upper_32_bits(ext_addr);

	curr->length = (len == CDNS_UDMA_BULK_MAX_SIZE) ? 0 : len;
	curr->interrupt = 1;
	curr->continuity = 0;

#if 0
	/* lli is zeroed when allocate */
	curr->type = 0; /* bulk */
	curr->sys_attr = 0;
	curr->ext_attr = 0;
	curr->sys_status = 0;
	curr->ext_status = 0;
	curr->chan_status = 0;
#endif

	if (prev) {
		prev->interrupt = 0;
		prev->continuity = 1;
		prev->p_lli_next = curr_phys;
		prev->v_lli_next = curr;
	}
}

static struct dma_async_tx_descriptor *cdns_udma_prep_dma_memcpy(
		struct dma_chan *dma_chan, dma_addr_t dst, dma_addr_t src,
		size_t len, unsigned long flags)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(dma_chan);
	struct cdns_udma_dev *udma = chan->udma;
	struct cdns_udma_desc *desc;
	struct cdns_udma_lli *v_lli, *prev = NULL;
	dma_addr_t ext_addr, sys_addr, p_lli;

	if (unlikely((src % SZ_16) || (dst % SZ_16))) {
		dev_err(chan->dev, "addres is unligned to 16!\n");
		return NULL;
	}

	if (unlikely((len % udma->align) && len > udma->align)) {
		dev_err(chan->dev, "length is unligned to %d!\n", udma->align);
		return NULL;
	}

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	if (BIT(dma_chan->chan_id) & memcpy_channel_dir)
		desc->dir = DMA_MEM_TO_DEV;	/* Write/Outbound */
	else
		desc->dir = DMA_DEV_TO_MEM;	/* Read/Inbound */

	if (chan->dir)
		desc->dir = chan->dir;

	if (desc->dir == DMA_MEM_TO_DEV) {
		sys_addr = src;
		ext_addr = dst;
	} else {
		sys_addr = dst;
		ext_addr = src;
	}

	while (len > 0) {
		size_t xfer_len = min_t(size_t, len, CDNS_UDMA_BULK_MAX_SIZE);

		v_lli = dma_pool_zalloc(udma->lli_pool, GFP_NOWAIT, &p_lli);
		if (!v_lli)
			goto err_exit;

		if (prev == NULL) {
			desc->v_lli = v_lli;
			desc->p_lli = p_lli;
		}

		cdns_udma_setup_lli(prev, v_lli, p_lli, sys_addr, ext_addr, xfer_len);

		ext_addr += xfer_len;
		sys_addr += xfer_len;
		len -= xfer_len;

		prev = v_lli;
	}

	return vchan_tx_prep(&chan->vc, &desc->vd, flags);

err_exit:
	cdns_udma_vchan_free_desc(&desc->vd);
	kfree(desc);
	return NULL;
}

static struct dma_async_tx_descriptor *cdns_udma_prep_slave_sg(
		struct dma_chan *dma_chan, struct scatterlist *sgl,
		unsigned int sg_len, enum dma_transfer_direction direction,
		unsigned long flags, void *bulk_context)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(dma_chan);
	struct cdns_udma_dev *udma = chan->udma;
	struct cdns_udma_desc *desc;
	struct cdns_udma_lli *v_lli, *prev = NULL;
	struct scatterlist *sg_sys;
	dma_addr_t ext_addr, sys_addr, p_lli;
	u32 i, sys_len = 0;

	desc = kzalloc(sizeof(*desc), GFP_NOWAIT);
	if (!desc)
		return NULL;

	sg_sys = sgl;
	desc->dir = direction;
	ext_addr = direction == DMA_MEM_TO_DEV ? chan->config.dst_addr : chan->config.src_addr;
	for_each_sg(sgl, sg_sys, sg_len, i) {
		sys_addr = sg_dma_address(sg_sys);
		sys_len = sg_dma_len(sg_sys);

		while (sys_len > 0) {
			u32 xfer_len = min_t(u32, sys_len, CDNS_UDMA_BULK_MAX_SIZE);

			v_lli = dma_pool_zalloc(udma->lli_pool, GFP_NOWAIT, &p_lli);
			if (!v_lli)
				goto err_exit;

			if (prev == NULL) {
				desc->v_lli = v_lli;
				desc->p_lli = p_lli;
			}

			cdns_udma_setup_lli(prev, v_lli, p_lli, sys_addr, ext_addr, xfer_len);

			ext_addr += xfer_len;
			sys_addr += xfer_len;
			sys_len -= xfer_len;

			prev = v_lli;
		}
	}

	return vchan_tx_prep(&chan->vc, &desc->vd, flags);

err_exit:
	cdns_udma_vchan_free_desc(&desc->vd);
	kfree(desc);
	return NULL;
}

static void cdns_udma_start_transfer(struct cdns_udma_chan *chan)
{
	struct cdns_udma_dev *udma = chan->udma;
	struct cdns_udma_desc *desc;
	struct virt_dma_desc *vd;
	u32 id = chan->vc.chan.chan_id;

	vd = vchan_next_desc(&chan->vc);
	if (!vd) {
		dev_err(chan->dev, "invalid virt_dma_desc: chan is no.%x!\n", id);
		return;
	}

	list_del(&vd->node);

	desc = to_cdns_udma_desc(vd);
	chan->desc = desc;

	/* Set up starting descriptor */
	writel(lower_32_bits(desc->p_lli), udma->regbase + CDNS_UDMA_SP_L_OFFSET(id));
	writel(upper_32_bits(desc->p_lli), udma->regbase + CDNS_UDMA_SP_U_OFFSET(id));

	/* Clear channel attr */
	writel(0, udma->regbase + CDNS_UDMA_ATTR_L_OFFSET(id));
	writel(0, udma->regbase + CDNS_UDMA_ATTR_U_OFFSET(id));

	/* let's go */
	if (desc->dir == DMA_MEM_TO_DEV)
		writel(0x3, udma->regbase + CDNS_UDMA_CTRL_OFFSET(id));
	else
		writel(0x1, udma->regbase + CDNS_UDMA_CTRL_OFFSET(id));
}

static void cdns_udma_issue_pending(struct dma_chan *c)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(c);
	unsigned long flags;

	spin_lock_irqsave(&chan->vc.lock, flags);

	if (vchan_issue_pending(&chan->vc))
		cdns_udma_start_transfer(chan);

	spin_unlock_irqrestore(&chan->vc.lock, flags);
}

static void cdns_udma_dump_error(struct cdns_udma_chan *chan, u32 id)
{
	struct cdns_udma_dev *udma = chan->udma;
	struct cdns_udma_lli *v_lli = chan->desc->v_lli;
	u32 num = 0;

	dev_err(chan->dev,
		"chan[%d]: uc ib: %x, c ib: %x, uc ob: %x, c ob: %x\n", id,
		readl(udma->regbase + CDNS_UDMA_IB_ECC_UNCO_OFFSET),
		readl(udma->regbase + CDNS_UDMA_IB_ECC_CORR_OFFSET),
		readl(udma->regbase + CDNS_UDMA_OB_ECC_UNCO_OFFSET),
		readl(udma->regbase + CDNS_UDMA_OB_ECC_CORR_OFFSET));

	while (v_lli) {
		dev_err(chan->dev, "chan[%d], lli[%d]: "
			"sys stat 0x%02x, ext stat 0x%02x, channel stat 0x%02x\n",
			id, num, v_lli->sys_status, v_lli->ext_status, v_lli->chan_status);

		v_lli = v_lli->v_lli_next;
		num++;
	}
}

static irqreturn_t cdns_udma_irq(int irq, void *data)
{
	struct cdns_udma_dev *udma = data;
	struct cdns_udma_chan *chan;
	u32 id, int_status;

	int_status = readl(udma->regbase + CDNS_UDMA_INT_OFFSET);

	/* Clear interrupt status */
	writel(int_status, udma->regbase + CDNS_UDMA_INT_OFFSET);

	for (id = 0; id < udma->nr_channels; id++) {
		chan = &udma->chan[id];

		if (int_status & BIT(id + CDNS_UDMA_MAX_CHANNELS)) {
			dev_err(chan->dev, "uDMA error!\n");
			cdns_udma_dump_error(chan, id);

		} else if (int_status & BIT(id)) {
			spin_lock(&chan->vc.lock);
			vchan_cookie_complete(&chan->desc->vd);
			chan->desc = NULL;
			spin_unlock(&chan->vc.lock);
		}
	}

	return IRQ_HANDLED;
}

static int cdns_udma_config(struct dma_chan *dma_chan, struct dma_slave_config *config)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(dma_chan);

	if (config->direction == DMA_MEM_TO_DEV || config->direction == DMA_DEV_TO_MEM)
		chan->dir = config->direction;
	else
		return -ENXIO;

	chan->config = *config;

	return 0;
}

static int cdns_udma_alloc_chan_resources(struct dma_chan *dma_chan)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(dma_chan);

	cdns_udma_enable_irq(chan);
	chan->dir = 0;

	return 0;
}

static void cdns_udma_free_chan_resources(struct dma_chan *dma_chan)
{
	struct cdns_udma_chan *chan = to_cdns_udma_chan(dma_chan);

	cdns_udma_disable_irq(chan);
	chan->dir = 0;

	vchan_free_chan_resources(to_virt_chan(dma_chan));
}

static int cdns_udma_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct cdns_udma_dev *udma;
	struct cdns_udma_chan *chan;
	struct dma_device *dma_dev;
	u32 i, val;
	int ret;

	udma = devm_kzalloc(dev, sizeof(struct cdns_udma_dev), GFP_KERNEL);
	if (!udma)
		return -ENOMEM;

	udma->regbase = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(udma->regbase)) {
		dev_err(dev, "devm_ioremap() failed\n");
		return PTR_ERR(udma->regbase);
	}

	udma->irq = platform_get_irq(pdev, 0);
	if (udma->irq < 0) {
		dev_err(dev, "get irq failed\n");
		return udma->irq;
	}

	udma->lli_pool = dmam_pool_create(dev_name(dev),
				dev, sizeof(struct cdns_udma_lli), 64, 0);
	if (!udma->lli_pool)
		return -ENOMEM;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "failed to set dma mask to 64\n");
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev->parent, DMA_BIT_MASK(64));
	if (ret) {
		dev_err(dev, "failed to set parent dma mask to 64\n");
		return ret;
	}

	udma->align = SZ_16;

	/* this register must be accessed after APB reset is deasserted */
	val = readl(udma->regbase + CDNS_UDMA_CONFIG_OFFSET);
	udma->nr_channels = FIELD_GET(CDNS_UDMA_NUM_CHANNELS_MASK, val);
	BUG_ON(udma->nr_channels > CDNS_UDMA_MAX_CHANNELS);

	for (i = 0; i < udma->nr_channels; i++) {
		udma->slave_map[i].slave = devm_kasprintf(dev, GFP_KERNEL, "ch%d", i);
		udma->slave_map[i].devname = dev_name(dev->parent);
	}

	dma_dev = &udma->dma_dev;
	dma_dev->dev = dev;
	INIT_LIST_HEAD(&dma_dev->channels);

	dma_cap_zero(dma_dev->cap_mask);
	dma_cap_set(DMA_SLAVE, dma_dev->cap_mask);
	dma_cap_set(DMA_MEMCPY, dma_dev->cap_mask);
	dma_cap_set(DMA_PRIVATE, dma_dev->cap_mask);

	/* TODO: impelment device_terminate_all */
	dma_dev->device_prep_dma_memcpy = cdns_udma_prep_dma_memcpy;
	dma_dev->device_prep_slave_sg = cdns_udma_prep_slave_sg;
	dma_dev->device_config = cdns_udma_config;
	dma_dev->device_issue_pending = cdns_udma_issue_pending;
	dma_dev->device_tx_status = dma_cookie_status;
	dma_dev->device_alloc_chan_resources = cdns_udma_alloc_chan_resources;
	dma_dev->device_free_chan_resources = cdns_udma_free_chan_resources;
	dma_dev->directions = BIT(DMA_MEM_TO_DEV) | BIT(DMA_DEV_TO_MEM);
	dma_dev->src_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dma_dev->dst_addr_widths = BIT(DMA_SLAVE_BUSWIDTH_4_BYTES) |
				   BIT(DMA_SLAVE_BUSWIDTH_8_BYTES);
	dma_dev->residue_granularity = DMA_RESIDUE_GRANULARITY_DESCRIPTOR;

	dma_dev->filter.map = udma->slave_map;
	dma_dev->filter.mapcnt = udma->nr_channels;

	udma->chan = devm_kzalloc(dev, sizeof(*chan) * udma->nr_channels, GFP_KERNEL);
	if (!udma->chan)
		return -ENOMEM;

	for (i = 0; i < udma->nr_channels; i++) {
		chan = &udma->chan[i];
		chan->dev = dev;
		chan->udma = udma;
		chan->vc.desc_free = cdns_udma_vchan_free_desc;
		vchan_init(&chan->vc, dma_dev);
	}

	ret = devm_request_irq(dev, udma->irq, cdns_udma_irq, 0, dev_name(dev), udma);
	if (ret) {
		dev_err(dev, "fail to request irq for udma channel!\n");
		return ret;
	}

	ret = dmaenginem_async_device_register(dma_dev);
	if (ret < 0) {
		dev_err(dev, "failed to register device!\n");
		return ret;
	}

	platform_set_drvdata(pdev, udma);

	dev_info(dev, "Register successfully, support %u channels\n", udma->nr_channels);

	return 0;
}

static int cdns_udma_remove(struct platform_device *pdev)
{
	struct cdns_udma_dev *udma = platform_get_drvdata(pdev);

	dma_async_device_unregister(&udma->dma_dev);

	return 0;
}

static const struct of_device_id cdns_udma_match[] = {
	{
		.compatible = "cdns,udma",
	},
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, cdns_udma_match);

static struct platform_driver cdns_udma_platform_driver = {
	.driver = {
		.name = "cdns_udma",
		.of_match_table = cdns_udma_match,
	},
	.probe = cdns_udma_probe,
	.remove = cdns_udma_remove,
};
module_platform_driver(cdns_udma_platform_driver);

MODULE_AUTHOR("Li Chen <lchen@ambarella.com>");
MODULE_DESCRIPTION("Candence uDMA controller driver");
MODULE_LICENSE("GPL v2");
