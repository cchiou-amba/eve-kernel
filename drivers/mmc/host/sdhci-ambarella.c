/*
 * /drivers/mmc/host/sdhci-ambarella.c
 *
 * Copyright (C) 2004-2099, Ambarella, Inc.
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

#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/sizes.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>
#include <linux/mmc/mmc.h>
#include <soc/ambarella/misc.h>
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/mmc/slot-gpio.h>

#include "sdhci-pltfm.h"

/* MSHC specific Mode Select value */
#define MSHC_CTRL_HS400		0x7

/* DWC IP vendor area 1 pointer */
#define MSHC_P_VENDOR_AREA		0xe8
#define MSHC_AREA_MASK		GENMASK(11, 0)

/* Offset inside the vendor specific area */
#define MSHC_CTRL_R			0x8
#define NEGEDGE_DATAOUT_EN		BIT(1)
#define CMD_CONFLICT_CHECK		BIT(0)

#define MSHC_EMMC_CONTROL		0x2c
#define MSHC_ENHANCED_STROBE		BIT(8)
#define MSHC_CARD_IS_EMMC		BIT(0)

#define DDL_TX_BYPASS		BIT(5)
#define DDL_STB_BYPASS		BIT(13)
#define DDL_TX_SEL_DEFAULT	0x1F
/* Also can use 0x6 as data strobe default */
#define DDL_STB_SEL_DEFAULT	0x7

#define BOUNDARY_OK(addr, len) \
	((addr | (SZ_128M - 1)) == ((addr + len - 1) | (SZ_128M - 1)))

#define SD_IOMUX_CTRL_SET_OFFSET 0xf0

#define AT_CTRL_R (0x540)
#define AT_STAT_R (0x544)

#define RETUNE_ALL_PASS_RANGE_TIMES 3

struct sdhci_ambarella_host {
	struct gpio_desc *power_gpio;
	struct gpio_desc *v18_gpio;
	int vendor_specific_area;
	struct regmap *scr_regmap;
	struct regmap *rct_regmap;
	struct regmap_field *ds_ctrl[4];
	struct regmap *sd_iomux_regmap;
	u32 sd_iomux_val;
	u32 sd_ddl_ctrl_offset;
	u32 ddl_tx_sel;
	u32 ddl_stb_sel;
	u32 support_invert;
	u32 invert_bit;
	u32 timing;
	u32 fixed_phase;
	u32 all_pass_range_fixed_phase;
	bool sw_tuning;
	bool retune_all_pass_range;
};
static u32 g_sd_iomux = 0;
static struct dentry *sdiomux_debugfs_file;

static void sdhci_ambarella_set_clock(struct sdhci_host *host, unsigned int clock)
{
	u32 sd_clk, clk_ctrl_r;
	u16 clk;
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);


	host->mmc->actual_clock = 0;

	sdhci_writew(host, 0, SDHCI_CLOCK_CONTROL);

	if (clock == 0)
		return;

	sd_clk = min_t(u32, clock, host->mmc->f_max);
	clk_set_rate(pltfm_host->clk, sd_clk);

	clk = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	sdhci_enable_clk(host, clk);

	if(ambarella_host->timing != host->timing) {
		/* Reset SD Clock Enable */
		clk_ctrl_r = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
		clk_ctrl_r &= ~SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);

		if (host->timing == MMC_TIMING_LEGACY ||
			 host->timing == MMC_TIMING_MMC_HS ||
			 host->timing == MMC_TIMING_SD_HS) {
			if (ambarella_host->support_invert) {
				regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
					1 << ambarella_host->invert_bit, 1 << ambarella_host->invert_bit);
				regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
					0x3f, 0);
			} else {
				regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
					0x3f, ambarella_host->ddl_tx_sel);
			}
		} else {
			regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
				0x3f, DDL_TX_BYPASS);
			regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
				1 << ambarella_host->invert_bit, 0);
		}

		ambarella_host->timing = host->timing;
		udelay(1);
		clk_ctrl_r |= SDHCI_CLOCK_CARD_EN;
		sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);
	}
}

static void sdhci_ambarella_set_power(struct sdhci_host *host, unsigned char mode,
		     unsigned short vdd)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	switch (mode) {
	case MMC_POWER_ON:
		break;
	case MMC_POWER_OFF:
		if(ambarella_host->power_gpio)
			gpiod_set_value_cansleep(ambarella_host->power_gpio, 0);
		break;
	case MMC_POWER_UP:
		if(ambarella_host->power_gpio)
			gpiod_set_value_cansleep(ambarella_host->power_gpio, 1);
		break;
	}

	sdhci_set_power_noreg(host, mode, vdd);
}

static int sdhci_ambarella_voltage_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	switch (ios->signal_voltage) {
	case MMC_SIGNAL_VOLTAGE_180:
		if(ambarella_host->v18_gpio)
			gpiod_set_value_cansleep(ambarella_host->v18_gpio, 1);
		break;

	case MMC_SIGNAL_VOLTAGE_330:
		if(ambarella_host->v18_gpio)
			gpiod_set_value_cansleep(ambarella_host->v18_gpio, 0);
		break;
	default:
		return -EINVAL;
	}

	return sdhci_start_signal_voltage_switch(mmc, ios);
}

static void sdhci_ambarella_hs400_enhanced_strobe(struct mmc_host *mmc,
					  struct mmc_ios *ios)
{
	u32 vendor, regval;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	vendor = sdhci_readl(host, ambarella_host->vendor_specific_area + MSHC_EMMC_CONTROL);
	if (ios->enhanced_strobe)
		vendor |= MSHC_ENHANCED_STROBE;
	else
		vendor &= ~MSHC_ENHANCED_STROBE;
	sdhci_writel(host, vendor | MSHC_CARD_IS_EMMC,
		ambarella_host->vendor_specific_area + MSHC_EMMC_CONTROL);

	regval = sdhci_readl(host, ambarella_host->vendor_specific_area + MSHC_CTRL_R);
	regval |= NEGEDGE_DATAOUT_EN;
	sdhci_writel(host, regval, ambarella_host->vendor_specific_area + MSHC_CTRL_R);

	regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
		0x3f << 8, ambarella_host->ddl_stb_sel << 8);
}

static int sdhci_ambarella_hs400_prepare_ddr(struct mmc_host *mmc)
{
	u32 vendor, regval;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	vendor = sdhci_readl(host, ambarella_host->vendor_specific_area + MSHC_EMMC_CONTROL);
	sdhci_writel(host, vendor | MSHC_CARD_IS_EMMC,
		ambarella_host->vendor_specific_area + MSHC_EMMC_CONTROL);

	regval = sdhci_readl(host, ambarella_host->vendor_specific_area + MSHC_CTRL_R);
	regval |= NEGEDGE_DATAOUT_EN;
	sdhci_writel(host, regval, ambarella_host->vendor_specific_area + MSHC_CTRL_R);

	regmap_update_bits(ambarella_host->scr_regmap, ambarella_host->sd_ddl_ctrl_offset,
		0x3f << 8, ambarella_host->ddl_stb_sel << 8);

	return 0;
}

static int wait_for_tran_state(struct mmc_host *host)
{
	int err, ready = 0;
	u32 status;
	ktime_t timeout;
	struct mmc_command cmd = {};

	timeout = ktime_add_ms(ktime_get(), 50);
	do {
		if(!ready)
			usleep_range(10, 20);
		cmd.opcode = MMC_SEND_STATUS;
		cmd.arg = 1 << 16;
		cmd.flags = MMC_RSP_SPI_R2 | MMC_RSP_R1 | MMC_CMD_AC;
		err = mmc_wait_for_cmd(host, &cmd, 3);
		if (err) {
			pr_err("send status error %d\n", err);
			return err;
		}
		status = cmd.resp[0];
		ready = mmc_ready_for_data(status);

		if(ktime_after(ktime_get(), timeout)) {
			pr_err("bad, wait tran state timeout\n");
			break;
		}
	} while(!ready);

	return 0;
}

static int sdhci_ambarella_all_pass_range(struct sdhci_host *host)
{
	u32 at_stat_r, l_edge_phase, r_edge_phase;

	at_stat_r = sdhci_readl(host, AT_STAT_R);
	l_edge_phase = (at_stat_r >> 16) & 0xff;
	r_edge_phase = (at_stat_r >> 8) & 0xff;
	if ((l_edge_phase == r_edge_phase + 1) || (l_edge_phase == 0 && r_edge_phase == 0x1f))
		return 1;
	else
		return 0;
}

static void sdhci_ambarella_set_fixed_center_phase(struct sdhci_host *host, int center_phase)
{
	u32 clk_ctrl_r;

	/* turn off sampling clock */
	clk_ctrl_r = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl_r &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);

	sdhci_writew(host, BIT(4), AT_CTRL_R);
	sdhci_writew(host, center_phase, AT_STAT_R);

	/* turn on sampling clock */
	clk_ctrl_r |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);
}

static int sdhci_ambarella_fixed_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	 sdhci_ambarella_set_fixed_center_phase(host, ambarella_host->fixed_phase);

	return 0;
}

static int sdhci_ambarella_sw_tuning(struct mmc_host *mmc, u32 opcode)
{
	u32 clk_ctrl_r, retune_all_pass_range_times = RETUNE_ALL_PASS_RANGE_TIMES, retune = 0;
	u32 left_edge_phase,right_edge_phase, max_range, center_phase, range;
	u8 result[32];
	int rc, phase, phase_0_range, phase_31_range;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	if ((clk_get_rate(pltfm_host->clk) > 150000000) || (ambarella_host->retune_all_pass_range))
		retune = 1;

	/* turn off sampling clock */
	clk_ctrl_r = sdhci_readw(host, SDHCI_CLOCK_CONTROL);
	clk_ctrl_r &= ~SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);

	/* Enable software tuning */
	sdhci_writel(host, BIT(4),  AT_CTRL_R);
	/* set center ph code to 0 */
	sdhci_writel(host, 0x0,  AT_STAT_R);

	/* turn on sampling clock */
	clk_ctrl_r |= SDHCI_CLOCK_CARD_EN;
	sdhci_writew(host, clk_ctrl_r, SDHCI_CLOCK_CONTROL);

sw_tuning_start:
	phase_0_range = phase_31_range = 0;
	for (phase = 0; phase < 32; phase++) {
		sdhci_writel(host, phase, AT_STAT_R);
		rc = mmc_send_tuning(mmc, opcode, NULL);
		result[phase] = rc ? 0 : 1;
		pr_debug("%s: phase=%d, %d\n", mmc_hostname(mmc), phase, rc);
	}

	if (result[0]) {
		phase_0_range++;
		for (phase = 1; phase < 32; phase++) {
			if (result[phase])
				phase_0_range++;
			else
				break;
		}
	}

	/* this case means all phase pass */
	if (phase_0_range == 32) {
		left_edge_phase = 0;
		right_edge_phase = 31;
		max_range = 32;
		center_phase = 0x10;
		if (retune && --retune_all_pass_range_times > 0) {
			pr_debug("%s: sw all pass range retry\n", mmc_hostname(host->mmc));
			goto sw_tuning_start;
		} else
			goto set_center_phase;
	}

	if (result[31]) {
		phase_31_range++;
		for (phase = 30; phase >= 0; phase--) {
			if (result[phase])
				phase_31_range++;
			else
				break;
		}
	}

	if (phase_0_range && phase_31_range) {
		left_edge_phase = 32 - phase_31_range;
		right_edge_phase = phase_0_range - 1;
		max_range = phase_0_range + phase_31_range;
		if (phase_0_range >= phase_31_range)
			center_phase = (phase_0_range - phase_31_range) / 2;
		else
			center_phase = 31 - (phase_31_range - phase_0_range) / 2;
	} else if (phase_0_range) {
		left_edge_phase = 0;
		right_edge_phase = phase_0_range - 1;
		max_range = phase_0_range;
		center_phase = phase_0_range / 2;
	} else if (phase_31_range) {
		left_edge_phase = 32 - phase_31_range;
		right_edge_phase = 31;
		max_range = phase_31_range;
		center_phase = 31 - phase_31_range / 2;
	} else {
		max_range = 0;
	}

	if (max_range >= 16)
		goto set_center_phase;

	range = 0;
	for (phase = phase_0_range + 1; phase < 32 - phase_31_range; phase++) {
		if ((result[phase] == 1) && (result[phase + 1] == 1)) {
			if (!range)
				left_edge_phase = phase;
			range++;
		} else {
			if (range && (range + 1) > max_range) {
				max_range = range + 1;
				right_edge_phase = phase;
				center_phase = (left_edge_phase + right_edge_phase) / 2;
			}
			range = 0;
		}
	}

set_center_phase:
	if (retune && max_range == 32) {
		center_phase = ambarella_host->all_pass_range_fixed_phase;
		pr_info("%s sw encounter all pass range, set center phase to 0x%x\n",  mmc_hostname(mmc),
			ambarella_host->all_pass_range_fixed_phase);
	}
	/* set center ph code */
	writel_relaxed(center_phase, host->ioaddr + AT_STAT_R);
	if(left_edge_phase > right_edge_phase)
		pr_debug("%s: center phase%s is 0x%x(%d), pass range is [0, %d] and [%d, 31], max_range is %d\n", mmc_hostname(mmc),
			mmc->doing_retune ? "(retune)" : "", center_phase, center_phase, right_edge_phase, left_edge_phase, max_range);
	else
		pr_debug("%s: center phase%s is 0x%x(%d), pass range is [%d, %d], max_range is %d\n", mmc_hostname(mmc),
			mmc->doing_retune ? "(retune)" : "", center_phase, center_phase, left_edge_phase, right_edge_phase, max_range);

	return 0;
}

static int sdhci_ambarella_auto_tuning(struct mmc_host *mmc, u32 opcode)
{
	int times = 3, retune = 0, retune_all_pass_range_times = RETUNE_ALL_PASS_RANGE_TIMES;
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	if ((clk_get_rate(pltfm_host->clk) > 150000000) || (ambarella_host->retune_all_pass_range))
		retune = 1;

	while(times-- > 0) {
retune:
		sdhci_execute_tuning(mmc, opcode);
		if (!host->tuning_err) {
			if (retune &&  sdhci_ambarella_all_pass_range(host) && --retune_all_pass_range_times > 0) {
				pr_debug("%s: hw all pass range at ctrl: 0x%08x | at stat:  0x%08x \n", mmc_hostname(host->mmc),
					 sdhci_readl(host, AT_CTRL_R), sdhci_readl(host, AT_STAT_R));
				goto retune;
			}
			break;
		}
		sdhci_abort_tuning(host, opcode);
	}

	if (!host->tuning_err) {
		if (retune &&  sdhci_ambarella_all_pass_range(host)) {
			pr_info("%s hw encounter all pass range, set center phase to 0x%x\n", mmc_hostname(host->mmc),
				ambarella_host->all_pass_range_fixed_phase);
			sdhci_ambarella_set_fixed_center_phase(host, ambarella_host->all_pass_range_fixed_phase);
			/* restore at_ctrl */
			sdhci_writel(host, 0x07000001, AT_CTRL_R);
		}
	}

	//return host->tuning_err;
	return 0;
}

static int sdhci_ambarella_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct sdhci_host *host = mmc_priv(mmc);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);

	if (opcode == MMC_SEND_TUNING_BLOCK_HS200)
		wait_for_tran_state(mmc);

	if (ambarella_host->fixed_phase != -1)
		sdhci_ambarella_fixed_tuning(mmc, opcode);
	else if (ambarella_host->sw_tuning)
		sdhci_ambarella_sw_tuning(mmc, opcode);
	else
		sdhci_ambarella_auto_tuning(mmc, opcode);

	if (opcode == MMC_SEND_TUNING_BLOCK_HS200)
		wait_for_tran_state(mmc);

	pr_debug("%s%s: at ctrl: 0x%08x | at stat:  0x%08x \n", mmc_hostname(host->mmc),
		 mmc->doing_retune ? "(retune)" : "", sdhci_readl(host, AT_CTRL_R), sdhci_readl(host, AT_STAT_R));

	return 0;
}

static void sdhci_ambarella_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sdhci_host *host = mmc_priv(mmc);

	/* support set rca through ioctl */
	if (mrq->cmd && mrq->cmd->opcode == MMC_SELECT_CARD &&
		mmc->card && mmc->card->type == MMC_TYPE_SD) {
		mmc->card->rca = mrq->cmd->arg >> 16;
		pr_debug("%s set sd card rca to 0x%x\n", mmc_hostname(mmc), mmc->card->rca);
	}

	/* Don't use auto cmd23 for reliable write */
	if (mrq->sbc && (mrq->sbc->arg & BIT(31)))
		host->flags &= ~SDHCI_AUTO_CMD23;
	else
		host->flags |= SDHCI_AUTO_CMD23;

	sdhci_request(mmc, mrq);
}

static int sdhci_ambarella_get_cd(struct mmc_host *mmc)
{
	struct sdhci_host *host = mmc_priv(mmc);
	int gpio_cd = mmc_gpio_get_cd(mmc);

	if (host->flags & SDHCI_DEVICE_DEAD)
		return 0;

	/* If nonremovable, assume that the card is always present. */
	if (!mmc_card_is_removable(mmc))
		return 1;

	/*
	 * Try slot gpio detect, if defined it take precedence
	 * over build in controller functionality
	 */
	if (gpio_cd >= 0)
		return !!gpio_cd;

	/* If polling, assume that the card is always present. */
	if (host->quirks & SDHCI_QUIRK_BROKEN_CARD_DETECTION)
		return 1;

	/* Host native card detect */
	if ((host->mmc->caps2 & MMC_CAP2_CD_ACTIVE_HIGH))
		return !(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
	else
		return !!(sdhci_readl(host, SDHCI_PRESENT_STATE) & SDHCI_CARD_PRESENT);
}

static unsigned int sdhci_ambarella_get_max_clock(struct sdhci_host *host)
{
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);

	return clk_get_rate(clk_get_parent(pltfm_host->clk));
}

static unsigned int sdhci_ambarella_get_min_clock(struct sdhci_host *host)
{
	return 100000;
}

static void sdhci_ambarella_reset(struct sdhci_host *host, u8 mask)
{
	u32 intmask = sdhci_readl(host, SDHCI_INT_STATUS);

	/* clear unexpect command complete interrupt */
	if (intmask & SDHCI_INT_RESPONSE) {
		sdhci_writel(host, SDHCI_INT_RESPONSE, SDHCI_INT_STATUS);
	}

	sdhci_reset(host, mask);
}

static void sdhci_ambarella_set_uhs_signaling(struct sdhci_host *host,
				      unsigned int timing)
{
	u16 ctrl_2;

	ctrl_2 = sdhci_readw(host, SDHCI_HOST_CONTROL2);
	/* Select Bus Speed Mode for host */
	ctrl_2 &= ~SDHCI_CTRL_UHS_MASK;
	if ((timing == MMC_TIMING_MMC_HS200) ||
	    (timing == MMC_TIMING_UHS_SDR104))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR104;
	else if (timing == MMC_TIMING_UHS_SDR12)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR12;
	else if ((timing == MMC_TIMING_UHS_SDR25) ||
		 (timing == MMC_TIMING_MMC_HS))
		ctrl_2 |= SDHCI_CTRL_UHS_SDR25;
	else if (timing == MMC_TIMING_UHS_SDR50)
		ctrl_2 |= SDHCI_CTRL_UHS_SDR50;
	else if ((timing == MMC_TIMING_UHS_DDR50) ||
		 (timing == MMC_TIMING_MMC_DDR52))
		ctrl_2 |= SDHCI_CTRL_UHS_DDR50;
	else if (timing == MMC_TIMING_MMC_HS400)
		ctrl_2 |= MSHC_CTRL_HS400;
	sdhci_writew(host, ctrl_2, SDHCI_HOST_CONTROL2);
}

/*
 * If DMA addr spans 128MB boundary, we split the DMA transfer into two
 * so that each DMA transfer doesn't exceed the boundary.
 */
static void sdhci_ambarella_adma_write_desc(struct sdhci_host *host, void **desc,
				    dma_addr_t addr, int len, unsigned int cmd)
{
	int tmplen, offset;

	if (likely(!len || BOUNDARY_OK(addr, len))) {
		sdhci_adma_write_desc(host, desc, addr, len, cmd);
		return;
	}

	offset = addr & (SZ_128M - 1);
	tmplen = SZ_128M - offset;
	sdhci_adma_write_desc(host, desc, addr, tmplen, cmd);

	addr += tmplen;
	len -= tmplen;
	sdhci_adma_write_desc(host, desc, addr, len, cmd);
}

#define DRIVER_NAME "sdhci_amba"
#define SDHCI_AMBA_DUMP(f, x...) \
	pr_err("%s: " DRIVER_NAME ": " f, mmc_hostname(host->mmc), ## x)

static void sdhci_ambarella_dump_vendor_regs(struct sdhci_host *host)
{
	SDHCI_AMBA_DUMP("----------- VENDOR REGISTER DUMP -----------\n");
	SDHCI_AMBA_DUMP(
			"at ctrl: 0x%08x | at stat:  0x%08x\n",
		readl_relaxed(host->ioaddr + AT_CTRL_R),
		readl_relaxed(host->ioaddr + AT_STAT_R));
}

static struct sdhci_ops sdhci_ambarella_ops = {
	.set_clock = sdhci_ambarella_set_clock,
	.set_power = sdhci_ambarella_set_power,
	.get_max_clock = sdhci_ambarella_get_max_clock,
	.get_min_clock = sdhci_ambarella_get_min_clock,
	.set_bus_width = sdhci_set_bus_width,
	.reset = sdhci_ambarella_reset,
	.set_uhs_signaling = sdhci_ambarella_set_uhs_signaling,
	.adma_write_desc = sdhci_ambarella_adma_write_desc,
	.dump_vendor_regs = sdhci_ambarella_dump_vendor_regs,
};

static int sdiomux_show(struct seq_file *s, void *data)
{
	int i;

	seq_printf(s, "just show sdiomux(GPIO0-17) mode for CV75/CV75m, please check /sys/kernel/debug/gpio for other info\n");
	seq_printf(s, " sdiomux is 0x%08x\n", g_sd_iomux);
	for (i = 0; i < 18; i++) {
		seq_printf(s, " gpio-%3d", i);
		if (g_sd_iomux & BIT(i))
			seq_printf(s, " [HW  ] (alt1)\n");
		else
			seq_printf(s, " [GPIO]\n");
	}

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(sdiomux);

static int sdhci_ambarella_parse_resource(struct platform_device *pdev,
	struct sdhci_host *host, struct sdhci_ambarella_host *ambarella_host)
{
	int ret, count, index;
	u32 regs[11], mask1, mask2, value1, value2;
	struct reg_field field;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;

	ambarella_host->power_gpio = devm_gpiod_get_optional(&pdev->dev,
							 "pwr",
							 GPIOD_OUT_HIGH);
	if (IS_ERR(ambarella_host->power_gpio)) {
		dev_err(dev, "Invalid power GPIO\n");
		ret = PTR_ERR(ambarella_host->power_gpio);
		goto out;
	}

	ambarella_host->v18_gpio = devm_gpiod_get_optional(&pdev->dev,
							 "v18",
							 GPIOD_OUT_LOW);
	if (IS_ERR(ambarella_host->v18_gpio)) {
		dev_err(dev, "Invalid v18 GPIO\n");
		ret = PTR_ERR(ambarella_host->v18_gpio);
		goto out;
	}

	if (of_property_read_u32(np, "amb,ddl_tx_sel", &ambarella_host->ddl_tx_sel) < 0)
		ambarella_host->ddl_tx_sel = DDL_TX_SEL_DEFAULT;
	if (of_property_read_u32(np, "amb,ddl_stb_sel", &ambarella_host->ddl_stb_sel) < 0)
		ambarella_host->ddl_stb_sel = DDL_STB_SEL_DEFAULT;
	ambarella_host->scr_regmap = syscon_regmap_lookup_by_phandle(np, "amb,scr-regmap");
	if (IS_ERR(ambarella_host->scr_regmap)) {
		ret = PTR_ERR(ambarella_host->scr_regmap);
		dev_err(dev, "scr regmap lookup failed %d.\n", ret);
		ambarella_host->scr_regmap = NULL;
		goto out;
	} else {
		count = of_property_count_elems_of_size(np, "amb,scr-regmap", sizeof(u32));
		ret = of_property_read_u32_array(np, "amb,scr-regmap", regs, count);
		if (ret < 0) {
			dev_err(dev, "Get scr regmap err=%d\n", ret);
			goto out;
		}
		/* using scr regmap property count to judge if support invert or not */
		if (count == 2) {
			ambarella_host->sd_ddl_ctrl_offset = regs[1];
			ambarella_host->support_invert = 0;
		} else if (count == 3) {
			ambarella_host->sd_ddl_ctrl_offset = regs[1];
			ambarella_host->support_invert = 1;
			ambarella_host->invert_bit = regs[2];
		} else {
			dev_err(dev, "scr regmap count don't support %d\n", count);
			ret = -EINVAL;
			goto out;
		}
	}

	if (of_property_read_bool(np, "amb,sd-iomux-regmap")) {
		ambarella_host->sd_iomux_regmap =
			syscon_regmap_lookup_by_phandle_args(np, "amb,sd-iomux-regmap", 1, &ambarella_host->sd_iomux_val);
		if (IS_ERR(ambarella_host->sd_iomux_regmap)) {
			ret = PTR_ERR(ambarella_host->sd_iomux_regmap);
			dev_err(dev, "sd iomux lookup failed err=%d.\n", ret);
			ambarella_host->sd_iomux_regmap = NULL;
			goto out;
		}
		index = host->mmc->index;
		g_sd_iomux |= ambarella_host->sd_iomux_val << (index * 9);
		regmap_write(ambarella_host->sd_iomux_regmap, 0, g_sd_iomux);
		regmap_write(ambarella_host->sd_iomux_regmap, 4, 0);
		regmap_write(ambarella_host->sd_iomux_regmap, 8, 0);
		regmap_write(ambarella_host->sd_iomux_regmap, SD_IOMUX_CTRL_SET_OFFSET, 1);
		regmap_write(ambarella_host->sd_iomux_regmap, SD_IOMUX_CTRL_SET_OFFSET, 0);

		if (!sdiomux_debugfs_file) {
			sdiomux_debugfs_file =  debugfs_create_file("sdiomux", S_IRUSR,
				ambarella_debugfs_dir(), NULL, &sdiomux_fops);
		}
	}

	count = of_property_count_elems_of_size(np, "amb,rct-regmap", sizeof(u32));
	if(count > 0) {
		ambarella_host->rct_regmap = syscon_regmap_lookup_by_phandle(np, "amb,rct-regmap");
		if (IS_ERR(ambarella_host->rct_regmap)) {
			ret = PTR_ERR(ambarella_host->rct_regmap);
			dev_err(dev, "rct regmap lookup failed %d.\n", ret);
			ambarella_host->rct_regmap = NULL;
			goto out;
		}

		ret = of_property_read_u32_array(np, "amb,rct-regmap", regs, count);
		if (ret < 0) {
			dev_err(dev, "Can't get ds rct regs offset err=%d\n", ret);
			goto out;
		}

		field.reg = regs[1];
		field.lsb = regs[3];
		field.msb = regs[4];
		ambarella_host->ds_ctrl[0] = devm_regmap_field_alloc(dev, ambarella_host->rct_regmap, field);
		if (IS_ERR(ambarella_host->ds_ctrl[0])) {
			dev_err(dev, "Can't alloc ds ctrl0 reg field.\n");
			ret = PTR_ERR(ambarella_host->ds_ctrl[0]);
			goto out;
		}

		field.reg = regs[2];
		field.lsb = regs[3];
		field.msb = regs[4];
		ambarella_host->ds_ctrl[1] = devm_regmap_field_alloc(dev, ambarella_host->rct_regmap, field);
		if (IS_ERR(ambarella_host->ds_ctrl[0])) {
			dev_err(dev, "Can't alloc ds ctrl1 reg field.\n");
			ret = PTR_ERR(ambarella_host->ds_ctrl[1]);
			goto out;
		}
		mask1 = (1 << (regs[4] - regs[3] + 1)) - 1;
		value1 = regs[5];

		if(count == 11) {
			field.reg = regs[6];
			field.lsb = regs[8];
			field.msb = regs[9];
			ambarella_host->ds_ctrl[2] = devm_regmap_field_alloc(dev, ambarella_host->rct_regmap, field);
			if (IS_ERR(ambarella_host->ds_ctrl[2])) {
				dev_err(dev, "Can't alloc ds ctrl2 reg field.\n");
				ret = PTR_ERR(ambarella_host->ds_ctrl[2]);
				goto out;
			}

			field.reg = regs[7];
			field.lsb = regs[8];
			field.msb = regs[9];
			ambarella_host->ds_ctrl[3] = devm_regmap_field_alloc(dev, ambarella_host->rct_regmap, field);
			if (IS_ERR(ambarella_host->ds_ctrl[3])) {
				dev_err(dev, "Can't alloc phy ctrl3 reg field.\n");
				ret = PTR_ERR(ambarella_host->ds_ctrl[3]);
				goto out;
			}
			mask2 = (1 << (regs[9] - regs[8] + 1)) - 1;
			value2 = regs[10];
		}

		if (of_property_read_bool(np, "amb,mmc_ds_2ma")) {
			regmap_field_update_bits(ambarella_host->ds_ctrl[0], mask1, value1);
			regmap_field_update_bits(ambarella_host->ds_ctrl[1], mask1, value1);
			if(count == 11) {
				regmap_field_update_bits(ambarella_host->ds_ctrl[2], mask2, value2);
				regmap_field_update_bits(ambarella_host->ds_ctrl[3], mask2, value2);
			}
		}

		if (of_property_read_bool(np, "amb,mmc_ds_4ma")) {
			regmap_field_update_bits(ambarella_host->ds_ctrl[0], mask1, value1);
			regmap_field_update_bits(ambarella_host->ds_ctrl[1], mask1, mask1);
			if(count == 11) {
				regmap_field_update_bits(ambarella_host->ds_ctrl[2], mask2, value2);
				regmap_field_update_bits(ambarella_host->ds_ctrl[3], mask2, mask2);
			}
		}

		if (of_property_read_bool(np, "amb,mmc_ds_8ma")) {
			regmap_field_update_bits(ambarella_host->ds_ctrl[0], mask1, mask1);
			regmap_field_update_bits(ambarella_host->ds_ctrl[1], mask1, value1);
			if(count == 11) {
				regmap_field_update_bits(ambarella_host->ds_ctrl[2], mask2, mask2);
				regmap_field_update_bits(ambarella_host->ds_ctrl[3], mask2, value2);
			}
		}

		if (of_property_read_bool(np, "amb,mmc_ds_12ma")) {
			regmap_field_update_bits(ambarella_host->ds_ctrl[0], mask1, mask1);
			regmap_field_update_bits(ambarella_host->ds_ctrl[1], mask1, mask1);
			if(count == 11) {
				regmap_field_update_bits(ambarella_host->ds_ctrl[2], mask2, mask2);
				regmap_field_update_bits(ambarella_host->ds_ctrl[3], mask2, mask2);
			}
		}
	}

	if (of_property_read_bool(np, "amb,retune-all-pass-range"))
		ambarella_host->retune_all_pass_range = true;
	else
		ambarella_host->retune_all_pass_range = false;

	if (of_property_read_u32(np, "amb,all-pass-range-fixed-phase", &ambarella_host->all_pass_range_fixed_phase) < 0)
		ambarella_host->all_pass_range_fixed_phase = 0;

	if (of_property_read_bool(np, "amb,sw-tuning"))
		ambarella_host->sw_tuning = true;
	else
		ambarella_host->sw_tuning = false;

	if (of_property_read_u32(np, "amb,fixed-phase", &ambarella_host->fixed_phase) < 0)
		ambarella_host->fixed_phase = -1;


	return 0;

out:
	return ret;
}

static const struct sdhci_pltfm_data sdhci_ambarella_pdata = {
	.quirks = SDHCI_QUIRK_CAP_CLOCK_BASE_BROKEN |
		  SDHCI_QUIRK_INVERTED_WRITE_PROTECT,
	.quirks2 = SDHCI_QUIRK2_PRESET_VALUE_BROKEN | SDHCI_QUIRK2_BROKEN_DDR50,
	.ops = &sdhci_ambarella_ops,
};

static int sdhci_ambarella_probe(struct platform_device *pdev)
{
	struct sdhci_host *host;
	struct sdhci_pltfm_host *pltfm_host;
	struct sdhci_ambarella_host *ambarella_host;
	int ret;

	host = sdhci_pltfm_init(pdev, &sdhci_ambarella_pdata, sizeof(*ambarella_host));
	if (IS_ERR(host))
		return PTR_ERR(host);

	pltfm_host = sdhci_priv(host);
	ambarella_host = sdhci_pltfm_priv(pltfm_host);
	pltfm_host->clk = devm_clk_get(&pdev->dev, NULL);

	if (IS_ERR(pltfm_host->clk)) {
		dev_err(&pdev->dev, "Get PLL failed!\n");
		ret = PTR_ERR(pltfm_host->clk);
		goto free_pltfm;
	}

	host->mmc_host_ops.start_signal_voltage_switch =
		sdhci_ambarella_voltage_switch;
	host->mmc_host_ops.hs400_enhanced_strobe = sdhci_ambarella_hs400_enhanced_strobe;
	host->mmc_host_ops.hs400_prepare_ddr = sdhci_ambarella_hs400_prepare_ddr;
	host->mmc_host_ops.execute_tuning = sdhci_ambarella_execute_tuning;
	host->mmc_host_ops.request = sdhci_ambarella_request;
	host->mmc_host_ops.get_cd = sdhci_ambarella_get_cd;

	ambarella_host->timing = -1;

	ret = sdhci_ambarella_parse_resource(pdev, host, ambarella_host);
	if (ret)
		goto free_pltfm;

	ret = mmc_of_parse(host->mmc);
	if (ret)
		goto free_pltfm;

	sdhci_get_of_property(pdev);

	sdhci_read_caps(host);
	/* if there is no 1.8v switch, clear UHS-I modes */
	if(ambarella_host->v18_gpio == NULL)
		host->caps1 &= ~(SDHCI_SUPPORT_SDR104 | SDHCI_SUPPORT_SDR50 |
			 SDHCI_SUPPORT_DDR50);

	ambarella_host->vendor_specific_area =
			sdhci_readl(host, MSHC_P_VENDOR_AREA) & MSHC_AREA_MASK;

	sdhci_enable_v4_mode(host);

	ret = sdhci_add_host(host);
	if (ret)
		goto free_pltfm;

	return 0;

free_pltfm:
	sdhci_pltfm_free(pdev);
	return ret;
}

static int sdhci_ambarella_remove(struct platform_device *pdev)
{
	struct sdhci_host *host = platform_get_drvdata(pdev);

	sdhci_remove_host(host, 0);
	sdhci_pltfm_free(pdev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int sdhci_ambarella_suspend(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	int ret;

	if (host->tuning_mode != SDHCI_TUNING_MODE_3)
		mmc_retune_needed(host->mmc);

	ret = sdhci_suspend_host(host);

	return ret;
}

static int sdhci_ambarella_resume(struct device *dev)
{
	struct sdhci_host *host = dev_get_drvdata(dev);
	struct sdhci_pltfm_host *pltfm_host = sdhci_priv(host);
	struct sdhci_ambarella_host *ambarella_host = sdhci_pltfm_priv(pltfm_host);
	int ret;

	if (ambarella_host->sd_iomux_regmap) {
		regmap_write(ambarella_host->sd_iomux_regmap, 0, g_sd_iomux);
		regmap_write(ambarella_host->sd_iomux_regmap, 4, 0);
		regmap_write(ambarella_host->sd_iomux_regmap, 8, 0);
		regmap_write(ambarella_host->sd_iomux_regmap, SD_IOMUX_CTRL_SET_OFFSET, 1);
		regmap_write(ambarella_host->sd_iomux_regmap, SD_IOMUX_CTRL_SET_OFFSET, 0);
	}
	ret = sdhci_resume_host(host);

	return ret;
}

static const struct dev_pm_ops sdhci_ambarella_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(sdhci_ambarella_suspend, sdhci_ambarella_resume)
};
#endif

static const struct of_device_id sdhci_ambarella_dt_match[] = {
	{ .compatible = "ambarella,sdhci"},
	{}
};
MODULE_DEVICE_TABLE(of, sdhci_ambarella_dt_match);

static struct platform_driver sdhci_ambarella_driver = {
	.probe = sdhci_ambarella_probe,
	.remove = sdhci_ambarella_remove,
	.driver = {
		   .name = "sdhci_ambarella",
		   .pm = &sdhci_ambarella_pm_ops,
		   .of_match_table = sdhci_ambarella_dt_match,
	},
};

module_platform_driver(sdhci_ambarella_driver);

MODULE_DESCRIPTION("Ambarella Secure Digital Host Controller Interface driver");
MODULE_LICENSE("GPL v2");

