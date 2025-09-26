/* SPDX-License-Identifier: GPL-2.0+ */
/*
 * Copyright (C) 2016-2048, Ambarella, Inc.
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *
 */


#ifndef __SOC_AMBARELLA_MISC_H__
#define __SOC_AMBARELLA_MISC_H__
#include <linux/io.h>

extern void memcpy_toio_fixup(volatile void __iomem *to, const void *from, long count);
extern void memcpy_fromio_fixup(void *to, const volatile void __iomem *from, size_t count);

extern unsigned int ambarella_sys_config(void);
extern struct proc_dir_entry *ambarella_procfs_dir(void);
extern struct dentry *ambarella_debugfs_dir(void);

#endif

