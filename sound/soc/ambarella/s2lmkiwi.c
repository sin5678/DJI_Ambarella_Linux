/*
 * sound/soc/s2lmkiwi.c
 *
 * Author: Cao Rongrong <rrcao@ambarella.com>
 *		  Diao Chengdong<cddiao@ambarella.com>
 * History:
 *	2009/08/20 - [Cao Rongrong] Created file
 *	2011/03/20 - [Cao Rongrong] Port to 2.6.38,
 *				    merge coconut and durian into a5sevk
 *	2012/06/27 - [Cao Rongrong] Rename to ambevk.c
 *	2013/01/18 - [Ken He] Port to 3.8
 *    2014/03/21 - [Diao Chengdong] modify for kiwi
 *
 * Copyright (C) 2014-2018, Ambarella, Inc.
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
#include <linux/of.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <plat/audio.h>
#include "../codecs/ak4954_amb.h"

static unsigned int dai_fmt = 0;
module_param(dai_fmt, uint, 0644);
MODULE_PARM_DESC(dai_fmt, "DAI format.");
/* clk_fmt :
* 0 : mclk, bclk provided by cpu
* 1 : bclk provide by cpu, mclk is not used
* 2 : mclk provided by cpu, bclk is provided by codec
* There are four type of connection:
* clk_fmt=0:
* cpu :                   codec:
*     MCLK    ------>  MCKI
*     BCLK    ------>  BICK
*     LRCK    ------>  LRCK
*                   ...
* clk_fmt=1:
* cpu :                   codec:
*     MCLK   is not used
*     BCLK    ------> BICK
*     LRCK    ------> LRCK
*                    ...
* clk_fmt=2:
* cpu :                   codec:
*     MCLK   ------> MCKI
*     BCLK   <------ BICK
*     LRCK   <------ LRCK
* There are one connection we are not used, it is like clk_fmt=0,but power on the codec PLL.
* It is a waster of power, so we do not use it.
*/
static unsigned int clk_fmt = 1;
module_param(clk_fmt, uint, 0664);

struct amb_clk {
	int mclk;
	int oversample;
	int bclk;
};
static int clk_0_config(struct snd_pcm_hw_params *params, struct amb_clk *clk)
{
	switch (params_rate(params)) {
	case 8000:
		clk->mclk = 2048000;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 11025:
		clk->mclk = 2822400;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 12000:
		clk->mclk = 3072000;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 16000:
		clk->mclk = 4096000;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 22050:
		clk->mclk = 5644800;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 24000:
		clk->mclk = 6144000;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 32000:
		clk->mclk = 8192000;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 44100:
		clk->mclk = 11289600;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 48000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_256xfs;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static int clk_1_config(struct snd_pcm_hw_params *params, struct amb_clk *clk)
{
	switch (params_rate(params)) {
	case 8000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_1536xfs;
		clk->bclk = 8000;
		break;
	case 11025:
		clk->mclk = 2822400;
		clk->oversample = AudioCodec_256xfs;
		clk->bclk = 11025;
		break;
	case 12000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_1024xfs;
		clk->bclk = 12000;
		break;
	case 16000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_768xfs;
		clk->bclk = 16000;
		break;
	case 22050:
		clk->mclk = 5644800;
		clk->oversample = AudioCodec_256xfs;
		clk->bclk = 22050;
		break;
	case 24000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_512xfs;
		clk->bclk = 24000;
		break;
	case 32000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_384xfs;
		clk->bclk = 32000;
		break;
	case 44100:
		clk->mclk = 11289600;
		clk->oversample = AudioCodec_256xfs;
		clk->bclk = 44100;
		break;
	case 48000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_256xfs;
		clk->bclk = 48000;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
* The range of MCLK in ak4954 is 11MHz ot 27MHz
*
*
*/
static int clk_2_config(struct snd_pcm_hw_params *params, struct amb_clk *clk)
{
	switch (params_rate(params)) {
	case 8000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_1536xfs;
		break;
	case 11025:
		clk->mclk = 11289600;
		clk->oversample = AudioCodec_1024xfs;
		break;
	case 12000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_1024xfs;
		break;
	case 16000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_768xfs;
		break;
	case 22050:
		clk->mclk = 11289600;
		clk->oversample = AudioCodec_512xfs;
		break;
	case 24000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_512xfs;
		break;
	case 32000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_384xfs;
		break;
	case 44100:
		clk->mclk = 11289600;
		clk->oversample = AudioCodec_256xfs;
		break;
	case 48000:
		clk->mclk = 12288000;
		clk->oversample = AudioCodec_256xfs;
		break;
	default:
		return -EINVAL;
	}
	return 0;


}
static int ambevk_board_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int errorCode = 0, i2s_mode;
	struct amb_clk clk={0, 0, 0};

	if (clk_fmt == 0) {
		errorCode = clk_0_config(params, &clk);
	} else if (clk_fmt == 1) {
		errorCode = clk_1_config(params, &clk);
	} else if (clk_fmt == 2) {
		errorCode = clk_2_config(params, &clk);
	} else {
		pr_err("clk_fmt is wrong\n");
		return -1;
	}

	if (dai_fmt == 0)
		i2s_mode = SND_SOC_DAIFMT_I2S;
	else
		i2s_mode = SND_SOC_DAIFMT_DSP_A;

	/* set the I2S system data format*/
	if (clk_fmt == 2) {
		errorCode = snd_soc_dai_set_fmt(codec_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		if (errorCode < 0) {
			pr_err("can't set codec DAI configuration\n");
			goto hw_params_exit;
		}

		errorCode = snd_soc_dai_set_fmt(cpu_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM);
		if (errorCode < 0) {
			pr_err("can't set cpu DAI configuration\n");
			goto hw_params_exit;
		}
	} else {
		errorCode = snd_soc_dai_set_fmt(codec_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (errorCode < 0) {
			pr_err("can't set codec DAI configuration\n");
			goto hw_params_exit;
		}

		errorCode = snd_soc_dai_set_fmt(cpu_dai,
			i2s_mode | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBS_CFS);
		if (errorCode < 0) {
			pr_err("can't set cpu DAI configuration\n");
			goto hw_params_exit;
		}
	}
	/* set the I2S system clock*/
	if (clk_fmt == 0) {
		errorCode = snd_soc_dai_set_sysclk(codec_dai, clk_fmt, clk.mclk, 0);
	} else if (clk_fmt == 1) {
		errorCode = snd_soc_dai_set_sysclk(codec_dai, clk_fmt, clk.bclk, 0);
	} else if (clk_fmt == 2) {
		errorCode = snd_soc_dai_set_sysclk(codec_dai, clk_fmt, clk.mclk, 0);
	} else {
		pr_err("clk_fmt is wrong\n");
		return -1;
	}

	if (errorCode < 0) {
		pr_err("can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_sysclk(cpu_dai, AMBARELLA_CLKSRC_ONCHIP, clk.mclk, 0);
	if (errorCode < 0) {
		pr_err("can't set cpu MCLK configuration\n");
		goto hw_params_exit;
	}

	errorCode = snd_soc_dai_set_clkdiv(cpu_dai, AMBARELLA_CLKDIV_LRCLK, clk.oversample);
	if (errorCode < 0) {
		pr_err("can't set cpu MCLK/SF ratio\n");
		goto hw_params_exit;
	}

hw_params_exit:
	return errorCode;
}

static struct snd_soc_ops ambevk_board_ops = {
	.hw_params = ambevk_board_hw_params,
};

/* ambevk machine dapm widgets */
static const struct snd_soc_dapm_widget ambevk_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic internal", NULL),
	SND_SOC_DAPM_MIC("Mic external", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_HP("HP Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

/* ambevk machine audio map (connections to ak4954 pins) */
static const struct snd_soc_dapm_route ambevk_audio_map[] = {
	/* Line In is connected to LLIN1, RLIN1 */
	{"LIN2", NULL, "Mic internal"},
	{"RIN2", NULL, "Mic external"},
	{"LIN3", NULL, "Line In"},
	{"RIN3", NULL, "Line In"},

	/* Line Out is connected to LLOUT, RLOUT */
	{"Line Out", NULL, "SPKLO"},
	{"Line Out", NULL, "SPKLO"},
	{"HP Jack", NULL, "HPR"},
	{"HP Jack", NULL, "HPL"},

	/* Speaker is connected to SPP, SPN */
	{"Speaker", NULL, "SPKLO"},
	{"Speaker", NULL, "SPKLO"},
};

static int ambevk_ak4954_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
}

static struct snd_soc_dai_link ambevk_dai_link = {
	.name = "ak4954",
	.stream_name = "ak4954 PCM",
	.codec_dai_name = "ak4954-hifi",
	.init = ambevk_ak4954_init,
	.ops = &ambevk_board_ops,
};


/* ambevk audio machine driver */
static struct snd_soc_card snd_soc_card_ambevk = {
	.owner = THIS_MODULE,
	.dai_link = &ambevk_dai_link,
	.num_links = 1,

	.dapm_widgets = ambevk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ambevk_dapm_widgets),
	.dapm_routes = ambevk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(ambevk_audio_map),
};

static int ambevk_soc_snd_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpup_np, *codec_np;
	struct snd_soc_card *card = &snd_soc_card_ambevk;
	int rval = 0;

	card->dev = &pdev->dev;
	if (snd_soc_of_parse_card_name(card, "amb,model")) {
		dev_err(&pdev->dev, "Card name is not provided\n");
		return -ENODEV;
	}

	cpup_np = of_parse_phandle(np, "amb,i2s-controllers", 0);
	codec_np = of_parse_phandle(np, "amb,audio-codec", 0);
	if (!cpup_np || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		return -EINVAL;
	}

	ambevk_dai_link.codec_of_node = codec_np;
	ambevk_dai_link.cpu_of_node = cpup_np;
	ambevk_dai_link.platform_of_node = cpup_np;

	of_node_put(codec_np);
	of_node_put(cpup_np);

	rval = snd_soc_register_card(card);
	if (rval)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", rval);

	return rval;
}

static int ambevk_soc_snd_remove(struct platform_device *pdev)
{
	snd_soc_unregister_card(&snd_soc_card_ambevk);

	return 0;
}

static const struct of_device_id ambevk_dt_ids[] = {
	{ .compatible = "ambarella,s2lmkiwi-ak4954", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ambevk_dt_ids);

static struct platform_driver ambevk_soc_snd_driver = {
	.driver = {
		.name = "snd_soc_card_s2lmkiwi",
		.owner = THIS_MODULE,
		.pm = &snd_soc_pm_ops,
		.of_match_table = ambevk_dt_ids,
	},
	.probe = ambevk_soc_snd_probe,
	.remove = ambevk_soc_snd_remove,
};

module_platform_driver(ambevk_soc_snd_driver);

MODULE_AUTHOR("Diao Chengdong<cddiao@ambarella.com>");
MODULE_DESCRIPTION("Amabrella Board with AK4954 Codec for ALSA");
MODULE_LICENSE("GPL");
MODULE_ALIAS("snd-soc-s2lkiwi");

