/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PCIe utility header, used by various Ambarella PCIe drivers.
 * Copyright (C) 2023 by Ambarella, Inc.
 */

#include <linux/dmaengine.h>
#include <linux/pci-epf.h>

#ifndef __SOC_AMBARELLA_PCI_H__
#define __SOC_AMBARELLA_PCI_H__

#define TRANSFER_DATA_EP_TO_RC		BIT(0)
#define TRANSFER_DATA_RC_TO_EP		BIT(1)

#define STATUS_IRQ_RAISED		BIT(0)
#define STATUS_SUCCESS			BIT(1)
#define STATUS_FAIL			BIT(2)
#define STATUS_CHKSUM_EN		BIT(3)
#define STATUS_UNKNOWN_COMMAND		BIT(4)

#define PCIE_OPCODE_START		1
#define PCIE_CTRL_NUM			64
#define PCIE_MAILBOX			1

struct pci_ambarella_ctrl {
	u32 command;
	u32 seq;
	union {
		struct {
			u64 addr;
		};
		struct {
			u32 addr_lo;
			u32 addr_hi;
		};
	};
	u32 size;
	u32 flags;
	u32 status;
	u32 mrrs;
	u32 rsvd;
	u32 checksum;
} __packed;

typedef volatile struct pci_ambarella_ctrl pci_ambarella_ctrl_t;

struct pci_mailbox {
	u32 opcode;
	u32 avail;
	u32 used;
	struct pci_ambarella_ctrl ctrl[PCIE_CTRL_NUM];
}__packed;

typedef volatile struct pci_mailbox pci_mailbox_t;

extern struct pci_epf *epf_ambarella_get(int nr);
extern void epf_ambarella_put(struct pci_epf *epf);
extern int epf_ambarella_xfer_from_rc(struct pci_epf *epf, void *buf_virt, dma_addr_t buf_phys, size_t len);
extern int epf_ambarella_xfer_to_rc(struct pci_epf *epf, void *buf_virt, dma_addr_t buf_phys, size_t len);
extern int epf_ambarella_xfer_by_dma(struct pci_epf *epf, void *buf_virt, dma_addr_t buf_phys, size_t len,
				     enum dma_transfer_direction dir);

#endif
