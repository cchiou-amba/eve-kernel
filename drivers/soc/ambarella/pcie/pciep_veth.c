// SPDX-License-Identifier: GPL-2.0
/*
 * Test driver to test Ambarella PCIe data transfer
 *
 * Copyright (C) 2023 Ambarella.Inc
 */
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/pci-epc.h>
#include <linux/pci-epf.h>
#include <linux/uaccess.h>
#include <linux/etherdevice.h>
#include <soc/ambarella/pci.h>

static int epf_id = 0;
module_param(epf_id, int, 0644);
MODULE_PARM_DESC(epf_id, "The ID of EPF attached by this veth");

static unsigned int buf_size = SZ_1M;
module_param(buf_size, uint, 0644);
MODULE_PARM_DESC(buf_size, "The size of buffer used to transfer data");

static bool veth_perf = true;
module_param(veth_perf, bool, 0);
MODULE_PARM_DESC(veth_perf, "high performance xmit/recv");

#define DRV_MODULE_NAME		"pciep-vnet"

#define to_pciep_ambaveth(w)	container_of(w, struct pciep_ambaveth_info, w)

enum {
	VETH_STATUS_BUSY = 0,
	VETH_STATUS_ERR = 1,
};

struct pciep_ambaveth_buffer {
	struct mutex mtx;
	void *virt;
	dma_addr_t phys;
	size_t size;
};

struct pciep_ambaveth_info {
	struct pci_epf *epf;
	struct device *dma_dev;

	struct net_device *ndev;
	struct work_struct recv;
	struct work_struct xmit;
	struct sk_buff_head txq;
	unsigned long status;

	struct pciep_ambaveth_buffer buf_rx;
	struct pciep_ambaveth_buffer buf_tx;
	struct net_device_stats	stats;
};

struct pciep_ambaveth_info *pciep_veth_info;

static void pciep_ambaveth_recv_worker(struct work_struct *recv)
{
	struct pciep_ambaveth_info *info = to_pciep_ambaveth(recv);
	struct pciep_ambaveth_buffer *buf = &info->buf_rx;
	struct sk_buff *skb;
	int len;

	len = epf_ambarella_xfer_from_rc(info->epf, buf->virt, buf->phys, buf->size);
	if (len < 0) {
		pr_err("%s: xfer from rc error (%d)\n", __func__, len);
		goto err_exit;
	}

	skb = dev_alloc_skb(len + NET_IP_ALIGN);
	skb_put(skb, len);

	memcpy(skb->data, buf->virt, len);
	skb->dev = info->ndev;
	skb->protocol = eth_type_trans(skb, skb->dev);
	info->stats.rx_bytes += len;
	info->stats.rx_packets ++;

	netif_rx(skb);

err_exit:
	schedule_work(&info->recv);
}

static void pciep_ambaveth_xmit_worker(struct work_struct *xmit)
{
	int ret;
	struct pciep_ambaveth_info *info = to_pciep_ambaveth(xmit);
	struct pciep_ambaveth_buffer *buf = &info->buf_tx;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&info->txq))) {
		BUG_ON(skb->len > buf->size);

		if (veth_perf) {
			phys_addr_t phys;

			phys = dma_map_single(info->dma_dev, skb->data, skb->len, DMA_TO_DEVICE);
			ret = epf_ambarella_xfer_to_rc(info->epf, skb->data, phys, skb->len);
			dma_unmap_single(info->dma_dev, phys, skb->len, DMA_TO_DEVICE);
		} else {
			memcpy(buf->virt, skb->data, skb->len);
			dma_sync_single_for_device(info->dma_dev, buf->phys, skb->len, DMA_TO_DEVICE);
			ret = epf_ambarella_xfer_to_rc(info->epf, buf->virt, buf->phys, skb->len);
		}

		dev_kfree_skb(skb);
		if (ret < 0) {
			break;
		}
	}

	if (ret < 0) {
		set_bit(VETH_STATUS_ERR, &info->status);
	}

	clear_bit(VETH_STATUS_BUSY, &info->status);
}

static int pciep_ambaveth_open(struct net_device *dev)
{
	struct pciep_ambaveth_info *info = netdev_priv(dev);
	struct pciep_ambaveth_buffer *buf;

	info->epf = epf_ambarella_get(epf_id);
	if (!info->epf || !info->epf->epc) {
		pr_err("%s: Failed to get epf or epc\n", __func__);
		return -ENODEV;
	}

	info->dma_dev =  info->epf->epc->dev.parent;

	/* Map buffer for read */
	buf = &info->buf_rx;
	buf->phys = dma_map_single(info->dma_dev, buf->virt, buf->size, DMA_FROM_DEVICE);
	if (dma_mapping_error(info->dma_dev, buf->phys)) {
		pr_err("%s: Failed to map rx buffer\n", __func__);
		goto err_exit;
	}

	/* Map buffer for write */
	buf = &info->buf_tx;
	buf->phys = dma_map_single(info->dma_dev, buf->virt, buf->size, DMA_TO_DEVICE);
	if (dma_mapping_error(info->dma_dev, buf->phys)) {
		pr_err("%s: Failed to map tx buffer\n", __func__);
		goto err_unmap;
	}

	schedule_work(&info->recv);

	netif_start_queue(dev);

	return 0;

err_unmap:
	buf = &info->buf_rx;
	dma_unmap_single(info->dma_dev, buf->phys, buf->size, DMA_FROM_DEVICE);
err_exit:
	epf_ambarella_put(info->epf);
	info->epf = NULL;
	info->dma_dev = NULL;
	return -ENOMEM;
}

static int pciep_ambaveth_stop(struct net_device *dev)
{
	struct pciep_ambaveth_info *info = netdev_priv(dev);
	struct pciep_ambaveth_buffer *buf;

	netif_stop_queue(dev);

	buf = &info->buf_rx;
	dma_unmap_single(info->dma_dev, buf->phys, buf->size, DMA_FROM_DEVICE);

	buf = &info->buf_tx;
	dma_unmap_single(info->dma_dev, buf->phys, buf->size, DMA_TO_DEVICE);

	epf_ambarella_put(info->epf);
	info->epf = NULL;
	info->dma_dev = NULL;

	return 0;
}

static int pciep_ambaveth_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct pciep_ambaveth_info *info = netdev_priv(dev);

	info->stats.tx_packets ++;

	if (test_bit(VETH_STATUS_ERR, &info->status)) {
		netif_carrier_off(dev);
		return NETDEV_TX_BUSY;
	}

	if (test_and_set_bit(VETH_STATUS_BUSY, &info->status)) {
		return NETDEV_TX_BUSY;
	}

	info->stats.tx_bytes += skb->len;

	skb_queue_tail(&info->txq, skb);
	schedule_work(&info->xmit);

	return NETDEV_TX_OK;
}

static struct net_device_stats *pciep_get_stats(struct net_device *dev)
{
	struct pciep_ambaveth_info *info = netdev_priv(dev);

	return &info->stats;
}

static int pciep_ambaveth_change_mtu(struct net_device *ndev, int new_mtu)
{
	if (new_mtu < 68 || new_mtu > ndev->max_mtu)
		return -EINVAL;

	ndev->mtu = new_mtu;

	return 0;
}

static const struct net_device_ops pciep_ambaveth_ops = {
	.ndo_open = pciep_ambaveth_open,
	.ndo_stop = pciep_ambaveth_stop,
	.ndo_start_xmit = pciep_ambaveth_start_xmit,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = pciep_ambaveth_change_mtu,
	.ndo_get_stats = pciep_get_stats,
};

static int __init pciep_ambaveth_init(void)
{
	struct pciep_ambaveth_info *info;
	struct net_device *ndev;
	int ret;

	ndev = alloc_netdev(sizeof(*info), "pciveth1", NET_NAME_ENUM, ether_setup);
	if (!ndev)
		return -ENOMEM;

	info = netdev_priv(ndev);
	info->ndev = ndev;

	/* Allocate buffer for read and write */
	buf_size = round_down(buf_size, 4096);
	info->buf_rx.size = info->buf_tx.size = buf_size;

	info->buf_rx.virt = kmalloc(buf_size, GFP_KERNEL);
	info->buf_tx.virt = kmalloc(buf_size, GFP_KERNEL);
	if (!info->buf_rx.virt || !info->buf_tx.virt) {
		pr_err("%s: unable to allocate buffer\n", __func__);
		ret = -ENOMEM;
		goto err_exit;
	}

	skb_queue_head_init(&info->txq);
	mutex_init(&info->buf_rx.mtx);
	mutex_init(&info->buf_tx.mtx);

	INIT_WORK(&info->recv, pciep_ambaveth_recv_worker);
	INIT_WORK(&info->xmit, pciep_ambaveth_xmit_worker);

	info->ndev->netdev_ops = &pciep_ambaveth_ops;
	info->ndev->max_mtu = buf_size;
	ret = register_netdev(info->ndev);
	if (ret) {
		pr_err("%s: failed to register net device\n", DRV_MODULE_NAME);
		goto err_exit;
	}

	pciep_veth_info = info;

	return 0;

err_exit:
	if (info->buf_rx.virt)
		kfree(info->buf_rx.virt);
	if (info->buf_tx.virt)
		kfree(info->buf_tx.virt);
	if (info->ndev)
		free_netdev(info->ndev);
	return ret;
}

static void __exit pciep_ambaveth_exit(void)
{
	struct pciep_ambaveth_info *info = pciep_veth_info;

	unregister_netdev(info->ndev);
	kfree(info->buf_rx.virt);
	kfree(info->buf_tx.virt);
	free_netdev(info->ndev);
	pciep_veth_info = NULL;
}

module_init(pciep_ambaveth_init);
module_exit(pciep_ambaveth_exit);

MODULE_DESCRIPTION("PCI EPF Ambarella Virtual Ethernet Driver");
MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_LICENSE("GPL v2");

