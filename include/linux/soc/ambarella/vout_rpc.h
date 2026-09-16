/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __LINUX_SOC_AMBARELLA_VOUT_RPC_H
#define __LINUX_SOC_AMBARELLA_VOUT_RPC_H

#include <linux/bits.h>
#include <linux/module.h>
#include <linux/types.h>

#define AMB_VOUT_MAX_CONTROLLERS	3U
#define AMB_VOUT_MAX_VIRTUAL_CHANNELS	4U
#define AMB_VOUT_CLUT_SIZE		256U
#define AMB_VOUT_MAX_MODES		256U

#define AMB_VOUT_MODE_FLAG_PHSYNC	BIT(0)
#define AMB_VOUT_MODE_FLAG_NHSYNC	BIT(1)
#define AMB_VOUT_MODE_FLAG_PVSYNC	BIT(2)
#define AMB_VOUT_MODE_FLAG_NVSYNC	BIT(3)
#define AMB_VOUT_MODE_FLAG_INTERLACE	BIT(4)

struct amb_vout_target {
	u32 vout_id;
	u32 virtual_id;
};

struct amb_vout_mode {
	u32 clock_khz;
	u32 hdisplay;
	u32 hsync_start;
	u32 hsync_end;
	u32 htotal;
	u32 vdisplay;
	u32 vsync_start;
	u32 vsync_end;
	u32 vtotal;
	u32 width_mm;
	u32 height_mm;
	u32 vrefresh_millihz;
	u32 flags;
	u8 preferred;
};

struct amb_vout_osd_config {
	struct amb_vout_target target;
	u64 dma_addr;
	u64 size;
	u32 fourcc;
	u32 bits_per_pixel;
	u32 pitch;
	u32 width;
	u32 height;
	u32 virtual_width;
	u32 virtual_height;
	u32 xoffset;
	u32 yoffset;
	u32 src_x;
	u32 src_y;
	u32 dst_x;
	u32 dst_y;
	u32 dst_width;
	u32 dst_height;
	u8 use_buffer_pool;
	u32 dsp_pts;
};

struct amb_vout_clut {
	u32 start;
	u32 len;
	u8 red[AMB_VOUT_CLUT_SIZE];
	u8 green[AMB_VOUT_CLUT_SIZE];
	u8 blue[AMB_VOUT_CLUT_SIZE];
	u8 alpha[AMB_VOUT_CLUT_SIZE];
};

struct amb_vout_buffer_pool_info {
	u32 pitch;
	u32 height;
	u8 ready;
};

struct amb_vout_buffer_release {
	u8 slot;
	u8 had_displaying;
};

struct amb_vout_buffer_retire {
	u8 released_count;
	u8 kept_slot;
	u8 found_reported;
	u8 had_displaying;
};

struct amb_vout_provider_ops {
	int (*open)(void *priv, const struct amb_vout_osd_config *config);
	int (*release)(void *priv);
	int (*check_osd)(void *priv, const struct amb_vout_osd_config *config);
	int (*set_osd)(void *priv, const struct amb_vout_osd_config *config);
	int (*pan_osd)(void *priv, const struct amb_vout_osd_config *config);
	int (*setup_osd)(void *priv, const struct amb_vout_osd_config *config);
	int (*set_clut)(void *priv, const struct amb_vout_clut *clut);
	int (*get_modes)(void *priv, struct amb_vout_mode *modes,
			 u32 capacity, u32 *count);
	int (*set_mode)(void *priv, const struct amb_vout_mode *mode);
	int (*check_started)(void *priv);
};

struct amb_vout_buffer_ops {
	int (*get_pool_info)(void *priv, struct amb_vout_buffer_pool_info *info);
	int (*acquire)(void *priv, u8 *slot, u64 *dma_addr);
	int (*drop_copy)(void *priv, u8 slot);
	int (*copy_done)(void *priv, u8 slot);
	int (*pick_ready)(void *priv, u8 *slot, u64 *dma_addr);
	int (*release_by_dma)(void *priv, u64 dma_addr,
			      struct amb_vout_buffer_release *result);
	/*
	 * Recycle every in-flight GEM strictly before @reported_dma in submit
	 * order; keep @reported_dma DISPLAYING. Used when osd_buf_daddr advances
	 * so silently dropped intermediate addresses can be freed too.
	 */
	int (*retire_before_dma)(void *priv, u64 reported_dma,
				 struct amb_vout_buffer_retire *result);
};

struct amb_vout_consumer_ops {
	void (*provider_changed)(void *priv, bool available);
};

/*
 * Registration is exclusive per (vout_id, virtual_id). The RPC layer pins a
 * non-NULL owner around every callback. Unregister removes the endpoint first
 * and waits for callbacks already in flight, so priv and ops may be freed as
 * soon as unregister returns. DTO pointers are valid only for the duration of
 * the synchronous call. Buffer release can be dispatched from hard-IRQ
 * context; buffer callbacks must therefore not sleep.
 */
int amb_vout_provider_register(const struct amb_vout_target *target,
			       const struct amb_vout_provider_ops *ops,
			       void *priv, struct module *owner);
void amb_vout_provider_unregister(const struct amb_vout_target *target,
				  const struct amb_vout_provider_ops *ops,
				  void *priv);

int amb_vout_buffer_register(const struct amb_vout_target *target,
			     const struct amb_vout_buffer_ops *ops,
			     void *priv, struct module *owner);
void amb_vout_buffer_unregister(const struct amb_vout_target *target,
				const struct amb_vout_buffer_ops *ops,
				void *priv);

int amb_vout_consumer_register(const struct amb_vout_target *target,
			       const struct amb_vout_consumer_ops *ops,
			       void *priv, struct module *owner);
void amb_vout_consumer_unregister(const struct amb_vout_target *target,
				  const struct amb_vout_consumer_ops *ops,
				  void *priv);

int amb_vout_open(const struct amb_vout_osd_config *config);
int amb_vout_release(const struct amb_vout_target *target);
int amb_vout_check_osd(const struct amb_vout_osd_config *config);
int amb_vout_set_osd(const struct amb_vout_osd_config *config);
int amb_vout_pan_osd(const struct amb_vout_osd_config *config);
int amb_vout_setup_osd(const struct amb_vout_osd_config *config);
int amb_vout_set_clut(const struct amb_vout_target *target,
		      const struct amb_vout_clut *clut);
int amb_vout_get_modes(const struct amb_vout_target *target,
		       struct amb_vout_mode *modes, u32 capacity, u32 *count);
int amb_vout_set_mode(const struct amb_vout_target *target,
		      const struct amb_vout_mode *mode);
int amb_vout_check_started(const struct amb_vout_target *target);

int amb_vout_buffer_get_pool_info(const struct amb_vout_target *target,
				  struct amb_vout_buffer_pool_info *info);
int amb_vout_buffer_acquire(const struct amb_vout_target *target,
			    u8 *slot, u64 *dma_addr);
int amb_vout_buffer_drop_copy(const struct amb_vout_target *target, u8 slot);
int amb_vout_buffer_copy_done(const struct amb_vout_target *target, u8 slot);
int amb_vout_buffer_pick_ready(const struct amb_vout_target *target,
			       u8 *slot, u64 *dma_addr);
int amb_vout_buffer_release_by_dma(const struct amb_vout_target *target,
				   u64 dma_addr,
				   struct amb_vout_buffer_release *result);
int amb_vout_buffer_retire_before_dma(const struct amb_vout_target *target,
				      u64 reported_dma,
				      struct amb_vout_buffer_retire *result);

#endif /* __LINUX_SOC_AMBARELLA_VOUT_RPC_H */
