/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Junfeng Shen <jfshen@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
 */

#ifndef __SOC_AMBARELLA_FB_H__
#define __SOC_AMBARELLA_FB_H__

#include <linux/fb.h>

struct ambfb_format {
	const char *name;
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 fourcc;
};

struct ambfb_par {
	u32 vout_id; /* must be put at first */
	u32 fb_id;
	struct device *dev;
	atomic_t use_count;
	u32 max_width;
	u32 max_height;
	struct ambfb_format *format;
};

#endif /* __SOC_AMBARELLA_FB_H__ */
