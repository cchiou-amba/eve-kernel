// SPDX-License-Identifier: GPL-2.0-only
/**
 * Host side driver to Ambarella EPF
 *
 * Copyright (C) 2023 Ambarella.Inc
 */
#include <linux/delay.h>
#include <linux/crc32.h>
#include <linux/version.h>
#include <linux/fs.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/pci.h>
#include <linux/pci_ids.h>
#include <linux/pci_regs.h>
#include <linux/etherdevice.h>
#include <soc/ambarella/pci.h>

#define DRV_MODULE_NAME		"pcirc-veth"


static u32 buf_size = SZ_1M;
module_param(buf_size, uint, 0644);
MODULE_PARM_DESC(buf_size, "The size of buffer used to transfer data");

static u32 mrrs = 4096;
module_param(mrrs, uint, 0644);
MODULE_PARM_DESC(mrrs, "The maximum read request size, valid values are 128, 256, 512, 1024, 2048, 4096");

static u32 test_xmit = 3;
module_param(test_xmit, uint, 0644);
MODULE_PARM_DESC(test_xmit, "Test xmit count when interface is opened");

static bool checksum = false;
module_param(checksum, bool, 0644);
MODULE_PARM_DESC(checksum, "Enable data checksum");

static bool napi_rx = false;
module_param(napi_rx, bool, 0644);
MODULE_PARM_DESC(napi_rx, "Enable napi poll to recvice");

static u32 rc2ep_seq, ep2rc_seq;

#define ctrl_offsetof(v)	offsetof(struct pci_ambarella_ctrl, v)

#define to_pcirc_ambaveth(w)	container_of(w, struct pcirc_ambaveth_info, w)

static DEFINE_IDA(pcirc_ambaveth_ida);

enum {
	VETH_STATUS_BUSY = 0,
};

struct pcirc_ambaveth_buffer {
	void __iomem *mailbox_bar;
	void *virt[PCIE_CTRL_NUM];
	dma_addr_t phys[PCIE_CTRL_NUM];
	size_t size;
	struct mutex mtx;
	struct completion done;
	u32 last_used;
	u32 last_avail;
};

struct pcirc_ambaveth_info {
	struct pci_dev *pdev;
	void __iomem *ctrl_bar;
	int id;
	int mrrs;

	struct net_device *ndev;
	struct work_struct recv;
	struct work_struct xmit;
	struct sk_buff_head txq;
	unsigned long status;
	struct napi_struct napi;

	struct pcirc_ambaveth_buffer buf_rx;
	struct pcirc_ambaveth_buffer buf_tx;
};

static irqreturn_t pcirc_ambaveth_irqhandler(int irq, void *dev_id)
{
	struct pcirc_ambaveth_info *info = dev_id;
	struct pcirc_ambaveth_buffer *buf;
	pci_mailbox_t *mbar;
	u32 avail, used;
	irqreturn_t ret = IRQ_NONE;

	buf = &info->buf_tx;
	mbar = buf->mailbox_bar;
	avail = mbar->avail;
	if (avail > buf->last_avail) {	// avail is updated by EP
		complete(&buf->done);
		ret = IRQ_HANDLED;
	}

	buf = &info->buf_rx;
	mbar = buf->mailbox_bar;
	used = mbar->used;
	if (used > buf->last_used) {	// used is updated by EP
		if (napi_rx) {
			napi_schedule(&info->napi);
		} else {
			complete(&buf->done);
		}
		ret = IRQ_HANDLED;
	}

	return ret;
}

#if 0	/* TODO */
static void pcirc_ambaveth_send_ctrl(void __iomem *ctrl_bar,
				     struct pci_ambarella_ctrl *ctrl)
{
	writel(ctrl->checksum, ctrl_bar + ctrl_offsetof(checksum));
	writel(lower_32_bits(ctrl->addr), ctrl_bar + ctrl_offsetof(addr));
	writel(upper_32_bits(ctrl->addr), ctrl_bar + ctrl_offsetof(addr) + 4);
	writel(ctrl->flags, ctrl_bar + ctrl_offsetof(flags));
	writel(ctrl->seq, ctrl_bar + ctrl_offsetof(seq));
	writel(ctrl->mrrs, ctrl_bar + ctrl_offsetof(mrrs));
	writel(ctrl->rsvd, ctrl_bar + ctrl_offsetof(rsvd));
	writel(ctrl->size, ctrl_bar + ctrl_offsetof(size));
	writel(ctrl->status, ctrl_bar + ctrl_offsetof(status));
	writel(ctrl->command, ctrl_bar + ctrl_offsetof(command));
}
#endif

static u32 pcirc_ambaveth_xfer_avail(struct pcirc_ambaveth_info *info)
{
	int ret = 0;
	u32 avail, used;
	pci_mailbox_t *mbar = info->buf_tx.mailbox_bar;
	struct pcirc_ambaveth_buffer *buf = &info->buf_tx;

	avail = mbar->avail;
	if (buf->last_avail < avail) {
		goto __found;
	} else {
		reinit_completion(&buf->done);
		ret = wait_for_completion_interruptible(&buf->done);
		if (ret < 0) {
			return -EIO;
		}
	}

	avail = mbar->avail;
	if (buf->last_avail >= avail) {
		return -EIO;
	}

__found:
	if (avail > (buf->last_avail + PCIE_CTRL_NUM)) {
		return -ERANGE;
	}

	used = mbar->used;
	avail = mbar->avail;
	pr_debug("RC/out: used.%u, avail.%u last_avail.%u ret=%d\n",
		 used, avail, buf->last_avail, ret);

	return buf->last_avail % PCIE_CTRL_NUM;
}

static int pcirc_ambaveth_recv_used(struct pcirc_ambaveth_info *info)
{
	int ret = 0, wait_comp = 0;
	u32 used, avail;
	pci_mailbox_t *mbar = info->buf_rx.mailbox_bar;
	struct pcirc_ambaveth_buffer *buf = &info->buf_rx;

	used = mbar->used;
	/* if buffer is used and updated by EP, got it */
	if (buf->last_used < used) {
		goto __found;
	} else {
		if (napi_rx) {
			return -EBUSY;
		} else {
			wait_comp = 1;
			reinit_completion(&buf->done);
			ret = wait_for_completion_interruptible(&buf->done);
			if (ret < 0) {
				return -EIO;
			}
		}
	}

	used = mbar->used;
	if (buf->last_used >= used) {
		return -EBUSY;
	}

__found:
	if (used > (buf->last_used + PCIE_CTRL_NUM)) {
		return -ERANGE;
	}
	used = mbar->used;
	avail = mbar->avail;

	pr_debug("RC/in: used.%u, avail.%u last_used.%u Wait=%u ret=%d\n",
	       used, avail, info->buf_rx.last_used, wait_comp, ret);

	return buf->last_used % PCIE_CTRL_NUM;
}

static int pcirc_ambaveth_xfer_to_ep(struct pcirc_ambaveth_info *info, void *data, size_t len)
{
	pci_ambarella_ctrl_t *ctrl;
	struct pcirc_ambaveth_buffer *buf = &info->buf_tx;
	pci_mailbox_t *mbar = info->buf_tx.mailbox_bar;
	int idx;

	BUG_ON(len > buf->size);

	idx = pcirc_ambaveth_xfer_avail(info);
	if (idx < 0) {
		return idx;
	}

	ctrl = (struct pci_ambarella_ctrl *)&mbar->ctrl[idx];
	memcpy(buf->virt[idx], data, len);

	ctrl->seq = ++rc2ep_seq;
	ctrl->addr_lo = lower_32_bits(buf->phys[idx]);
	ctrl->addr_hi = upper_32_bits(buf->phys[idx]);
	ctrl->size = len;
	ctrl->flags = 0;
	ctrl->status = checksum ? STATUS_CHKSUM_EN : 0;
	ctrl->rsvd = 0;
	ctrl->mrrs = info->mrrs;
	ctrl->checksum = checksum ? crc32_le(~0, buf->virt[idx], len) : 0;
	ctrl->command = TRANSFER_DATA_RC_TO_EP;

	dma_sync_single_for_device(&info->pdev->dev, buf->phys[idx], len, DMA_TO_DEVICE);
	dev_dbg(&info->pdev->dev, "rc2ep: %u Done\n", ctrl->seq);

	mbar->used ++;
	buf->last_avail ++;

	return len;
}

static int pcirc_ambaveth_xfer_from_ep(struct pcirc_ambaveth_info *info,
				       int (*cb)(struct pcirc_ambaveth_info *info, void *data, u32 len))
{
	int idx;
	u32 status, size, offset, crc32_val;
	struct pcirc_ambaveth_buffer *buf = &info->buf_rx;
	pci_mailbox_t *mbar = info->buf_rx.mailbox_bar;
	pci_ambarella_ctrl_t *ctrl;

	/* find a used xfer ctrl by EP */
	idx = pcirc_ambaveth_recv_used(info);
	if (idx < 0) {
		return -EBUSY;
	}

	ctrl = (struct pci_ambarella_ctrl *)&mbar->ctrl[idx];
	dev_dbg(&info->pdev->dev,
		"ep2rc: used %u, avail %u, last_used %u, seq %u\n",
		mbar->used,
		mbar->avail,
		buf->last_used,
		ctrl->seq);

	status = ctrl->status;
	if (!(status & STATUS_SUCCESS)) {
		dev_err(&info->pdev->dev, "Failed to transfer data from EP: 0x%x\n", status);
		return -EIO;
	}

	offset = ctrl->rsvd;
	size = ctrl->size;

	dma_sync_single_for_cpu(&info->pdev->dev, buf->phys[idx], buf->size, DMA_FROM_DEVICE);

	crc32_val = ctrl->checksum;
	if (checksum && (crc32_val != crc32_le(~0, buf->virt[idx] + offset, size))) {
		dev_err(&info->pdev->dev, "ep2rc: seq = %u, data checksum err\n", ep2rc_seq);
		do {
			size_t remain_size = size;
			size_t chunk_size;
			void *data = buf->virt + offset;

			while (remain_size > 0) {
				chunk_size = remain_size > SZ_16K ? SZ_16K : remain_size;

				dev_dbg(&info->pdev->dev, "%s[%lu] %08x\n",
					"ep2rc",
					size - remain_size,
					crc32_le(~0, data, chunk_size));
				remain_size -= chunk_size;
				data += chunk_size;
			}
		} while(0);
		return -EIO;
	}

	if (cb(info, buf->virt[idx] + offset, size) != size) {
		return -EPIPE;
	}

	/* Reuse ctrl */
	ctrl->seq = ++ep2rc_seq;
	ctrl->addr_lo = lower_32_bits(buf->phys[idx]);
	ctrl->addr_hi = upper_32_bits(buf->phys[idx]);
	ctrl->size = buf->size;
	ctrl->flags = 0;
	ctrl->status = checksum ? STATUS_CHKSUM_EN : 0;
	ctrl->mrrs = info->mrrs;
	ctrl->rsvd = 0;
	ctrl->checksum = 0;
	ctrl->command = TRANSFER_DATA_EP_TO_RC;

	/* Update index */
	buf->last_used ++;
	mbar->avail++;

	return 0;
}

static int pcirc_ambaveth_recv_callback(struct pcirc_ambaveth_info *info, void *data, u32 len)
{
	struct sk_buff *skb;

	skb = dev_alloc_skb(len);
	if (!skb) {
		return -ENOMEM;
	}

	skb_put_data(skb, data, len);
	skb->dev = info->ndev;
	skb->protocol = eth_type_trans(skb, skb->dev);

#if 1	/* TODO */
	netif_rx(skb);
#else
	netif_receive_skb(skb);
#endif
	return len;
}

static void pcirc_ambaveth_recv_worker(struct work_struct *recv)
{
	int ret;
	struct pcirc_ambaveth_info *info = to_pcirc_ambaveth(recv);

	ret = pcirc_ambaveth_xfer_from_ep(info, pcirc_ambaveth_recv_callback);
	if (ret < 0) {
		if (ret != -EBUSY) {
			dev_err(&info->pdev->dev, "xfer from ep error (%d)\n", ret);
			goto err_exit;
		}
	}

	schedule_work(&info->recv);

	return;

err_exit:
	cancel_work_sync(&info->recv);
}

static void pcirc_ambaveth_xmit_worker(struct work_struct *xmit)
{
	int ret;
	struct sk_buff *skb;
	struct pcirc_ambaveth_info *info = to_pcirc_ambaveth(xmit);

	while ((skb = skb_dequeue(&info->txq))) {
		ret = pcirc_ambaveth_xfer_to_ep(info, skb->data, skb->len);
		dev_kfree_skb(skb);
		if (ret < 0) {
			dev_err(&info->pdev->dev, "xmit err %d\n", ret);
			break;
		}
	}

	clear_bit(VETH_STATUS_BUSY, &info->status);
}

static int pcirc_ambaveth_napi(struct napi_struct *napi, int budget)
{
	int ret, rx_done = 0;
	struct pcirc_ambaveth_info *info =
		container_of(napi, struct pcirc_ambaveth_info, napi);

	do {
		ret = pcirc_ambaveth_xfer_from_ep(info, pcirc_ambaveth_recv_callback);
		if (ret < 0) {
			if (ret != -EBUSY) {
				dev_err(&info->pdev->dev, "xfer from ep error (%d)\n", ret);
				goto err_exit;
			}
		}
	} while(++rx_done < budget);

err_exit:
	if (rx_done < budget)
		napi_complete_done(&info->napi, rx_done);

	return rx_done;
}

static void pcirc_ambaveth_test_xmit(struct pcirc_ambaveth_info *info)
{
	int i;
	void *data = kmalloc(buf_size, GFP_KERNEL);

	memset(data, 0x5a, buf_size);
	for (i = 0; i < test_xmit; i++)
		pcirc_ambaveth_xfer_to_ep(info, data, buf_size);
}

static int pcirc_ambaveth_open(struct net_device *dev)
{
	int i;
	struct pcirc_ambaveth_info *info = netdev_priv(dev);
	struct pcirc_ambaveth_buffer *buf;
	pci_mailbox_t *mbar;
	pci_ambarella_ctrl_t *ctrl;

	/* Map buffer for read */
	buf = &info->buf_rx;
	mbar = info->buf_rx.mailbox_bar;
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		buf->phys[i] = dma_map_single(&info->pdev->dev, buf->virt[i],
					      buf->size, DMA_FROM_DEVICE);
		if (dma_mapping_error(&info->pdev->dev, buf->phys[i])) {
			dev_err(&info->pdev->dev, "Failed to map rx buffer addr\n");
			goto err_exit;
		}

		ctrl = (struct pci_ambarella_ctrl *)&mbar->ctrl[i];
		ctrl->command = TRANSFER_DATA_EP_TO_RC;
		ctrl->seq = ++ep2rc_seq;
		ctrl->addr_lo = lower_32_bits(buf->phys[i]);
		ctrl->addr_hi = upper_32_bits(buf->phys[i]);
		ctrl->size = buf->size;
		ctrl->flags = 0;
		ctrl->status = checksum ? STATUS_CHKSUM_EN : 0;
		ctrl->mrrs = info->mrrs;
		ctrl->rsvd = 0;
		ctrl->checksum = 0;
		dev_dbg(&info->pdev->dev, "ep2rc: %u Submit\n", ctrl->seq);
	}

	mbar->avail = PCIE_CTRL_NUM;		/* updated by RC */
	mbar->used = 0;				/* updated by EP */
	mbar->opcode = 1;
	buf->last_used = 0;

	/* Map buffer for write */
	buf = &info->buf_tx;
	mbar = info->buf_tx.mailbox_bar;
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		buf->phys[i] = dma_map_single(&info->pdev->dev, buf->virt[i],
					      buf->size, DMA_TO_DEVICE);
		if (dma_mapping_error(&info->pdev->dev, buf->phys[i])) {
			dev_err(&info->pdev->dev, "Failed to map tx buffer addr\n");
			goto err_unmap;
		}
	}

	mbar->avail = PCIE_CTRL_NUM;	/* updated by EP */
	mbar->used = 0;			/* updated by RC */
	mbar->opcode = 1;
	buf->last_avail = 0;

	pcirc_ambaveth_test_xmit(info);

	if (!napi_rx)
		schedule_work(&info->recv);
	else
		napi_enable(&info->napi);

	netif_start_queue(dev);

	return 0;

err_unmap:
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		dma_unmap_single(&info->pdev->dev, buf->phys[i], buf->size, DMA_FROM_DEVICE);
	}
err_exit:
	return -ENOMEM;
}

static int pcirc_ambaveth_stop(struct net_device *dev)
{
	int i;
	struct pcirc_ambaveth_info *info = netdev_priv(dev);
	struct pcirc_ambaveth_buffer *buf;

	if (napi_rx) {
		napi_disable(&info->napi);
	}

	netif_stop_queue(dev);

	buf = &info->buf_rx;
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		dma_unmap_single(&info->pdev->dev, buf->phys[i], buf->size, DMA_FROM_DEVICE);
	}

	buf = &info->buf_tx;
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		dma_unmap_single(&info->pdev->dev, buf->phys[i], buf->size, DMA_TO_DEVICE);
	}

	return 0;
}

static int pcirc_ambaveth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pcirc_ambaveth_info *info = netdev_priv(dev);

	if (test_and_set_bit(VETH_STATUS_BUSY, &info->status))
		return NETDEV_TX_BUSY;

	skb_queue_tail(&info->txq, skb);
	schedule_work(&info->xmit);

	return NETDEV_TX_OK;
}

static int pcirc_ambaveth_change_mtu(struct net_device *ndev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > ndev->max_mtu)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

static const struct net_device_ops pcirc_ambaveth_ops = {
	.ndo_open = pcirc_ambaveth_open,
	.ndo_stop = pcirc_ambaveth_stop,
	.ndo_start_xmit = pcirc_ambaveth_start_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_change_mtu = pcirc_ambaveth_change_mtu,
};

static int pcirc_ambaveth_probe(struct pci_dev *pdev,
					const struct pci_device_id *ent)
{
	struct pcirc_ambaveth_info *info;
	struct net_device *ndev;
	int i, err;

	if (pci_is_bridge(pdev))
		return -ENODEV;

	ndev = alloc_netdev(sizeof(*info), "pciveth0", NET_NAME_ENUM, ether_setup);
	if (!ndev)
		return -ENOMEM;

	info = netdev_priv(ndev);
	info->ndev = ndev;
	info->pdev = pdev;

	if ((dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(48)) != 0) &&
	    dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32)) != 0) {
		dev_err(&pdev->dev, "Cannot set DMA mask\n");
		err = -EINVAL;
		goto err_free_ndev;
	}

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Cannot enable PCI device\n");
		goto err_free_ndev;
	}

	err = pci_request_regions(pdev, DRV_MODULE_NAME);
	if (err) {
		dev_err(&pdev->dev, "Cannot obtain PCI resources\n");
		goto err_disable_pdev;
	}

	pci_set_master(pdev);

	if (pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_LEGACY) < 0) {
		err = -EINVAL;
		goto err_release_region;
	}

	if (pcie_set_readrq(pdev, mrrs)) {
		goto err_free_irqvec;
	}

	info->mrrs = pcie_get_readrq(pdev);

	info->ctrl_bar = pci_ioremap_bar(pdev, 0); /* bar 0 */
	if (!info->ctrl_bar) {
		dev_err(&pdev->dev, "Failed to ioremap BAR0\n");
		err = -ENOMEM;
		goto err_free_irqvec;
	}

#if 0	/* TODO */
	info->buf_tx.bar = info->ctrl_bar;
	info->buf_rx.bar = info->ctrl_bar + sizeof(struct pci_ambarella_ctrl);
#else
	info->buf_tx.mailbox_bar = info->ctrl_bar;
	info->buf_rx.mailbox_bar = info->ctrl_bar +
		sizeof(struct pci_mailbox);
#endif

	/* Allocate buffer for read and write */
	buf_size = round_down(buf_size, 4096);
	info->buf_rx.size = info->buf_tx.size = buf_size;

	for (i = 0; i < PCIE_CTRL_NUM; i ++) {
		info->buf_rx.virt[i] = kmalloc(buf_size, GFP_KERNEL);
		info->buf_tx.virt[i] = kmalloc(buf_size, GFP_KERNEL);
		if (!info->buf_rx.virt[i] || !info->buf_tx.virt[i]) {
			dev_err(&pdev->dev, "unable to allocate buffer\n");
			err = -ENOMEM;
			goto err_release_buffer;
		}
	}

	mutex_init(&info->buf_rx.mtx);
	mutex_init(&info->buf_tx.mtx);

	init_completion(&info->buf_rx.done);
	init_completion(&info->buf_tx.done);

	info->id = ida_simple_get(&pcirc_ambaveth_ida, 0, 0, GFP_KERNEL);
	if (info->id < 0) {
		err = info->id;
		dev_err(&pdev->dev, "Unable to get id\n");
		goto err_release_buffer;
	}

	err = devm_request_irq(&pdev->dev, pdev->irq, pcirc_ambaveth_irqhandler,
			       IRQF_SHARED, DRV_MODULE_NAME, info);
	if (err) {
		dev_err(&pdev->dev, "Failed to request IRQ %d for Legacy\n", pdev->irq);
		err = -EINVAL;
		goto err_ida_remove;
	}

	skb_queue_head_init(&info->txq);
	INIT_WORK(&info->xmit, pcirc_ambaveth_xmit_worker);
	if (!napi_rx) {
		INIT_WORK(&info->recv, pcirc_ambaveth_recv_worker);
	} else {
#if LINUX_VERSION_CODE < KERNEL_VERSION(5,18,0)
		netif_napi_add(info->ndev, &info->napi, pcirc_ambaveth_napi, PCIE_CTRL_NUM);
#else
		netif_napi_add(info->ndev, &info->napi, pcirc_ambaveth_napi);
#endif
	}
	info->ndev->netdev_ops = &pcirc_ambaveth_ops;
	info->ndev->max_mtu = buf_size;
	err = register_netdev(info->ndev);
	if (err) {
		dev_err(&pdev->dev, "failed to register net device\n");
		goto err_ida_remove;
	}

	pci_set_drvdata(pdev, info);
	dev_info(&pdev->dev, "checksum is %s\n", checksum ? "enabled" : "disabled");

	return 0;

err_ida_remove:
	ida_simple_remove(&pcirc_ambaveth_ida, info->id);
err_release_buffer:
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		if (info->buf_rx.virt[i])
			kfree(info->buf_rx.virt[i]);
		if (info->buf_tx.virt[i])
			kfree(info->buf_tx.virt[i]);
	}
	pci_iounmap(pdev, info->ctrl_bar);
err_free_irqvec:
	pci_free_irq_vectors(pdev);
err_release_region:
	pci_release_regions(pdev);
err_disable_pdev:
	pci_disable_device(pdev);
err_free_ndev:
	free_netdev(info->ndev);
	return err;
}

static void pcirc_ambaveth_remove(struct pci_dev *pdev)
{
	int i;
	struct pcirc_ambaveth_info *info = pci_get_drvdata(pdev);

	pci_disable_device(pdev);
	pci_iounmap(pdev, info->ctrl_bar);
	pci_release_regions(pdev);
	pci_free_irq_vectors(pdev);

	if (napi_rx) {
		netif_napi_del(&info->napi);
	}

	devm_free_irq(&pdev->dev, pdev->irq, info);
	unregister_netdev(info->ndev);
	for (i = 0; i < PCIE_CTRL_NUM; i++) {
		kfree(info->buf_rx.virt[i]);
		kfree(info->buf_tx.virt[i]);
	}
	free_netdev(info->ndev);
	ida_simple_remove(&pcirc_ambaveth_ida, info->id);
}

#define PCI_DEVICE_ID_AMBAEP 0x0500

static const struct pci_device_id pcirc_ambaveth_tbl[] = {
	{
		PCI_DEVICE(PCI_VENDOR_ID_CDNS, PCI_DEVICE_ID_AMBAEP),
	},
	{}
};
MODULE_DEVICE_TABLE(pci, pcirc_ambaveth_tbl);

static struct pci_driver pcirc_ambaveth_driver = {
	.name = DRV_MODULE_NAME,
	.id_table = pcirc_ambaveth_tbl,
	.probe = pcirc_ambaveth_probe,
	.remove = pcirc_ambaveth_remove,
	.sriov_configure = pci_sriov_configure_simple,
};
module_pci_driver(pcirc_ambaveth_driver);

MODULE_DESCRIPTION("PCI Endpoint(RC) Ambarella Virtual Ethernet Driver");
MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_LICENSE("GPL v2");

