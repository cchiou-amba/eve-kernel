/*
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>
#include <linux/msi.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/pci.h>
#include <linux/log2.h>
#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/of_reserved_mem.h>
#include <linux/dma-mapping.h>
#include <linux/memremap.h>

#define MSI_DETECTOR_MAX_FIFOS		32
#define MSI_DETECTOR_MAX_MSIS		32

#define MSI_DETECTOR_MSI_ADDR_REG_LOW	0x00
#define MSI_DETECTOR_MSI_ADDR_REG_HIGH	0x04
#define MSI_DETECTOR_FIFO_CTRL_REG	0x20
#define MSI_DETECTOR_CTRL_REG		0x30
#define MSI_DETECTOR_FIFO_DATA_REG	0x100

#define MSI_DETECTOR_CTRL_ENABLE	BIT(0)
#define MSI_DETECTOR_CTRL_FIFO_RESET	BIT(1)

#define MSI_DETECTOR_FIFO_EMPTY		BIT(0)
#define MSI_DETECTOR_FIFO_FULL		BIT(1)
#define MSI_DETECTOR_FIFO_OVERFLOW	BIT(2)
#define MSI_DETECTOR_FIFO_UNDERFLOW	BIT(3)
#define MSI_DETECTOR_FIFO_COUNT_MASK	(0x3F << 4)
#define MSI_DETECTOR_FIFO_COUNT_SHIFT	4

#define MSI_DETECTOR_FIFO_UNDERFLOW_DATA	0xDEADBEEF

/* aligned target address for better compatibility */
#define MSI_DETECTOR_ALIGN_SIZE			SZ_512K
#define MSI_DETECTOR_FALLBACK_ALIGN_SIZE	SZ_64K

struct msi_detect_fifo {
	/* the fifo now only have one data
	 * 0x0000_xxxx, bit[4:0] as hardware-vector
	 */
	u32 data;
};

struct msi_detector_data {
	struct device *dev;
	void __iomem *base;

	int spi_irq;				/* interrupt report to gic by this spi line */
	struct irq_domain *hardware_domain;	/* hardware layer */
	struct irq_domain *msi_domain;		/* software layer */
	struct mutex msi_map_lock;
	struct msi_domain_info msi_domain_info;
	struct irq_chip msi_irq_chip;		/* software layer */
	struct irq_chip hardware_irq_chip;	/* hardware layer */
	unsigned long msi_irq_in_use[BITS_TO_LONGS(MSI_DETECTOR_MAX_MSIS)];

	spinlock_t fifo_lock;			/* store the msg-data(irq-vector) */
	struct msi_detect_fifo fifo_buffer[MSI_DETECTOR_MAX_FIFOS];
	int fifo_head;
	int fifo_tail;
	int fifo_count;

	/* pcie-detector monitor this axi-address wroten from pcie-bus
	 * if enabled then record the data into fifo and report interrupt
	 * to gic if fifo is not empty.*/
	dma_addr_t msi_target_paddr;
	bool using_reserved_mem;
};

static inline u32 msi_detector_readl(struct msi_detector_data *priv, u32 offset)
{
	return readl(priv->base + offset);
}

static inline void msi_detector_writel(struct msi_detector_data *priv, u32 offset, u32 value)
{
	writel(value, priv->base + offset);
}

static inline u32 msi_detector_get_fifo_count(struct msi_detector_data *priv)
{
	u32 fifo_entry, fifo_ctrl;

	fifo_ctrl = msi_detector_readl(priv, MSI_DETECTOR_FIFO_CTRL_REG);
	fifo_entry = (fifo_ctrl & MSI_DETECTOR_FIFO_COUNT_MASK) >> MSI_DETECTOR_FIFO_COUNT_SHIFT;

	return fifo_entry;
}

static inline bool msi_detector_fifo_empty(struct msi_detector_data *priv)
{
	u32 fifo_ctrl;

	fifo_ctrl = msi_detector_readl(priv, MSI_DETECTOR_FIFO_CTRL_REG);
	return !!(fifo_ctrl & MSI_DETECTOR_FIFO_EMPTY);
}

static inline bool msi_detector_fifo_has_error(struct msi_detector_data *priv)
{
	u32 fifo_ctrl;

	fifo_ctrl = msi_detector_readl(priv, MSI_DETECTOR_FIFO_CTRL_REG);
	return !!(fifo_ctrl & (MSI_DETECTOR_FIFO_OVERFLOW | MSI_DETECTOR_FIFO_UNDERFLOW));
}

static void msi_detector_mask_msi(struct irq_data *data)
{
	pci_msi_mask_irq(data);
}

static void msi_detector_unmask_msi(struct irq_data *data)
{
	pci_msi_unmask_irq(data);
}

static void msi_detector_ack_msi(struct irq_data *data)
{
	pr_debug("call:%s \n", __FUNCTION__);
	irq_chip_mask_parent(data);
}

static void msi_detector_compose_msi_msg(struct irq_data *data, struct msi_msg *msg)
{
	struct msi_detector_data *priv;
	struct irq_data *parent_data;

	parent_data = data->parent_data;
	if (!parent_data) {
		dev_err(priv->dev,"No parent IRQ data found\n");
		return;
	}

	priv = irq_data_get_irq_chip_data(parent_data);
	if (!priv) {
		dev_err(priv->dev,"No private data found in parent domain\n");
		return;
	}

	/* msg->address_lo/address_hi should be treat as pcie-bus address.
	 * this address is tell-epside and ep write to this pcie-bus-address.
	 * Thanks to RC has the default BAR7 inbound windows, which can
	 * map inbound_pcie_addreess:Axi_phy_address = 1:1.
	 * the msi_data.bit[4:0] should be used as vectors of 32 msi-msgs
	 * that is as hwirq (0-31) for msi
	*/
	msg->address_lo = lower_32_bits(priv->msi_target_paddr);
	msg->address_hi = upper_32_bits(priv->msi_target_paddr);
	msg->data = parent_data->hwirq;

	dev_dbg(priv->dev, "MSI msg: addr_lo=0x%08x, addr_hi=0x%08x, data=0x%08x (hwirq=%lu)\n",
		msg->address_lo, msg->address_hi, msg->data, parent_data->hwirq);
}

/* disable each message interrupt ? not avaiable from hardware */
static void msi_detector_hardware_mask(struct irq_data *data)
{
	pr_debug("call:%s \n", __FUNCTION__);
}

/* enable each message detector ? not avaiable from hardware */
static void msi_detector_hardware_unmask(struct irq_data *data)
{
	pr_debug("call:%s \n", __FUNCTION__);
}

static void msi_detector_hardware_ack(struct irq_data *data)
{
	pr_debug("call:%s \n", __FUNCTION__);
}

static int msi_detector_hardware_domain_alloc(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs,
					   void *args)
{
	struct msi_detector_data *priv = domain->host_data;
	unsigned int hwirq, i, aligned_hwirq;

	/* For multi-MSI, nr_irqs should be a power of 2 and aligned */
	if (nr_irqs > 1) {
		if (!is_power_of_2(nr_irqs)) {
			dev_warn(priv->dev,
			 "invalid %u vectors, msi-vectors must be pow of 2 \n", nr_irqs);
			 nr_irqs = roundup_pow_of_two(nr_irqs);
		}

		if (nr_irqs > MSI_DETECTOR_MAX_MSIS) {
			dev_err(priv->dev, "can't alloc more than %u msi-vectors \n",
				MSI_DETECTOR_MAX_MSIS);
			return -EINVAL;
		}
	}

	mutex_lock(&priv->msi_map_lock);

	/* Find aligned position for multi-MSI */
	if (nr_irqs > 1) {
		/* For multi-MSI, find aligned position */
		for (aligned_hwirq = 0;
		     aligned_hwirq < MSI_DETECTOR_MAX_MSIS;
		     aligned_hwirq += nr_irqs) {

			/* Check if all required slots are available */
			bool available = true;
			for (i = 0; i < nr_irqs; i++) {
				if (aligned_hwirq + i >= MSI_DETECTOR_MAX_MSIS ||
				    test_bit(aligned_hwirq + i, priv->msi_irq_in_use)) {
					available = false;
					break;
				}
			}
			if (available) {
				hwirq = aligned_hwirq;
				break;
			}
		}

		if (aligned_hwirq >= MSI_DETECTOR_MAX_MSIS) {
			mutex_unlock(&priv->msi_map_lock);
			dev_err(priv->dev,
				"multi_msi_nospc: aligned_hwirq: %u, want:%u \n",
				aligned_hwirq, nr_irqs);
			return -ENOSPC;
		}
	} else {
		/* Single MSI - find any available slot */
		hwirq = find_first_zero_bit(priv->msi_irq_in_use, MSI_DETECTOR_MAX_MSIS);
		if (hwirq >= MSI_DETECTOR_MAX_MSIS) {
			mutex_unlock(&priv->msi_map_lock);
			dev_err(priv->dev,
				"single_msi_nospc: hwirq: %u \n", hwirq);
			return -ENOSPC;
		}
	}

	/* Allocate all requested IRQs */
	for (i = 0; i < nr_irqs; i++) {
		set_bit(hwirq + i, priv->msi_irq_in_use);
	}

	mutex_unlock(&priv->msi_map_lock);

	/* Set up IRQ domain info for all allocated IRQs */
	for (i = 0; i < nr_irqs; i++) {
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &priv->hardware_irq_chip, priv,
				    handle_simple_irq, NULL, NULL);
	}

	dev_dbg(priv->dev,
		"Allocated %d MSI vectors starting at hwirq %d virq: %d \n",
		nr_irqs, hwirq, virq);

	return 0;
}

static void msi_detector_hardware_domain_free(struct irq_domain *domain,
					   unsigned int virq,
					   unsigned int nr_irqs)
{
	int i;
	struct msi_detector_data *priv = domain->host_data;
	struct irq_data *data = irq_domain_get_irq_data(domain, virq);

	if (!data)
		return;

	mutex_lock(&priv->msi_map_lock);
	for (i = 0; i < nr_irqs; i++)
		clear_bit(data->hwirq + i, priv->msi_irq_in_use);
	mutex_unlock(&priv->msi_map_lock);

	dev_dbg(priv->dev, "Freed %d MSI vectors starting at hwirq %lu\n",
		nr_irqs, data->hwirq);
}

static const struct irq_domain_ops msi_detector_hardware_domain_ops = {
	.alloc = msi_detector_hardware_domain_alloc,
	.free = msi_detector_hardware_domain_free,
};

static int msi_detector_process_fifo(struct msi_detector_data *priv)
{
	struct msi_detect_fifo *entry;
	unsigned long flags;
	int processed = 0;
	int virq;
	u32 fifo_ctrl, hw_fifo_count;

	spin_lock_irqsave(&priv->fifo_lock, flags);

	/* Check for FIFO errors */
	if (msi_detector_fifo_has_error(priv)) {
		fifo_ctrl = msi_detector_readl(priv, MSI_DETECTOR_FIFO_CTRL_REG);
		if (fifo_ctrl & MSI_DETECTOR_FIFO_OVERFLOW) {
			dev_dbg(priv->dev, "FIFO overflow detected\n");
		}
		if (fifo_ctrl & MSI_DETECTOR_FIFO_UNDERFLOW) {
			dev_dbg(priv->dev, "FIFO underflow detected\n");
		}
	}

	/* Read FIFO entries from hardware */
	while (!msi_detector_fifo_empty(priv) && priv->fifo_count < MSI_DETECTOR_MAX_FIFOS) {
		entry = &priv->fifo_buffer[priv->fifo_tail];

		/* Read MSI data from hardware FIFO */
		entry->data = msi_detector_readl(priv, MSI_DETECTOR_FIFO_DATA_REG);

		/* Check for underflow indicator, empty now */
		if (entry->data == MSI_DETECTOR_FIFO_UNDERFLOW_DATA) {
			dev_warn(priv->dev, "FIFO underflow - got 0xDEADBEEF\n");
			break;
		}

		priv->fifo_tail = (priv->fifo_tail + 1) % MSI_DETECTOR_MAX_FIFOS;
		priv->fifo_count++;
	}

	while (priv->fifo_count > 0) {
		entry = &priv->fifo_buffer[priv->fifo_head];
		if (entry->data < MSI_DETECTOR_MAX_MSIS) {
			virq = irq_find_mapping(priv->hardware_domain, entry->data);
			if (virq) {
				dev_dbg(priv->dev, "Processing MSI: hwirq=%u -> virq=%u\n",
					entry->data, virq);
				generic_handle_irq(virq);
				processed++;
			} else {
				dev_err(priv->dev, "No mapping found for MSI data 0x%x in hardware domain\n", entry->data);
			}
		} else {
			dev_err(priv->dev, "Invalid MSI data max 31 \n");
		}

		priv->fifo_head = (priv->fifo_head + 1) % MSI_DETECTOR_MAX_FIFOS;
		priv->fifo_count--;
	}

	/* Log hardware FIFO count for debugging */
	hw_fifo_count = msi_detector_get_fifo_count(priv);
	if (hw_fifo_count != 0) {
		dev_dbg(priv->dev, "Hardware FIFO count: %u, processed: %d\n",
			hw_fifo_count, processed);
	}

	spin_unlock_irqrestore(&priv->fifo_lock, flags);

	return processed;
}

static irqreturn_t msi_detector_irq_handler(int irq, void *data)
{
	struct msi_detector_data *priv = data;
	int processed;

	if (msi_detector_fifo_empty(priv))
		return IRQ_NONE;

	processed = msi_detector_process_fifo(priv);

	dev_dbg(priv->dev, "Processed %d MSI interrupts\n", processed);

	return IRQ_HANDLED;
}

/* make sure the aligned address is still in the rerserved-region */
static bool msi_detector_target_fits(phys_addr_t base, resource_size_t size,
				     phys_addr_t aligned)
{
	return aligned <= base + size;
}

/* aligned the target address: 512KB or fallback to 64KB */
static int msi_detector_align_target(phys_addr_t base, resource_size_t size,
				     phys_addr_t *paddr)
{
	phys_addr_t aligned;

	if (IS_ALIGNED(base, MSI_DETECTOR_ALIGN_SIZE)) {
		*paddr = base;
		return 0;
	}

	aligned = ALIGN(base, MSI_DETECTOR_ALIGN_SIZE);
	if (msi_detector_target_fits(base, size, aligned)) {
		*paddr = aligned;
		return 0;
	}

	/* fall back to 64KB alignment */
	if (IS_ALIGNED(base, MSI_DETECTOR_FALLBACK_ALIGN_SIZE)) {
		if (msi_detector_target_fits(base, size, base)) {
			*paddr = base;
			return 0;
		}
	} else {
		aligned = ALIGN(base, MSI_DETECTOR_FALLBACK_ALIGN_SIZE);
		if (msi_detector_target_fits(base, size, aligned)) {
			*paddr = aligned;
			return 0;
		}
	}

	return -EINVAL;
}

/* pre-defined target address: check alignment */
static int msi_detector_init_target_region(struct msi_detector_data *priv,
					   struct reserved_mem *rmem)
{
	struct device *dev = priv->dev;
	phys_addr_t paddr;
	int ret;

	if (rmem->size < PAGE_SIZE) {
		dev_err(dev, "reserved region %s too small (%pa)\n",
			rmem->name, &rmem->size);
		return -EINVAL;
	}

	ret = msi_detector_align_target(rmem->base, rmem->size, &paddr);
	if (ret) {
		dev_err(dev,
			"reserved region %s: base %pa cannot align to %u or %u, size %pa too small\n",
			rmem->name, &rmem->base, MSI_DETECTOR_ALIGN_SIZE,
			MSI_DETECTOR_FALLBACK_ALIGN_SIZE, &rmem->size);
		return ret;
	}

	/* recored the address */
	priv->msi_target_paddr = paddr;

	dev_info(dev, "MSI target address: %s at 0x%llx\n",
		 rmem->name, (phys_addr_t)priv->msi_target_paddr);

	return 0;
}

static int msi_detector_alloc_target_region(struct msi_detector_data *priv)
{
	struct device *dev = priv->dev;
	int ret;
	void *orig_vaddr;
	dma_addr_t orig_paddr;
	dma_addr_t align_size = MSI_DETECTOR_ALIGN_SIZE;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(32));
	if (ret < 0) {
		dev_warn(dev, "set dma mask failed(32-bit), falling back to 64-bit\n");
		ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
		if (ret < 0) {
			dev_warn(dev, "set dma mask failed(64-bit)\n");
			return ret;
		}
	}

	orig_vaddr = dmam_alloc_coherent(dev, PAGE_SIZE + align_size,
					&orig_paddr, GFP_KERNEL);
	if (!orig_vaddr) {
		align_size = MSI_DETECTOR_FALLBACK_ALIGN_SIZE;
		orig_vaddr = dmam_alloc_coherent(dev, PAGE_SIZE + align_size,
					&orig_paddr, GFP_KERNEL);
	}

	if (!orig_vaddr) {
		dev_err(dev, "Fail to alloc MSI_target address \n");
		return -ENOMEM;
	}

	if (!IS_ALIGNED(orig_paddr, align_size))
		priv->msi_target_paddr = PTR_ALIGN(orig_paddr, align_size);
	else
		priv->msi_target_paddr = orig_paddr;

	dev_info(dev, "MSI target address: 0x%llx \n", (phys_addr_t)priv->msi_target_paddr);
	return 0;
}

/*
 * the msi-target address needs to handle 3 cases
 * case1: fixed-target-address
 * case2: alloc from the reserved memory
 * case3: alloc from the system memory
 *
 * to be max compatible:
 * 1. aligned up to 512KB to meet almost all controller's ATU alignment requirment.
 * 2. fallback set 64KB. if not 64KB aligned then fail.
 * 3. place the target-address in lower-4GB; lower-2GB even better.
 *
 * case1_dts_node: msi target in the preset place.
 *	msi_target_mema: msi-target-mema@02000000 {
 *		compatible = "shared-dma-pool";
 *		reg = <0x0 0x02000000  0x0 0x010000>;
 *		no-map;
 *	};
 *
 * case2_dts_node: msi target from reserved memory
 *	msi_target_memb: msi-target-memb {
 *		compatible = "shared-dma-pool";
 *		size = <0x0 0x010000>;
 *		no-map;
 *	};
 *
 * case1_case_dts_node
 *  &msi_detect {
 *	memory-region = <&msi_target_mema/b>;
 *	status = "okay";
 *  };
 */
static int msi_detector_hw_init(struct msi_detector_data *priv)
{
	struct device_node *mem_node;
	struct reserved_mem *rmem;
	bool fixed_addr;
	u32 ctrl;
	int ret;

	mem_node = of_parse_phandle(priv->dev->of_node, "memory-region", 0);
	if (mem_node) {
		fixed_addr = of_find_property(mem_node, "reg", NULL);
		rmem = of_reserved_mem_lookup(mem_node);
		of_node_put(mem_node);

		if (!rmem)
			return -EINVAL;

		if (fixed_addr) {
			/* msi-addr: pre-defined at the dedicated place */
			ret = msi_detector_init_target_region(priv, rmem);
		} else {
			/* msi-addr: from reserved memory */
			ret = of_reserved_mem_device_init(priv->dev);
			if (ret) {
				priv->using_reserved_mem = false;
				dev_warn(priv->dev,
			 	"no reserved memory,falling back to DMA coherent allocation\n");
			} else
				priv->using_reserved_mem = true;

			/* alloc the target address from reserved memory */
			ret = msi_detector_alloc_target_region(priv);
		}
	} else {
		/* alloc the target address from system memory */
		ret = msi_detector_alloc_target_region(priv);
	}

	/* check if alloc ok */
	if (ret) {
		if (priv->using_reserved_mem) {
			of_reserved_mem_device_release(priv->dev);
			priv->using_reserved_mem = false;
		}
		return ret;
	}

	/* Configure MSI target address registers */
	msi_detector_writel(priv, MSI_DETECTOR_MSI_ADDR_REG_LOW,
			    lower_32_bits(priv->msi_target_paddr));
	msi_detector_writel(priv, MSI_DETECTOR_MSI_ADDR_REG_HIGH,
			    upper_32_bits(priv->msi_target_paddr));

	/* Reset the FIFO */
	ctrl = msi_detector_readl(priv, MSI_DETECTOR_CTRL_REG);
	ctrl |= MSI_DETECTOR_CTRL_FIFO_RESET;
	msi_detector_writel(priv, MSI_DETECTOR_CTRL_REG, ctrl);
	udelay(10);

	/* Enable MSI detector and can report interrupt to gic */
	ctrl |= MSI_DETECTOR_CTRL_ENABLE;
	msi_detector_writel(priv, MSI_DETECTOR_CTRL_REG, ctrl);

	return 0;
}

static int msi_detector_probe(struct platform_device *pdev)
{
	struct msi_detector_data *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "Failed to allocate memory for msi_detector_data\n");
		return -ENOMEM;
	}

	priv->dev = &pdev->dev;
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->spi_irq = platform_get_irq(pdev, 0);
	if (priv->spi_irq < 0)
		return priv->spi_irq;

	mutex_init(&priv->msi_map_lock);
	spin_lock_init(&priv->fifo_lock);
	priv->fifo_head = 0;
	priv->fifo_tail = 0;
	priv->fifo_count = 0;

	/* hardware layer IRQ chip */
	priv->hardware_irq_chip.name = "msi-detector-hardware";
	priv->hardware_irq_chip.irq_mask = msi_detector_hardware_mask;
	priv->hardware_irq_chip.irq_unmask = msi_detector_hardware_unmask;
	priv->hardware_irq_chip.irq_ack = msi_detector_hardware_ack;
	priv->hardware_domain = irq_domain_create_linear(of_node_to_fwnode(pdev->dev.of_node),
						      MSI_DETECTOR_MAX_MSIS,
						      &msi_detector_hardware_domain_ops,
						      priv);
	if (!priv->hardware_domain) {
		dev_err(&pdev->dev, "Failed to create hardware linear domain\n");
		return -ENOMEM;
	}

	/* software layer */
	priv->msi_irq_chip.name = "msi-detector-pcie";
	priv->msi_irq_chip.irq_mask = msi_detector_mask_msi;
	priv->msi_irq_chip.irq_unmask = msi_detector_unmask_msi;
	priv->msi_irq_chip.irq_ack = msi_detector_ack_msi;
	priv->msi_irq_chip.irq_set_affinity = irq_chip_set_affinity_parent;
	priv->msi_irq_chip.irq_compose_msi_msg = msi_detector_compose_msi_msg;
	priv->msi_domain_info.flags = MSI_FLAG_USE_DEF_DOM_OPS |
				      MSI_FLAG_USE_DEF_CHIP_OPS |
				      MSI_FLAG_MULTI_PCI_MSI;
	priv->msi_domain_info.chip = &priv->msi_irq_chip;
	priv->msi_domain_info.ops = NULL;
	priv->msi_domain_info.handler = handle_edge_irq;
	priv->msi_domain_info.handler_name = "edge";
	priv->msi_domain = pci_msi_create_irq_domain(of_node_to_fwnode(pdev->dev.of_node),
						     &priv->msi_domain_info,
						     priv->hardware_domain);
	if (!priv->msi_domain) {
		dev_err(&pdev->dev, "Failed to create PCI MSI domain\n");
		irq_domain_remove(priv->hardware_domain);
		return -ENOMEM;
	}

	/* all 32 msi-msg will report to one gic-spi interrupt line */
	ret = devm_request_irq(&pdev->dev, priv->spi_irq,
			       msi_detector_irq_handler,
			       IRQF_SHARED, "msi-detector", priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to request SPI interrupt %d: %d\n",
			priv->spi_irq, ret);
		goto err_remove_domain;
	}

	ret = msi_detector_hw_init(priv);
	if (ret) {
		dev_err(&pdev->dev, "Failed to initialize hardware: %d\n", ret);
		goto err_remove_domain;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "MSI detector initialized with %d FIFO entries, SPI IRQ %d\n",
		 MSI_DETECTOR_MAX_FIFOS, priv->spi_irq);

	return 0;

err_remove_domain:
	irq_domain_remove(priv->msi_domain);
	return ret;
}

static int msi_detector_remove(struct platform_device *pdev)
{
	struct msi_detector_data *priv = platform_get_drvdata(pdev);

	msi_detector_writel(priv, MSI_DETECTOR_CTRL_REG, 0);

	if (priv->msi_domain)
		irq_domain_remove(priv->msi_domain);
	if (priv->hardware_domain)
		irq_domain_remove(priv->hardware_domain);

	if (priv->using_reserved_mem)
		of_reserved_mem_device_release(&pdev->dev);

	return 0;
}

static const struct of_device_id msi_detector_of_match[] = {
	{ .compatible = "ambarella,msi", },
	{},
};
MODULE_DEVICE_TABLE(of, msi_detector_of_match);

static struct platform_driver msi_detector_driver = {
	.probe = msi_detector_probe,
	.remove = msi_detector_remove,
	.driver = {
		.name = "msi-detector",
		.of_match_table = msi_detector_of_match,
	},
};
builtin_platform_driver(msi_detector_driver);

MODULE_DESCRIPTION("msi detector driver");
MODULE_AUTHOR("Ambarella Plat");
MODULE_LICENSE("GPL v2");
