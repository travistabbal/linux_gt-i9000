/*
 * wm8950.c  --  WM8950 ALSA Soc Audio driver
 *
 * Copyright 2006 Wolfson Microelectronics PLC.
 *
 * Author: Liam Girdwood <liam.girdwood@wolfsonmicro.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/initval.h>

#include "wm8950.h"

struct snd_soc_codec_device soc_codec_dev_wm8950;

/*
 * wm8950 register cache
 * We can't read the WM8950 register space when we are
 * using 2 wire for device control, so we cache them instead.
 */
static const u16 wm8950_reg[WM8950_CACHEREGNUM] = {
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0050, 0x0000, 0x0140, 0x0000,
	0x0000, 0x0000, 0x0000, 0x00ff,
	0x0000, 0x0000, 0x0100, 0x00ff,
	0x0000, 0x0000, 0x012c, 0x002c,
	0x002c, 0x002c, 0x002c, 0x0000,
	0x0032, 0x0000, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0038, 0x000b, 0x0032, 0x0000,
	0x0008, 0x000c, 0x0093, 0x00e9,
	0x0000, 0x0000, 0x0000, 0x0000,
	0x0003, 0x0010, 0x0000, 0x0000,
	0x0000, 0x0002, 0x0000, 0x0000,
	0x0000, 0x0000, 0x0039, 0x0000,
	0x0000,
};

struct wm8950_priv {
	struct snd_soc_codec codec;
	u16 reg_cache[WM8950_CACHEREGNUM];
};

static struct snd_soc_codec *wm8950_codec;

/*
 * read wm8950 register cache
 */
static inline unsigned int wm8950_read_reg_cache(struct snd_soc_codec *codec,
	unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg == WM8950_RESET)
		return 0;
	if (reg >= WM8950_CACHEREGNUM)
		return -1;
	return cache[reg];
}

/*
 * write wm8950 register cache
 */
static inline void wm8950_write_reg_cache(struct snd_soc_codec *codec,
	u16 reg, unsigned int value)
{
	u16 *cache = codec->reg_cache;
	if (reg >= WM8950_CACHEREGNUM)
		return;
	cache[reg] = value;
}

/*
 * write to the WM8950 register space
 */
static int wm8950_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int value)
{
	u8 data[2];

	/* data is
	 *   D15..D9 WM8950 register offset
	 *   D8...D0 register data
	 */
	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	wm8950_write_reg_cache(codec, reg, value);
	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

#define wm8950_reset(c)	wm8950_write(c, WM8950_RESET, 0)

static const char *wm8950_companding[] = {"Off", "NC", "u-law", "A-law" };
static const char *wm8950_deemp[] = {"None", "32kHz", "44.1kHz", "48kHz" };
static const char *wm8950_bw[] = {"Narrow", "Wide" };
static const char *wm8950_eq1[] = {"80Hz", "105Hz", "135Hz", "175Hz" };
static const char *wm8950_eq2[] = {"230Hz", "300Hz", "385Hz", "500Hz" };
static const char *wm8950_eq3[] = {"650Hz", "850Hz", "1.1kHz", "1.4kHz" };
static const char *wm8950_eq4[] = {"1.8kHz", "2.4kHz", "3.2kHz", "4.1kHz" };
static const char *wm8950_eq5[] = {"5.3kHz", "6.9kHz", "9kHz", "11.7kHz" };
static const char *wm8950_alc[] = {"ALC", "Limiter" };

static const struct soc_enum wm8950_enum[] = {
	SOC_ENUM_SINGLE(WM8950_COMP, 1, 4, wm8950_companding), /* adc */
	SOC_ENUM_SINGLE(WM8950_DAC,  4, 4, wm8950_deemp),

	SOC_ENUM_SINGLE(WM8950_EQ1,  5, 4, wm8950_eq1),
	SOC_ENUM_SINGLE(WM8950_EQ2,  8, 2, wm8950_bw),
	SOC_ENUM_SINGLE(WM8950_EQ2,  5, 4, wm8950_eq2),
	SOC_ENUM_SINGLE(WM8950_EQ3,  8, 2, wm8950_bw),

	SOC_ENUM_SINGLE(WM8950_EQ3,  5, 4, wm8950_eq3),
	SOC_ENUM_SINGLE(WM8950_EQ4,  8, 2, wm8950_bw),
	SOC_ENUM_SINGLE(WM8950_EQ4,  5, 4, wm8950_eq4),

	SOC_ENUM_SINGLE(WM8950_EQ5,  5, 4, wm8950_eq5),
	SOC_ENUM_SINGLE(WM8950_ALC3,  8, 2, wm8950_alc),
};

static const struct snd_kcontrol_new wm8950_snd_controls[] = {

SOC_SINGLE("Digital Loopback Switch", WM8950_COMP, 0, 1, 0),

SOC_ENUM("ADC Companding", wm8950_enum[0]),

SOC_SINGLE("High Pass Filter Switch", WM8950_ADC, 8, 1, 0),
SOC_SINGLE("High Pass Cut Off", WM8950_ADC, 4, 7, 0),
SOC_SINGLE("ADC Inversion Switch", WM8950_COMP, 0, 1, 0),

SOC_SINGLE("Capture Volume", WM8950_ADCVOL,  0, 127, 0),

SOC_ENUM("Equaliser Function", wm8950_enum[3]),
SOC_ENUM("EQ1 Cut Off", wm8950_enum[4]),
SOC_SINGLE("EQ1 Volume", WM8950_EQ1,  0, 31, 1),

SOC_ENUM("Equaliser EQ2 Bandwith", wm8950_enum[5]),
SOC_ENUM("EQ2 Cut Off", wm8950_enum[6]),
SOC_SINGLE("EQ2 Volume", WM8950_EQ2,  0, 31, 1),

SOC_ENUM("Equaliser EQ3 Bandwith", wm8950_enum[7]),
SOC_ENUM("EQ3 Cut Off", wm8950_enum[8]),
SOC_SINGLE("EQ3 Volume", WM8950_EQ3,  0, 31, 1),

SOC_ENUM("Equaliser EQ4 Bandwith", wm8950_enum[9]),
SOC_ENUM("EQ4 Cut Off", wm8950_enum[10]),
SOC_SINGLE("EQ4 Volume", WM8950_EQ4,  0, 31, 1),

SOC_ENUM("EQ5 Cut Off", wm8950_enum[12]),
SOC_SINGLE("EQ5 Volume", WM8950_EQ5,  0, 31, 1),

SOC_SINGLE("ALC Enable Switch", WM8950_ALC1,  8, 1, 0),
SOC_SINGLE("ALC Capture Max Gain", WM8950_ALC1,  3, 7, 0),
SOC_SINGLE("ALC Capture Min Gain", WM8950_ALC1,  0, 7, 0),

SOC_SINGLE("ALC Capture ZC Switch", WM8950_ALC2,  8, 1, 0),
SOC_SINGLE("ALC Capture Hold", WM8950_ALC2,  4, 7, 0),
SOC_SINGLE("ALC Capture Target", WM8950_ALC2,  0, 15, 0),

SOC_ENUM("ALC Capture Mode", wm8950_enum[13]),
SOC_SINGLE("ALC Capture Decay", WM8950_ALC3,  4, 15, 0),
SOC_SINGLE("ALC Capture Attack", WM8950_ALC3,  0, 15, 0),

SOC_SINGLE("ALC Capture Noise Gate Switch", WM8950_NGATE,  3, 1, 0),
SOC_SINGLE("ALC Capture Noise Gate Threshold", WM8950_NGATE,  0, 7, 0),

SOC_SINGLE("Capture PGA ZC Switch", WM8950_INPPGA,  7, 1, 0),
SOC_SINGLE("Capture PGA Volume", WM8950_INPPGA,  0, 63, 0),

SOC_SINGLE("Capture Boost(+20dB)", WM8950_ADCBOOST,  8, 1, 0),
};

/* AUX Input boost vol */
static const struct snd_kcontrol_new wm8950_aux_boost_controls =
SOC_DAPM_SINGLE("Aux Volume", WM8950_ADCBOOST, 0, 7, 0);

/* Mic Input boost vol */
static const struct snd_kcontrol_new wm8950_mic_boost_controls =
SOC_DAPM_SINGLE("Mic Volume", WM8950_ADCBOOST, 4, 7, 0);

/* Capture boost switch */
static const struct snd_kcontrol_new wm8950_capture_boost_controls =
SOC_DAPM_SINGLE("Capture Boost Switch", WM8950_INPPGA,  6, 1, 0);

/* Aux In to PGA */
static const struct snd_kcontrol_new wm8950_aux_capture_boost_controls =
SOC_DAPM_SINGLE("Aux Capture Boost Switch", WM8950_INPPGA,  2, 1, 0);

/* Mic P In to PGA */
static const struct snd_kcontrol_new wm8950_micp_capture_boost_controls =
SOC_DAPM_SINGLE("Mic P Capture Boost Switch", WM8950_INPPGA,  0, 1, 0);

/* Mic N In to PGA */
static const struct snd_kcontrol_new wm8950_micn_capture_boost_controls =
SOC_DAPM_SINGLE("Mic N Capture Boost Switch", WM8950_INPPGA,  1, 1, 0);

static const struct snd_soc_dapm_widget wm8950_dapm_widgets[] = {
SND_SOC_DAPM_ADC("ADC", "HiFi Capture", WM8950_POWER3, 0, 0),
SND_SOC_DAPM_PGA("Aux Input", WM8950_POWER1, 6, 0, NULL, 0),
SND_SOC_DAPM_PGA("Mic PGA", WM8950_POWER2, 2, 0, NULL, 0),

SND_SOC_DAPM_PGA("Aux Boost", SND_SOC_NOPM, 0, 0,
	&wm8950_aux_boost_controls, 1),
SND_SOC_DAPM_PGA("Mic Boost", SND_SOC_NOPM, 0, 0,
	&wm8950_mic_boost_controls, 1),
SND_SOC_DAPM_SWITCH("Capture Boost", SND_SOC_NOPM, 0, 0,
	&wm8950_capture_boost_controls),

SND_SOC_DAPM_MIXER("Boost Mixer", WM8950_POWER2, 4, 0, NULL, 0),

SND_SOC_DAPM_MICBIAS("Mic Bias", WM8950_POWER1, 4, 0),

SND_SOC_DAPM_INPUT("MICN"),
SND_SOC_DAPM_INPUT("MICP"),
SND_SOC_DAPM_INPUT("AUX"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Boost Mixer */
	{"Boost Mixer", NULL, "ADC"},
	{"Capture Boost Switch", "Aux Capture Boost Switch", "AUX"},
	{"Aux Boost", "Aux Volume", "Boost Mixer"},
	{"Capture Boost", "Capture Switch", "Boost Mixer"},
	{"Mic Boost", "Mic Volume", "Boost Mixer"},

	/* Inputs */
	{"MICP", NULL, "Mic Boost"},
	{"MICN", NULL, "Mic PGA"},
	{"Mic PGA", NULL, "Capture Boost"},
	{"AUX", NULL, "Aux Input"},
};

static int wm8950_add_widgets(struct snd_soc_codec *codec)
{
	snd_soc_dapm_new_controls(codec, wm8950_dapm_widgets,
				  ARRAY_SIZE(wm8950_dapm_widgets));

	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_new_widgets(codec);
	return 0;
}

struct pll_ {
	unsigned int in_hz, out_hz;
	unsigned int pre:4; /* prescale - 1 */
	unsigned int n:4;
	unsigned int k;
};

static struct pll_ pll[] = {
	{12000000, 11289600, 0, 7, 0x86c220},
	{12000000, 12288000, 0, 8, 0x3126e8},
	{13000000, 11289600, 0, 6, 0xf28bd4},
	{13000000, 12288000, 0, 7, 0x8fd525},
	{12288000, 11289600, 0, 7, 0x59999a},
	{11289600, 12288000, 0, 8, 0x80dee9},
	/* liam - add more entries */
};

static int wm8950_set_dai_pll(struct snd_soc_dai *codec_dai,
	int pll_id, int src, unsigned int freq_in, unsigned int freq_out)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	int i;
	u16 reg;

	if (freq_in == 0 || freq_out == 0) {
		reg = wm8950_read_reg_cache(codec, WM8950_POWER1);
		wm8950_write(codec, WM8950_POWER1, reg & 0x1df);
		return 0;
	}

	for (i = 0; i < ARRAY_SIZE(pll); i++) {
		if (freq_in == pll[i].in_hz && freq_out == pll[i].out_hz) {
			wm8950_write(codec, WM8950_PLLN,
				     (pll[i].pre << 4) | pll[i].n);
			wm8950_write(codec, WM8950_PLLK1, pll[i].k >> 18);
			wm8950_write(codec, WM8950_PLLK1,
				     (pll[i].k >> 9) && 0x1ff);
			wm8950_write(codec, WM8950_PLLK1, pll[i].k && 0x1ff);
			reg = wm8950_read_reg_cache(codec, WM8950_POWER1);
			wm8950_write(codec, WM8950_POWER1, reg | 0x020);
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Configure WM8950 clock dividers.
 */
static int wm8950_set_dai_clkdiv(struct snd_soc_dai *codec_dai,
		int div_id, int div)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 reg;

	switch (div_id) {
	case WM8950_OPCLKDIV:
		reg = wm8950_read_reg_cache(codec, WM8950_GPIO & 0x1cf);
		wm8950_write(codec, WM8950_GPIO, reg | div);
		break;
	case WM8950_MCLKDIV:
		reg = wm8950_read_reg_cache(codec, WM8950_CLOCK & 0x1f);
		wm8950_write(codec, WM8950_CLOCK, reg | div);
		break;
	case WM8950_ADCCLK:
		reg = wm8950_read_reg_cache(codec, WM8950_ADC & 0x1f7);
		wm8950_write(codec, WM8950_ADC, reg | div);
		break;
	case WM8950_BCLKDIV:
		reg = wm8950_read_reg_cache(codec, WM8950_CLOCK & 0x1e3);
		wm8950_write(codec, WM8950_CLOCK, reg | div);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int wm8950_set_dai_fmt(struct snd_soc_dai *codec_dai,
		unsigned int fmt)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	u16 iface = 0;
	u16 clk = wm8950_read_reg_cache(codec, WM8950_CLOCK) & 0x1fe;

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		clk |= 0x0001;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	/* interface format */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		iface |= 0x0010;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		iface |= 0x0008;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		iface |= 0x00018;
		break;
	default:
		return -EINVAL;
	}

	/* clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		iface |= 0x0180;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		iface |= 0x0100;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		iface |= 0x0080;
		break;
	default:
		return -EINVAL;
	}

	wm8950_write(codec, WM8950_IFACE, iface);
	wm8950_write(codec, WM8950_CLOCK, clk);
	return 0;
}

static int wm8950_pcm_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	u16 iface = wm8950_read_reg_cache(codec, WM8950_IFACE) & 0x19f;
	u16 adn = wm8950_read_reg_cache(codec, WM8950_ADD) & 0x1f1;

	/* bit size */
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		iface |= 0x0020;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		iface |= 0x0040;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		iface |= 0x0060;
		break;
	}

	/* filter coefficient */
	switch (params_rate(params)) {
	case SNDRV_PCM_RATE_8000:
		adn |= 0x5 << 1;
		break;
	case SNDRV_PCM_RATE_11025:
		adn |= 0x4 << 1;
		break;
	case SNDRV_PCM_RATE_16000:
		adn |= 0x3 << 1;
		break;
	case SNDRV_PCM_RATE_22050:
		adn |= 0x2 << 1;
		break;
	case SNDRV_PCM_RATE_32000:
		adn |= 0x1 << 1;
		break;
	case SNDRV_PCM_RATE_44100:
		break;
	}

	wm8950_write(codec, WM8950_IFACE, iface);
	wm8950_write(codec, WM8950_ADD, adn);
	return 0;
}

/* liam need to make this lower power with dapm */
static int wm8950_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		wm8950_write(codec, WM8950_POWER1, 0x1ff);
		wm8950_write(codec, WM8950_POWER2, 0x1ff);
		wm8950_write(codec, WM8950_POWER3, 0x1ff);
		break;
	case SND_SOC_BIAS_PREPARE:
	case SND_SOC_BIAS_STANDBY:
		break;
	case SND_SOC_BIAS_OFF:
		/* everything off, dac mute, inactive */
		wm8950_write(codec, WM8950_POWER1, 0x0);
		wm8950_write(codec, WM8950_POWER2, 0x0);
		wm8950_write(codec, WM8950_POWER3, 0x0);
		break;
	}
	codec->bias_level = level;
	return 0;
}

#define WM8950_RATES (SNDRV_PCM_RATE_8000_48000)

#define WM8950_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE |\
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops wm8950_ops = {
	.hw_params = wm8950_pcm_hw_params,
	.set_fmt = wm8950_set_dai_fmt,
	.set_clkdiv = wm8950_set_dai_clkdiv,
	.set_pll = wm8950_set_dai_pll,
};

struct snd_soc_dai wm8950_dai = {
	.name = "WM8950 HiFi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 1,
		.rates = WM8950_RATES,
		.formats = WM8950_FORMATS,},
	.ops = &wm8950_ops,
};
EXPORT_SYMBOL_GPL(wm8950_dai);

static int wm8950_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;

	wm8950_set_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int wm8950_resume(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec = socdev->card->codec;
	int i;
	u8 data[2];
	u16 *cache = codec->reg_cache;

	/* Sync reg_cache with the hardware */
	for (i = 0; i < ARRAY_SIZE(wm8950_reg); i++) {
		data[0] = (i << 1) | ((cache[i] >> 8) & 0x0001);
		data[1] = cache[i] & 0x00ff;
		codec->hw_write(codec->control_data, data, 2);
	}
	wm8950_set_bias_level(codec, SND_SOC_BIAS_STANDBY);
	wm8950_set_bias_level(codec, codec->suspend_bias_level);
	return 0;
}

static int wm8950_probe(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);
	struct snd_soc_codec *codec;
	int ret = 0;

	if (wm8950_codec == NULL) {
		dev_err(&pdev->dev, "Codec device not registered\n");
		return -ENODEV;
	}

	socdev->card->codec = wm8950_codec;
	codec = wm8950_codec;

	/* register pcms */
	ret = snd_soc_new_pcms(socdev, SNDRV_DEFAULT_IDX1, SNDRV_DEFAULT_STR1);
	if (ret < 0) {
		dev_err(codec->dev, "failed to create pcms: %d\n", ret);
		goto pcm_err;
	}

	snd_soc_add_controls(codec, wm8950_snd_controls,
			     ARRAY_SIZE(wm8950_snd_controls));
	wm8950_add_widgets(codec);

	return 0;

pcm_err:
	return ret;
}

/* power down chip */
static int wm8950_remove(struct platform_device *pdev)
{
	struct snd_soc_device *socdev = platform_get_drvdata(pdev);

	snd_soc_free_pcms(socdev);
	snd_soc_dapm_free(socdev);

	return 0;
}

struct snd_soc_codec_device soc_codec_dev_wm8950 = {
	.probe = 	wm8950_probe,
	.remove = 	wm8950_remove,
	.suspend = 	wm8950_suspend,
	.resume =	wm8950_resume,
};
EXPORT_SYMBOL_GPL(soc_codec_dev_wm8950);

static int wm8950_register(struct wm8950_priv *wm8950)
{
	int ret;
	struct snd_soc_codec *codec = &wm8950->codec;

	if (wm8950_codec) {
		dev_err(codec->dev, "Another WM8950 is registered\n");
		return -EINVAL;
	}

	mutex_init(&codec->mutex);
	INIT_LIST_HEAD(&codec->dapm_widgets);
	INIT_LIST_HEAD(&codec->dapm_paths);

	codec->name = "WM8950";
	codec->owner = THIS_MODULE;
	codec->read = wm8950_read_reg_cache;
	codec->write = wm8950_write;
	codec->bias_level = SND_SOC_BIAS_OFF;
	codec->set_bias_level = wm8950_set_bias_level;
	codec->dai = &wm8950_dai;
	codec->num_dai = 1;
	codec->reg_cache_size = WM8950_CACHEREGNUM;
	codec->reg_cache = &wm8950->reg_cache;

	memcpy(codec->reg_cache, wm8950_reg, sizeof(wm8950_reg));

	ret = wm8950_reset(codec);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to issue reset\n");
		return ret;
	}

	wm8950_dai.dev = codec->dev;

	wm8950_codec = codec;

	ret = snd_soc_register_codec(codec);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register codec: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_dai(&wm8950_dai);
	if (ret != 0) {
		dev_err(codec->dev, "Failed to register DAI: %d\n", ret);
		snd_soc_unregister_codec(codec);
		return ret;
	}

	return 0;
}

static void wm8950_unregister(struct wm8950_priv *wm8950)
{
	wm8950_set_bias_level(&wm8950->codec, SND_SOC_BIAS_OFF);
	snd_soc_unregister_dai(&wm8950_dai);
	snd_soc_unregister_codec(&wm8950->codec);
	kfree(wm8950);
	wm8950_codec = NULL;
}

#if defined(CONFIG_SPI_MASTER)
static int wm8950_spi_write(struct spi_device *spi, const char *data, int len)
{
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[2];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}

static int __devinit wm8950_spi_probe(struct spi_device *spi)
{
	struct snd_soc_codec *codec;
	struct wm8950_priv *wm8950;

	wm8950 = kzalloc(sizeof(struct wm8950_priv), GFP_KERNEL);
	if (wm8950 == NULL)
		return -ENOMEM;

	codec = &wm8950->codec;
	codec->control_data = spi;
	codec->hw_write = (hw_write_t)wm8950_spi_write;
	codec->dev = &spi->dev;

	dev_set_drvdata(&spi->dev, wm8950);

	return wm8950_register(wm8950);
}

static int __devexit wm8950_spi_remove(struct spi_device *spi)
{
	struct wm8950_priv *wm8950 = dev_get_drvdata(&spi->dev);

	wm8950_unregister(wm8950);

	return 0;
}

static struct spi_driver wm8950_spi_driver = {
	.driver = {
		.name	= "wm8950",
		.bus	= &spi_bus_type,
		.owner	= THIS_MODULE,
	},
	.probe		= wm8950_spi_probe,
	.remove		= __devexit_p(wm8950_spi_remove),
};
#endif /* CONFIG_SPI_MASTER */

#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
static __devinit int wm8950_i2c_probe(struct i2c_client *i2c,
				      const struct i2c_device_id *id)
{
	struct wm8950_priv *wm8950;
	struct snd_soc_codec *codec;

	wm8950 = kzalloc(sizeof(struct wm8950_priv), GFP_KERNEL);
	if (wm8950 == NULL)
		return -ENOMEM;

	codec = &wm8950->codec;
	codec->hw_write = (hw_write_t)i2c_master_send;

	i2c_set_clientdata(i2c, wm8950);
	codec->control_data = i2c;

	codec->dev = &i2c->dev;

	return wm8950_register(wm8950);
}

static __devexit int wm8950_i2c_remove(struct i2c_client *client)
{
	struct wm8950_priv *wm8950 = i2c_get_clientdata(client);
	wm8950_unregister(wm8950);
	return 0;
}

static const struct i2c_device_id wm8950_i2c_id[] = {
	{ "wm8950", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, wm8950_i2c_id);

static struct i2c_driver wm8950_i2c_driver = {
	.driver = {
		.name = "WM8950 I2C Codec",
		.owner = THIS_MODULE,
	},
	.probe =    wm8950_i2c_probe,
	.remove =   __devexit_p(wm8950_i2c_remove),
	.id_table = wm8950_i2c_id,
};
#endif

static int __init wm8950_modinit(void)
{
	int ret;
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	ret = i2c_add_driver(&wm8950_i2c_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8950 I2C driver: %d\n",
		       ret);
	}
#endif
#if defined(CONFIG_SPI_MASTER)
	ret = spi_register_driver(&wm8950_spi_driver);
	if (ret != 0) {
		printk(KERN_ERR "Failed to register WM8950 SPI driver: %d\n",
		       ret);
	}
#endif
	return 0;
}
module_init(wm8950_modinit);

static void __exit wm8950_exit(void)
{
#if defined(CONFIG_I2C) || defined(CONFIG_I2C_MODULE)
	i2c_del_driver(&wm8950_i2c_driver);
#endif
#if defined(CONFIG_SPI_MASTER)
	spi_unregister_driver(&wm8950_spi_driver);
#endif
}
module_exit(wm8950_exit);


MODULE_DESCRIPTION("ASoC WM8950 driver");
MODULE_AUTHOR("Liam Girdwood");
MODULE_LICENSE("GPL");
