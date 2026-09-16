/*
 * sound/soc/ambarella_i2s.c
 *
 * History:
 *	2008/03/03 - [Eric Lee] created file
 *	2008/04/16 - [Eric Lee] Removed the compiling warning
 *	2009/01/22 - [Anthony Ginger] Port to 2.6.28
 *	2009/03/05 - [Cao Rongrong] Update from 2.6.22.10
 *	2009/06/10 - [Cao Rongrong] Port to 2.6.29
 *	2009/06/29 - [Cao Rongrong] Support more mclk and fs
 *	2010/10/25 - [Cao Rongrong] Port to 2.6.36+
 *	2011/03/20 - [Cao Rongrong] Port to 2.6.38
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
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>
#include <soc/ambarella/audio.h>
#include "ambarella_pcm.h"
#include "ambarella_i2s.h"

static unsigned int capture_enabled = 1;
module_param(capture_enabled, uint, 0644);
MODULE_PARM_DESC(capture_enabled, "capture_enabled.");

static unsigned int bclk_reverse = 0;
module_param(bclk_reverse, uint, 0644);
MODULE_PARM_DESC(bclk_reverse, "bclk_reverse.");

static unsigned int rsp_falling = 0;
module_param(rsp_falling, uint, 0644);
MODULE_PARM_DESC(rsp_falling, "rsp_falling.");

static DEFINE_MUTEX(clock_reg_mutex);

/* ==========================================================================*/

SRCU_NOTIFIER_HEAD_STATIC(audio_notifier_list);
static struct ambarella_i2s_interface audio_i2s_intf;

struct ambarella_i2s_interface get_audio_i2s_interface(void)
{
	return audio_i2s_intf;
}
EXPORT_SYMBOL(get_audio_i2s_interface);

int ambarella_audio_register_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_register(&audio_notifier_list, nb);
}
EXPORT_SYMBOL(ambarella_audio_register_notifier);

int ambarella_audio_unregister_notifier(struct notifier_block *nb)
{
	return srcu_notifier_chain_unregister(&audio_notifier_list, nb);
}
EXPORT_SYMBOL(ambarella_audio_unregister_notifier);

static void ambarella_audio_notify_transition(int state, void *data)
{
	memcpy(&audio_i2s_intf, data, sizeof(audio_i2s_intf));
	audio_i2s_intf.state = state;
	srcu_notifier_call_chain(&audio_notifier_list, state, &audio_i2s_intf);
}

/* ==========================================================================*/

static inline void dai_tx_enable(struct amb_i2s_priv *priv_data)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val |= I2S_TX_ENABLE_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);
}

static inline void dai_rx_enable(struct amb_i2s_priv *priv_data)
{
	u32 val;
	unsigned long flags;

	if(!capture_enabled)
		return;

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val |= I2S_RX_ENABLE_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);
}

static inline void dai_tx_disable(struct amb_i2s_priv *priv_data)
{
	u32 val;
	int i, j;
	unsigned long flags;

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val &= ~I2S_TX_ENABLE_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);

	if (priv_data->ws_set_support == 0) {
		for (j = 0; j < 100; j++){
			val = readl_relaxed(priv_data->regbase + I2S_TX_STATUS_OFFSET);
			if ((val & 0x10) == 0x10)
				break;
			for (i = 0; i <8; i++)
				writel(0, priv_data->regbase + I2S_TX_LEFT_DATA_OFFSET);
		}

		val = readl_relaxed(priv_data->regbase + I2S_TX_STATUS_OFFSET);
		if ((val & 0x10) != 0x10)
			printk("Try to disable tx failed \n");
	}
}

static inline void dai_rx_disable(struct amb_i2s_priv *priv_data)
{
	u32 val;
	unsigned long flags;

	if(!capture_enabled)
		return;

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val &= ~I2S_RX_ENABLE_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);
}

static inline void dai_tx_fifo_rst(struct amb_i2s_priv *priv_data)
{
	u32 val;
	int rval;
	unsigned long flags;
	struct ambarella_i2s_interface *i2s_intf = &priv_data->i2s_intf;

	rval = readl_poll_timeout_atomic(priv_data->regbase + I2S_TX_STATUS_OFFSET,
				val, !!(val & I2S_TX_IDLE_FLAG_BIT), 100, 10000);
	if (rval < 0)
		pr_err("%s: TX is busy.\n", __func__);

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val |= I2S_TX_FIFO_RESET_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);

	if ((priv_data->ws_set_support == 0) && (i2s_intf->mode == I2S_DSP_MODE)) {
		int i, j;

		val = readl_relaxed(priv_data->regbase + I2S_TX_CTRL_OFFSET);
		if ((val & I2S_TX_UNISON_BIT) == I2S_TX_UNISON_BIT)
			j = (i2s_intf->slots - 1) >> 1;
		else
			j = i2s_intf->slots - 1;

		for (i = 0; i < j; i++)
			writel(0, priv_data->regbase + I2S_TX_LEFT_DATA_OFFSET);
	}
}

static inline void dai_rx_fifo_rst(struct amb_i2s_priv *priv_data)
{
	u32 val;
	int rval;
	unsigned long flags;

	rval = readl_poll_timeout_atomic(priv_data->regbase + I2S_RX_STATUS_OFFSET,
				val, !!(val & I2S_RX_IDLE_FLAG_BIT), 100, 10000);
	if (rval < 0)
		pr_err("%s: RX is busy.\n", __func__);

	if(!capture_enabled)
		return;

	spin_lock_irqsave(&priv_data->init_lock, flags);
	val = readl_relaxed(priv_data->regbase + I2S_INIT_OFFSET);
	val |= I2S_RX_FIFO_RESET_BIT;
	writel_relaxed(val, priv_data->regbase + I2S_INIT_OFFSET);
	spin_unlock_irqrestore(&priv_data->init_lock, flags);
}

static int ambarella_i2s_prepare(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(dai);

	if(substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		dai_rx_disable(priv_data);
		dai_rx_fifo_rst(priv_data);
	} else {
		dai_tx_disable(priv_data);
		dai_tx_fifo_rst(priv_data);
	}
	return 0;
}

static int ambarella_i2s_startup(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret = 0;

	/* Add a rule to enforce the DMA buffer align. */
	ret = snd_pcm_hw_constraint_step(runtime, 0,
			SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
	if (ret)
		goto ambarella_i2s_startup_exit;

	ret = snd_pcm_hw_constraint_step(runtime, 0,
			SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
	if (ret)
		goto ambarella_i2s_startup_exit;

	ret = snd_pcm_hw_constraint_integer(runtime,
			SNDRV_PCM_HW_PARAM_PERIODS);

ambarella_i2s_startup_exit:
	return ret >= 0 ? 0 : ret;
}

static u32 ambarella_i2s_channels_to_sel(u32 channels, bool dsp_mode)
{
	if (dsp_mode)
		return 0;

	switch (channels) {
	case 2:
		return I2S_2CHANNELS_ENB;
	case 4:
		return I2S_4CHANNELS_ENB;
	case 6:
		return I2S_6CHANNELS_ENB;
	case 8:
		return I2S_8CHANNELS_ENB;
	default:
		return I2S_2CHANNELS_ENB;
	}
}

static u32 ambarella_i2s_encode_channel(struct amb_i2s_priv *priv)
{
	u32 tx_sel, rx_sel;

	tx_sel = ambarella_i2s_channels_to_sel(priv->tx_channels,
			priv->i2s_intf.mode == I2S_DSP_MODE);
	rx_sel = ambarella_i2s_channels_to_sel(priv->rx_channels,
			priv->i2s_intf.mode == I2S_DSP_MODE);

	return (rx_sel << I2S_CHSEL_RX_SHIFT) | tx_sel;
}

static void ambarella_i2s_apply_channel(struct amb_i2s_priv *priv)
{
	if (priv->split_chsel) {
		priv->channel_sel = ambarella_i2s_encode_channel(priv);
	} else if (priv->i2s_intf.mode == I2S_DSP_MODE) {
		priv->channel_sel = 0;
	} else {
		priv->channel_sel = ambarella_i2s_channels_to_sel(priv->i2s_intf.channels,
							      false);
	}

	writel_relaxed(priv->channel_sel, priv->regbase + I2S_CHANNEL_SELECT_OFFSET);
}

static u32 ambarella_i2s_bclk_channels(struct amb_i2s_priv *priv)
{
	if (priv->split_chsel)
		return I2S_PARALLEL_LANE_CHANNELS;

	return priv->i2s_intf.channels;
}

static int ambarella_i2s_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *cpu_dai)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(cpu_dai);
	struct ambarella_i2s_interface *i2s_intf = &priv_data->i2s_intf;
	u32 clock_reg, clk_div, slot_width, bclk;
	int rval;

	if(capture_enabled == 0)
		return 0;

	/* Disable tx/rx before initializing */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		dai_tx_disable(priv_data);
	else
		dai_rx_disable(priv_data);

	i2s_intf->sfreq = params_rate(params);
	i2s_intf->channels = params_channels(params);
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		priv_data->tx_channels = i2s_intf->channels;
	else
		priv_data->rx_channels = i2s_intf->channels;

	ambarella_i2s_apply_channel(priv_data);

	/* Set format */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		i2s_intf->multi24 = 0;
		i2s_intf->tx_ctrl = I2S_TX_UNISON_BIT;
		i2s_intf->word_len = 15;
		if (i2s_intf->mode == I2S_DSP_MODE) {
			i2s_intf->slots = i2s_intf->channels - 1;
			i2s_intf->word_pos = 15;
		} else if (i2s_intf->mode == I2S_LEFT_JUSTIFIED_MODE) {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0;
			i2s_intf->tx_ctrl |= I2S_TX_WS_INV_BIT;
			i2s_intf->rx_ctrl |= I2S_RX_WS_INV_BIT;
		} else {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0;
		}
		priv_data->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
		break;

	case SNDRV_PCM_FORMAT_S24_LE:		/* 32bits, valid data in low 3 bytes */
		i2s_intf->multi24 = I2S_24BITMUX_MODE_ENABLE;
		i2s_intf->tx_ctrl = 0;
		i2s_intf->word_len = 23;
		if (i2s_intf->mode == I2S_DSP_MODE) {
			i2s_intf->slots = i2s_intf->channels - 1;
			i2s_intf->word_pos = 0; /* ignored */
		} else if (i2s_intf->mode == I2S_LEFT_JUSTIFIED_MODE) {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0;
			i2s_intf->tx_ctrl |= I2S_TX_WS_INV_BIT;
			i2s_intf->rx_ctrl |= I2S_RX_WS_INV_BIT;
		} else {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0; /* ignored */
		}
		priv_data->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;

	case SNDRV_PCM_FORMAT_S32_LE:
		i2s_intf->multi24 = I2S_24BITMUX_MODE_ENABLE;
		i2s_intf->tx_ctrl = 0;
		i2s_intf->word_len = 31;
		if (i2s_intf->mode == I2S_DSP_MODE) {
			i2s_intf->slots = i2s_intf->channels - 1;
			i2s_intf->word_pos = 0; /* ignored */
		} else if (i2s_intf->mode == I2S_LEFT_JUSTIFIED_MODE) {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0;
			i2s_intf->tx_ctrl |= I2S_TX_WS_INV_BIT;
			i2s_intf->rx_ctrl |= I2S_RX_WS_INV_BIT;
		} else {
			i2s_intf->slots = 0;
			i2s_intf->word_pos = 0; /* ignored */
		}
		priv_data->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		break;

	default:
		goto hw_params_exit;
	}

	if (priv_data->dai_master == true) {
		i2s_intf->tx_ctrl |= I2S_TX_WS_MST_BIT;
		i2s_intf->rx_ctrl |= I2S_RX_WS_MST_BIT;
	} else {
		i2s_intf->tx_ctrl &= ~I2S_TX_WS_MST_BIT;
		i2s_intf->rx_ctrl &= ~I2S_RX_WS_MST_BIT;
	}

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		writel_relaxed(i2s_intf->tx_ctrl, priv_data->regbase + I2S_TX_CTRL_OFFSET);
		writel_relaxed(0x10, priv_data->regbase + I2S_TX_FIFO_LTH_OFFSET);
	} else {
		writel_relaxed(i2s_intf->rx_ctrl, priv_data->regbase + I2S_RX_CTRL_OFFSET);
		writel_relaxed(0x20, priv_data->regbase + I2S_RX_FIFO_GTH_OFFSET);
	}

	writel_relaxed(i2s_intf->mode, priv_data->regbase + I2S_MODE_OFFSET);
	writel_relaxed(i2s_intf->word_len, priv_data->regbase + I2S_WLEN_OFFSET);
	writel_relaxed(i2s_intf->word_pos, priv_data->regbase + I2S_WPOS_OFFSET);
	writel_relaxed(i2s_intf->slots, priv_data->regbase + I2S_SLOT_OFFSET);
	writel_relaxed(i2s_intf->multi24, priv_data->regbase + I2S_24BITMUX_MODE_OFFSET);

	/* Set clock */
	rval = clk_set_rate(priv_data->mclk, i2s_intf->mclk);
	if (rval < 0) {
		dev_err(cpu_dai->dev, "failed to set I2S mclk to %u: %d\n",
			i2s_intf->mclk, rval);
		return rval;
	}

	slot_width = i2s_intf->word_len + 1;
	/* S24_LE: 32-bit I2S slot on wire unless DSP uses 24-bit slots. */
	if (params_format(params) == SNDRV_PCM_FORMAT_S24_LE &&
	    i2s_intf->mode != I2S_DSP_MODE)
		slot_width = 32;

	/*
	 * bclk = clk_au / (2 * (clk_div + 1))
	 * target: bclk = bclk_channels * sfreq * slot_width
	 */
	bclk = ambarella_i2s_bclk_channels(priv_data) *
		i2s_intf->sfreq * slot_width;
	if (!bclk || 2 * bclk > i2s_intf->mclk) {
		dev_err(cpu_dai->dev, "invalid I2S clock: mclk=%u, bclk=%u\n",
			i2s_intf->mclk, bclk);
		return -EINVAL;
	}

	clk_div = i2s_intf->mclk / (2 * bclk) - 1;
	if (clk_div > I2S_CLK_DIV_MASK) {
		dev_err(cpu_dai->dev, "I2S clock divider %u exceeds mask 0x%x\n",
			clk_div, I2S_CLK_DIV_MASK);
		return -EINVAL;
	}

	mutex_lock(&clock_reg_mutex);

	clock_reg = readl_relaxed(priv_data->regbase + I2S_CLOCK_OFFSET);
	clock_reg &= ~I2S_CLK_DIV_MASK;
	clock_reg |= clk_div;

	if (priv_data->dai_master == true)
		clock_reg |= I2S_CLK_MASTER_MODE;
	else
		clock_reg &= ~I2S_CLK_MASTER_MODE;

	if (bclk_reverse)
		clock_reg &= ~I2S_CLK_TX_PO_FALL;
	else
		clock_reg |= I2S_CLK_TX_PO_FALL;

	if (rsp_falling)
		clock_reg |= I2S_CLK_RX_PO_FALL;
	else
		clock_reg &= ~I2S_CLK_RX_PO_FALL;

	writel_relaxed(clock_reg, priv_data->regbase + I2S_CLOCK_OFFSET);
	mutex_unlock(&clock_reg_mutex);

	msleep(1);

	if ((priv_data->dai_master == true) && priv_data->ws_set_support)
		writel_relaxed(I2S_WS_EN, priv_data->regbase + I2S_WS_OFFSET);

	/* Notify HDMI that the audio interface is changed */
	ambarella_audio_notify_transition(AUDIO_NOTIFY_SETHWPARAMS, i2s_intf);

	return 0;

hw_params_exit:
	return -EINVAL;
}

static int ambarella_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *cpu_dai)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			dai_tx_enable(priv_data);
		else
			dai_rx_enable(priv_data);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			dai_tx_disable(priv_data);
		else
			dai_rx_disable(priv_data);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK){
			dai_tx_disable(priv_data);
			dai_tx_fifo_rst(priv_data);
		}else{
			dai_rx_disable(priv_data);
			dai_rx_fifo_rst(priv_data);
		}
		break;
	default:
		break;
	}

	return 0;
}

/*
 * Set Ambarella I2S DAI format
 */
static int ambarella_i2s_set_fmt(struct snd_soc_dai *cpu_dai,
		unsigned int fmt)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(cpu_dai);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_LEFT_J:
		priv_data->i2s_intf.mode = I2S_LEFT_JUSTIFIED_MODE;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		priv_data->i2s_intf.mode = I2S_RIGHT_JUSTIFIED_MODE;
		break;
	case SND_SOC_DAIFMT_I2S:
		priv_data->i2s_intf.mode = I2S_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		priv_data->i2s_intf.mode = I2S_DSP_MODE;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		priv_data->dai_master = true;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		if (priv_data->i2s_intf.mode != I2S_I2S_MODE) {
			printk("DAI can't work in slave mode without standard I2S format!\n");
			return -EINVAL;
		}
		priv_data->dai_master = false;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int ambarella_i2s_set_sysclk(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(cpu_dai);

	if (clk_id != 0) {
		/* AMBARELLA_CLKSRC_ONCHIP */
		printk("clk source (%d) is not supported yet\n", clk_id);
		return -EINVAL;
	}

	if (dir != SND_SOC_CLOCK_OUT)
		return -ENOTSUPP;

	priv_data->i2s_intf.clksrc = clk_id;
	priv_data->i2s_intf.mclk = freq;

	return 0;
}

/* ==========================================================================*/


#ifdef CONFIG_PM
static int ambarella_i2s_suspend(struct snd_soc_component *component)
{
	struct amb_i2s_priv *priv_data = snd_soc_component_get_drvdata(component);
	struct ambarella_i2s_interface *i2s_intf = &priv_data->i2s_intf;
	void __iomem *regbase = priv_data->regbase;

	if (!snd_soc_component_active(component))
		return 0;

	priv_data->clock_reg = readl_relaxed(regbase + I2S_CLOCK_OFFSET);
	i2s_intf->mode = readl_relaxed(regbase + I2S_MODE_OFFSET);
	i2s_intf->word_len = readl_relaxed(regbase + I2S_WLEN_OFFSET);
	i2s_intf->word_pos = readl_relaxed(regbase + I2S_WPOS_OFFSET);
	i2s_intf->slots = readl_relaxed(regbase + I2S_SLOT_OFFSET);
	priv_data->channel_sel = readl_relaxed(regbase + I2S_CHANNEL_SELECT_OFFSET);
	if (priv_data->split_chsel) {
		priv_data->tx_channels = ((priv_data->channel_sel & I2S_CHSEL_TX_MASK) + 1) * 2;
		priv_data->rx_channels = (((priv_data->channel_sel & I2S_CHSEL_RX_MASK) >>
					   I2S_CHSEL_RX_SHIFT) + 1) * 2;
	}
	i2s_intf->rx_ctrl = readl_relaxed(regbase + I2S_RX_CTRL_OFFSET);
	i2s_intf->tx_ctrl = readl_relaxed(regbase + I2S_TX_CTRL_OFFSET);
	i2s_intf->rx_fifo_len = readl_relaxed(regbase + I2S_RX_FIFO_GTH_OFFSET);
	i2s_intf->tx_fifo_len = readl_relaxed(regbase + I2S_TX_FIFO_LTH_OFFSET);
	i2s_intf->multi24 = readl_relaxed(regbase + I2S_24BITMUX_MODE_OFFSET);
	i2s_intf->ws_set= readl_relaxed(regbase + I2S_WS_OFFSET);

	return 0;
}

static int ambarella_i2s_resume(struct snd_soc_component *component)
{
	struct amb_i2s_priv *priv_data = snd_soc_component_get_drvdata(component);
	struct ambarella_i2s_interface *i2s_intf = &priv_data->i2s_intf;
	void __iomem *regbase = priv_data->regbase;

	if (priv_data->clk_au_enable)
		regmap_update_bits(priv_data->scr_regmap, priv_data->clk_au_pad_offset, BIT(0), 0);

	writel_relaxed(i2s_intf->mode, regbase + I2S_MODE_OFFSET);
	writel_relaxed(i2s_intf->word_len, regbase + I2S_WLEN_OFFSET);
	writel_relaxed(i2s_intf->word_pos, regbase + I2S_WPOS_OFFSET);
	writel_relaxed(i2s_intf->slots, regbase + I2S_SLOT_OFFSET);
	writel_relaxed(priv_data->channel_sel, regbase + I2S_CHANNEL_SELECT_OFFSET);
	writel_relaxed(i2s_intf->rx_ctrl, regbase + I2S_RX_CTRL_OFFSET);
	writel_relaxed(i2s_intf->tx_ctrl, regbase + I2S_TX_CTRL_OFFSET);
	writel_relaxed(i2s_intf->rx_fifo_len, regbase + I2S_RX_FIFO_GTH_OFFSET);
	writel_relaxed(i2s_intf->tx_fifo_len, regbase + I2S_TX_FIFO_LTH_OFFSET);
	writel_relaxed(i2s_intf->multi24, regbase + I2S_24BITMUX_MODE_OFFSET);
	writel_relaxed(i2s_intf->ws_set, regbase + I2S_WS_OFFSET);
	writel_relaxed(priv_data->clock_reg, regbase + I2S_CLOCK_OFFSET);

	return 0;
}
#else /* CONFIG_PM */
#define ambarella_i2s_suspend	NULL
#define ambarella_i2s_resume	NULL
#endif /* CONFIG_PM */

static int ambarella_i2s_dai_probe(struct snd_soc_dai *dai)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(dai);
	struct ambarella_i2s_interface *i2s_intf = &priv_data->i2s_intf;
	u32 sfreq, clk_div = 3;

	snd_soc_dai_init_dma_data(dai,  &priv_data->playback_dma_data,
					&priv_data->capture_dma_data);

	if (priv_data->default_mclk == 12288000) {
		sfreq = 48000;
	} else if (priv_data->default_mclk == 11289600) {
		sfreq = 44100;
	} else {
		priv_data->default_mclk = 12288000;
		sfreq = 48000;
	}

	clk_set_rate(priv_data->mclk, priv_data->default_mclk);

	/* bclk = clk_au / (2 * (clk_div + 1)) */
	clk_div = priv_data->default_mclk / (2 * 2 * 16 * sfreq) - 1;

	/*
	 * Dai default smapling rate, polarity configuration.
	 * Note: Just be configured, actually BCLK and LRCLK will not
	 * output to outside at this time.
	 */
	if(capture_enabled)
		writel_relaxed(clk_div | I2S_CLK_TX_PO_FALL,
						priv_data->regbase + I2S_CLOCK_OFFSET);

	priv_data->dai_master = true;
	i2s_intf->mode = I2S_I2S_MODE;
	i2s_intf->clksrc = AMBARELLA_CLKSRC_ONCHIP;
	i2s_intf->mclk = priv_data->default_mclk;
	i2s_intf->sfreq = sfreq;
	i2s_intf->word_len = 15;
	i2s_intf->word_pos = 0;
	i2s_intf->slots = 0;
	i2s_intf->channels = 2;
	priv_data->tx_channels = 2;
	priv_data->rx_channels = 2;
	priv_data->channel_sel = ambarella_i2s_encode_channel(priv_data);

	/* Notify HDMI that the audio interface is initialized */
	ambarella_audio_notify_transition(AUDIO_NOTIFY_INIT, &priv_data->i2s_intf);

	return 0;
}

static int ambarella_i2s_dai_remove(struct snd_soc_dai *dai)
{
	struct amb_i2s_priv *priv_data = snd_soc_dai_get_drvdata(dai);

	/* Disable I2S clock output */
	if(!capture_enabled)
		writel_relaxed(0x0, priv_data->regbase + I2S_CLOCK_OFFSET);

	/* Notify that the audio interface is removed */
	ambarella_audio_notify_transition(AUDIO_NOTIFY_REMOVE, &priv_data->i2s_intf);

	return 0;
}

static const struct snd_soc_dai_ops ambarella_i2s_dai_ops = {
	.prepare = ambarella_i2s_prepare,
	.startup = ambarella_i2s_startup,
	.hw_params = ambarella_i2s_hw_params,
	.trigger = ambarella_i2s_trigger,
	.set_fmt = ambarella_i2s_set_fmt,
	.set_sysclk = ambarella_i2s_set_sysclk,
};

static const struct snd_soc_dai_driver ambarella_i2s_dai = {
	.probe = ambarella_i2s_dai_probe,
	.remove = ambarella_i2s_dai_remove,
	.playback = {
		.channels_min = 2,
		.channels_max = 0, /* set per-instance in ambarella_i2s_probe() */
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 0, /* set per-instance in ambarella_i2s_probe() */
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S24_LE |
			   SNDRV_PCM_FMTBIT_S32_LE,
	},
	.ops = &ambarella_i2s_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver ambarella_i2s_component = {
	.name = "ambarella-i2s",
	.suspend = ambarella_i2s_suspend,
	.resume = ambarella_i2s_resume,
	.legacy_dai_naming	= 1,
};

static int ambarella_i2s_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct amb_i2s_priv *priv_data;
	struct snd_soc_dai_driver *i2s_dai;
	struct resource *res;
	int channels, rval;

	priv_data = devm_kzalloc(&pdev->dev, sizeof(*priv_data), GFP_KERNEL);
	if (priv_data == NULL)
		return -ENOMEM;

	spin_lock_init(&priv_data->init_lock);

	priv_data->regbase = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(priv_data->regbase)) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return PTR_ERR(priv_data->regbase);
	}

	priv_data->playback_dma_data.addr = res->start + I2S_TX_LEFT_DATA_DMA_OFFSET;
	priv_data->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	priv_data->playback_dma_data.maxburst = 32;

	priv_data->capture_dma_data.addr = res->start + I2S_RX_DATA_DMA_OFFSET;
	priv_data->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_2_BYTES;
	priv_data->capture_dma_data.maxburst = 32;
	priv_data->mclk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(priv_data->mclk)) {
		dev_err(&pdev->dev, "Get audio clk failed!\n");
		return PTR_ERR(priv_data->mclk);
	}

	dev_set_drvdata(&pdev->dev, priv_data);

	rval = of_property_read_u32(np, "amb,i2s-channels", &channels);
	if (rval < 0) {
		dev_err(&pdev->dev, "Get channels failed! %d\n", rval);
		return -ENXIO;
	}

	of_property_read_u32(np, "amb,default-mclk", &priv_data->default_mclk);

	priv_data->ws_set_support = !!of_find_property(np, "amb,ws-set", NULL);

	priv_data->split_chsel = of_property_read_bool(np, "amb,split-tx-rx-chsel");
	if (priv_data->split_chsel && channels != 8) {
		dev_warn(&pdev->dev,
			 "split-tx-rx-chsel requires amb,i2s-channels = 8\n");
		priv_data->split_chsel = false;
	}

	priv_data->tx_channels = 2;
	priv_data->rx_channels = 2;

	i2s_dai = devm_kmemdup(&pdev->dev, &ambarella_i2s_dai,
			sizeof(*i2s_dai), GFP_KERNEL);
	if (!i2s_dai)
		return -ENOMEM;

	i2s_dai->playback.channels_max = channels;
	i2s_dai->capture.channels_max = channels;

	priv_data->clk_au_enable = !!of_find_property(np, "amb,clk-au-enable", NULL);
	if (priv_data->clk_au_enable) {
		priv_data->scr_regmap = syscon_regmap_lookup_by_phandle(np, "amb,scr-regmap");
		if (IS_ERR(priv_data->scr_regmap)) {
			dev_err(&pdev->dev, "no regmap!\n");
			rval = PTR_ERR(priv_data->scr_regmap);
			return rval;
		}

		if (of_property_read_u32_index(np, "amb,scr-regmap", 1, &priv_data->clk_au_pad_offset)) {
			dev_err(&pdev->dev, "couldn't get clk_au pad offset\n");
			rval= -EINVAL;
			return rval;
		}
		rval = regmap_update_bits(priv_data->scr_regmap, priv_data->clk_au_pad_offset, BIT(0), 0);
		if (rval < 0)
			return rval;
	}

	if (of_alias_get_id(np, "i2s") < 0) {
		dev_warn(&pdev->dev, "i2s without alias,use id:0 as default.\n");
		priv_data->i2s_intf.id = 0;
	} else {
		priv_data->i2s_intf.id = of_alias_get_id(np, "i2s");
	}

	rval = snd_soc_register_component(&pdev->dev,
			&ambarella_i2s_component,  i2s_dai, 1);
	if (rval < 0){
		dev_err(&pdev->dev, "register DAI failed\n");
		return rval;
	}

	rval = ambarella_pcm_platform_register(&pdev->dev);
	if (rval) {
		dev_err(&pdev->dev, "register PCM failed: %d\n", rval);
		snd_soc_unregister_component(&pdev->dev);
		return rval;
	}

	return 0;
}

static int ambarella_i2s_remove(struct platform_device *pdev)
{
	ambarella_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	return 0;
}

static const struct of_device_id ambarella_i2s_dt_ids[] = {
	{ .compatible = "ambarella,i2s", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ambarella_i2s_dt_ids);

static struct platform_driver ambarella_i2s_driver = {
	.probe = ambarella_i2s_probe,
	.remove = ambarella_i2s_remove,

	.driver = {
		.name = "ambarella-i2s",
		.of_match_table = ambarella_i2s_dt_ids,
	},
};

module_platform_driver(ambarella_i2s_driver);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("Ambarella Soc I2S Interface");

MODULE_LICENSE("GPL");

