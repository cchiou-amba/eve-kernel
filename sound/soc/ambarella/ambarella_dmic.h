/*
 * sound/soc/ambarella_i2s.h
 *
 * History:
 *	2016/07/13 - [XianqingZheng] created file
 *
 * Copyright (C) 2004-2009, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#ifndef AMBARELLA_DMIC_H_
#define AMBARELLA_DMIC_H_

#define DMIC_ENABLE_OFFSET			0x00
#define AUDIO_CODEC_DP_RESET_OFFSET		0x04
#define DECIMATION_FACTOR_OFFSET		0x08
#define DMIC_STATAUS_OFFSET			0x0c
#define I2S_WPOS_OFFSET				0x10
#define CIC_MUTIPLIER_OFFSET			0x14
#define DMIC_CLK_DIV_OFFSET			0x100
#define DMIC_DATA_PHASE_OFFSET			0x104
#define DMIC_CLK_ENABLE_OFFSET			0x108
#define WIND_NS_FL_GM_CTRL_OFFSET		0x10C
#define WIND_NS_FL_CTRL_OFFSET			0x110
#define DMIC_I2S_CTRL_OFFSET			0x114
#define DEBUG_STATUS0_OFFSET			0x118
#define DEBUG_STATUS1_OFFSET			0x11C
#define DEBUG_STATUS2_OFFSET			0x120
/* Per-PDM data phase (PDM0 @ 0x104; PDM1..3 @ 0x124/128/12c, same fields) */
#define DMIC_DATA_PHASE_PDM0_OFFSET		DMIC_DATA_PHASE_OFFSET
#define DMIC_DATA_PHASE_PDM1_OFFSET		0x124
#define DMIC_DATA_PHASE_PDM2_OFFSET		0x128
#define DMIC_DATA_PHASE_PDM3_OFFSET		0x12C
#define DMIC_CHANNEL_NUM_OFFSET			0x130
#define DMIC_PDM_CHANNEL_SELECT_OFFSET		0x134
#define DROOP_CP_FL_COFT0_OFFSET		0x200
#define DROOP_CP_FL_COFT63_OFFSET		0x2FC
#define HALF_BD_FL_COFT0_OFFSET			0x300
#define HALF_BD_FL_COFT63_OFFSET		0x3FC
#define WIND_NS_FL_HPF_COFT0_OFFSET		0x400
#define WIND_NS_FL_HPF_COFT67_OFFSET		0x50C
#define WIND_NS_FL_LPF_COFT0_OFFSET		0x510
#define WIND_NS_FL_LPF_COFT67_OFFSET		0x61C
#define WIND_NS_FL_WND_COFT0_OFFSET		0x620
#define WIND_NS_FL_WND_COFT16_OFFSET		0x660
#define WIND_NS_FL_VOC_HPF_COFT0_OFFSET		0x664
#define WIND_NS_FL_VOC_HPF_COFT16_OFFSET	0x6A4
#define WIND_NS_FL_VOC_LPF_COFT0_OFFSET		0x6A8
#define WIND_NS_FL_VOC_LPF_COFT10_OFFSET	0x6D0

/*Channel Active Number */
#define DMIC_ACTIVE_CHANNELS_2CH			0x0
#define DMIC_ACTIVE_CHANNELS_4CH			0x1
#define DMIC_ACTIVE_CHANNELS_6CH			0x2
#define DMIC_ACTIVE_CHANNELS_8CH			0x3

/* DMIC_PDM_CHANNEL_SELECT (0x134): pdmX -> sdY, 2 bits per PDM lane */
#define DMIC_PDM_CH_SEL_MASK			0x3
#define DMIC_PDM0_CH_SEL_SHIFT			0
#define DMIC_PDM1_CH_SEL_SHIFT			2
#define DMIC_PDM2_CH_SEL_SHIFT			4
#define DMIC_PDM3_CH_SEL_SHIFT			6

/* S5L */
#define DMIC_I2S_SEL_MASK_V1			(1 << 6)
#define DMIC_I2S_SEL_OFFSET_V1			(0x0C)

/*CV2x S6LM */
#define DMIC_I2S_SEL_MASK_V2			(1 << 24)
#define DMIC_I2S_SEL_OFFSET_V2			(0x60)

/*CV5 CV3 CV7x CV8 */
#define DMIC_I2S_SEL_MASK_V3			(1 << 20)
#define DMIC_I2S_SEL_OFFSET_V3			(0x60)

#define DMIC_MAX_PDM				4
#define DMIC_CHANNELS_PER_PDM			2

struct amb_dmic_pdata {
	u32 dmic_sel_offset;
	u32 dmic_sel_mask;
};

struct amb_dmic_priv {
	void __iomem 		*regbase;
	struct regmap		*reg_scr;
	u32 			mclk;
	u32				custom_iir_support;
	u32				dmic_sel_offset;
	u32				dmic_sel_mask;
	u32				num_pdm;
	u32				channels;
	u32				pdm_channel_select;
};

#endif /*AMBARELLA_DMIC_H_*/

