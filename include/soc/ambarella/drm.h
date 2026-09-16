
/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Junfeng Shen <jfshen@ambarella.com>
 *
 * Copyright (C) 2004-2010, Ambarella, Inc.
 */

#ifndef __SOC_AMBARELLA_DRM_H
#define __SOC_AMBARELLA_DRM_H

#include <drm/drm_encoder.h>
#include <drm/drm_fb_helper.h>

/* memory type definitions. */
enum e_ambdrm_gem_mem_type {
	/* Physically Continuous memory and used as default. */
	AMBDRM_BO_CONTIG	= 0 << 0,
	/* Physically Non-Continuous memory. */
	AMBDRM_BO_NONCONTIG	= 1 << 0,
	/* non-cachable mapping and used as default. */
	AMBDRM_BO_NONCACHABLE	= 0 << 1,
	/* cachable mapping. */
	AMBDRM_BO_CACHABLE	= 1 << 1,
	/* write-combine mapping. */
	AMBDRM_BO_WC		= 1 << 2,
	AMBDRM_BO_MASK		= AMBDRM_BO_NONCONTIG | AMBDRM_BO_CACHABLE |
					AMBDRM_BO_WC
};

/**
 * User-desired buffer creation information structure.
 *
 * @size: user-desired memory allocation size.
 *	- this size value would be page-aligned internally.
 * @flags: user request for setting memory type or cache attributes.
 * @handle: returned a handle to created gem object.
 *	- this handle will be set by gem module of kernel side.
 */
struct ambdrm_gem_create {
	__u64 size;
	__u32 flags;
	__u32 handle;
};

/**
 * A structure for getting a fake-offset that can be used with mmap.
 *
 * @handle: handle of gem object.
 * @reserved: just padding to be 64-bit aligned.
 * @offset: a fake-offset of gem object.
 */
struct ambdrm_gem_map {
	__u32 handle;
	__u32 reserved;
	__u64 offset;
};

/**
 * A structure to gem information.
 *
 * @handle: a handle to gem object created.
 * @flags: flag value including memory type and cache attribute and
 *	this value would be set by driver.
 * @size: size to memory region allocated by gem and this size would
 *	be set by driver.
 */
struct ambdrm_gem_info {
	__u32 handle;
	__u32 flags;
	__u64 size;
};

struct ambdrm_fb_format {
	const char *name;
	u32 bits_per_pixel;
	struct fb_bitfield red;
	struct fb_bitfield green;
	struct fb_bitfield blue;
	struct fb_bitfield transp;
	u32 fourcc;
};

struct ambdrm_fb_par {
	u32 vout_id; /* must be put at first */
	u32 fb_id;
	u32 vc_id; /* virtual channel id */
	struct drm_device *drm;
	atomic_t use_count;
	u32 width;
	u32 height;
	u32 stride;
	u32 buffernum;
	bool alloc_buf;
	struct ambdrm_fb_format *format;
};

/*
 * Ambarella DRM Framebuffer device
 */
struct ambdrm_device {
	struct drm_device dev;

	/* ambdrm_fb settings */
	struct drm_display_mode mode;
	const struct drm_format_info *format;
	unsigned int pitch;

#if 0
	/* memory management */
	struct iosys_map screen_base;
#endif

	/* modesetting */
	uint32_t formats[8];
	size_t nformats;
	struct drm_plane primary_plane;
	struct drm_crtc crtc;
	struct drm_encoder encoder;
	struct drm_connector connector;
	struct ambdrm_fb_par par;
};

static inline struct ambdrm_device *to_ambdrm_device(struct drm_device *dev)
{
	return container_of(dev, struct ambdrm_device, dev);
}

#endif /* __SOC_AMBARELLA_DRM_H__ */
