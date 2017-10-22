/*
 * Driver for ADAU1761/ADAU1461/ADAU1761/ADAU1961 codec
 *
 * Copyright 2011-2013 Analog Devices Inc.
 * Author: Lars-Peter Clausen <lars@metafoo.de>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <linux/platform_data/adau17x1.h>
#include <linux/delay.h>

#include <asm/unaligned.h>

#include "sigmadsp.h"
#include "adau_1761-i2c.h" 
#include "SigmaStudioFW.h"

#define DSPCONF
#ifdef DSPCONF
#include "adau1761_IC_1.h"
#include "adau1761_IC_1_PARAM.h"
#include "adau1761_IC_1_REG.h"
#endif

static struct i2c_client *local_client=NULL;


#define ADAU1761_FIRMWARE "adau1761.bin"

static const struct reg_default adau1761_reg_defaults[] = {
	{ ADAU1761_DEJITTER,			0x03 },
	{ ADAU1761_DIGMIC_JACKDETECT,		0x00 },
	{ ADAU1761_REC_MIXER_LEFT0,		0x00 },
	{ ADAU1761_REC_MIXER_LEFT1,		0x00 },
	{ ADAU1761_REC_MIXER_RIGHT0,		0x00 },
	{ ADAU1761_REC_MIXER_RIGHT1,		0x00 },
	{ ADAU1761_LEFT_DIFF_INPUT_VOL,		0x00 },
	{ ADAU1761_RIGHT_DIFF_INPUT_VOL,	0x00 },
	{ ADAU1761_PLAY_LR_MIXER_LEFT,		0x00 },
	{ ADAU1761_PLAY_MIXER_LEFT0,		0x00 },
	{ ADAU1761_PLAY_MIXER_LEFT1,		0x00 },
	{ ADAU1761_PLAY_MIXER_RIGHT0,		0x00 },
	{ ADAU1761_PLAY_MIXER_RIGHT1,		0x00 },
	{ ADAU1761_PLAY_LR_MIXER_RIGHT,		0x00 },
	{ ADAU1761_PLAY_MIXER_MONO,		0x00 },
	{ ADAU1761_PLAY_HP_LEFT_VOL,		0x00 },
	{ ADAU1761_PLAY_HP_RIGHT_VOL,		0x00 },
	{ ADAU1761_PLAY_LINE_LEFT_VOL,		0x00 },
	{ ADAU1761_PLAY_LINE_RIGHT_VOL,		0x00 },
	{ ADAU1761_PLAY_MONO_OUTPUT_VOL,	0x00 },
	{ ADAU1761_POP_CLICK_SUPPRESS,		0x00 },
	{ ADAU1761_JACK_DETECT_PIN,		0x00 },
	{ ADAU1761_CLK_ENABLE0,			0x00 },
	{ ADAU1761_CLK_ENABLE1,			0x00 },
	{ ADAU17X1_CLOCK_CONTROL,		0x00 },
	{ ADAU17X1_PLL_CONTROL,			0x00 },
	{ ADAU17X1_REC_POWER_MGMT,		0x00 },
	{ ADAU17X1_MICBIAS,			0x00 },
	{ ADAU17X1_SERIAL_PORT0,		0x00 },
	{ ADAU17X1_SERIAL_PORT1,		0x00 },
	{ ADAU17X1_CONVERTER0,			0x00 },
	{ ADAU17X1_CONVERTER1,			0x00 },
	{ ADAU17X1_LEFT_INPUT_DIGITAL_VOL,	0x00 },
	{ ADAU17X1_RIGHT_INPUT_DIGITAL_VOL,	0x00 },
	{ ADAU17X1_ADC_CONTROL,			0x00 },
	{ ADAU17X1_PLAY_POWER_MGMT,		0x00 },
	{ ADAU17X1_DAC_CONTROL0,		0x00 },
	{ ADAU17X1_DAC_CONTROL1,		0x00 },
	{ ADAU17X1_DAC_CONTROL2,		0x00 },
	{ ADAU17X1_SERIAL_PORT_PAD,		0xaa },
	{ ADAU17X1_CONTROL_PORT_PAD0,		0xaa },
	{ ADAU17X1_CONTROL_PORT_PAD1,		0x00 },
	{ ADAU17X1_DSP_SAMPLING_RATE,		0x01 },
	{ ADAU17X1_SERIAL_INPUT_ROUTE,		0x00 },
	{ ADAU17X1_SERIAL_OUTPUT_ROUTE,		0x00 },
	{ ADAU17X1_DSP_ENABLE,			0x00 },
	{ ADAU17X1_DSP_RUN,			0x00 },
	{ ADAU17X1_SERIAL_SAMPLING_RATE,	0x00 },
};

static const DECLARE_TLV_DB_SCALE(adau1761_sing_in_tlv, -1500, 300, 1);
static const DECLARE_TLV_DB_SCALE(adau1761_diff_in_tlv, -1200, 75, 0);
static const DECLARE_TLV_DB_SCALE(adau1761_out_tlv, -5700, 100, 0);
static const DECLARE_TLV_DB_SCALE(adau1761_sidetone_tlv, -1800, 300, 1);
static const DECLARE_TLV_DB_SCALE(adau1761_boost_tlv, -600, 600, 1);
static const DECLARE_TLV_DB_SCALE(adau1761_pga_boost_tlv, -2000, 2000, 1);

static const unsigned int adau1761_bias_select_values[] = {
	0, 2, 3,
};

static const char * const adau1761_bias_select_text[] = {
	"Normal operation", "Enhanced performance", "Power saving",
};

static const char * const adau1761_bias_select_extreme_text[] = {
	"Normal operation", "Extreme power saving", "Enhanced performance",
	"Power saving",
};

static SOC_ENUM_SINGLE_DECL(adau1761_adc_bias_enum,
		ADAU17X1_REC_POWER_MGMT, 3, adau1761_bias_select_extreme_text);
static SOC_ENUM_SINGLE_DECL(adau1761_hp_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 6, adau1761_bias_select_extreme_text);
static SOC_ENUM_SINGLE_DECL(adau1761_dac_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 4, adau1761_bias_select_extreme_text);
static SOC_VALUE_ENUM_SINGLE_DECL(adau1761_playback_bias_enum,
		ADAU17X1_PLAY_POWER_MGMT, 2, 0x3, adau1761_bias_select_text,
		adau1761_bias_select_values);
static SOC_VALUE_ENUM_SINGLE_DECL(adau1761_capture_bias_enum,
		ADAU17X1_REC_POWER_MGMT, 1, 0x3, adau1761_bias_select_text,
		adau1761_bias_select_values);

static const struct snd_kcontrol_new adau1761_jack_detect_controls[] = {
	SOC_SINGLE("Speaker Auto-mute Switch", ADAU1761_DIGMIC_JACKDETECT,
		4, 1, 0),
};

static const struct snd_kcontrol_new adau1761_differential_mode_controls[] = {
	SOC_DOUBLE_R_TLV("Capture Volume", ADAU1761_LEFT_DIFF_INPUT_VOL,
		ADAU1761_RIGHT_DIFF_INPUT_VOL, 2, 0x3f, 0,
		adau1761_diff_in_tlv),
	SOC_DOUBLE_R("Capture Switch", ADAU1761_LEFT_DIFF_INPUT_VOL,
		ADAU1761_RIGHT_DIFF_INPUT_VOL, 1, 1, 0),

	SOC_DOUBLE_R_TLV("PGA Boost Capture Volume", ADAU1761_REC_MIXER_LEFT1,
		ADAU1761_REC_MIXER_RIGHT1, 3, 2, 0, adau1761_pga_boost_tlv),
};

static const struct snd_kcontrol_new adau1761_single_mode_controls[] = {
	SOC_SINGLE_TLV("Input 1 Capture Volume", ADAU1761_REC_MIXER_LEFT0,
		4, 7, 0, adau1761_sing_in_tlv),
	SOC_SINGLE_TLV("Input 2 Capture Volume", ADAU1761_REC_MIXER_LEFT0,
		1, 7, 0, adau1761_sing_in_tlv),
	SOC_SINGLE_TLV("Input 3 Capture Volume", ADAU1761_REC_MIXER_RIGHT0,
		4, 7, 0, adau1761_sing_in_tlv),
	SOC_SINGLE_TLV("Input 4 Capture Volume", ADAU1761_REC_MIXER_RIGHT0,
		1, 7, 0, adau1761_sing_in_tlv),
};

static const struct snd_kcontrol_new adau1761_controls[] = {
	SOC_DOUBLE_R_TLV("Aux Capture Volume", ADAU1761_REC_MIXER_LEFT1,
		ADAU1761_REC_MIXER_RIGHT1, 0, 7, 0, adau1761_sing_in_tlv),

	SOC_DOUBLE_R_TLV("Headphone Playback Volume", ADAU1761_PLAY_HP_LEFT_VOL,
		ADAU1761_PLAY_HP_RIGHT_VOL, 2, 0x3f, 0, adau1761_out_tlv),
	SOC_DOUBLE_R("Headphone Playback Switch", ADAU1761_PLAY_HP_LEFT_VOL,
		ADAU1761_PLAY_HP_RIGHT_VOL, 1, 1, 0),
	SOC_DOUBLE_R_TLV("Lineout Playback Volume", ADAU1761_PLAY_LINE_LEFT_VOL,
		ADAU1761_PLAY_LINE_RIGHT_VOL, 2, 0x3f, 0, adau1761_out_tlv),
	SOC_DOUBLE_R("Lineout Playback Switch", ADAU1761_PLAY_LINE_LEFT_VOL,
		ADAU1761_PLAY_LINE_RIGHT_VOL, 1, 1, 0),

	SOC_ENUM("ADC Bias", adau1761_adc_bias_enum),
	SOC_ENUM("DAC Bias", adau1761_dac_bias_enum),
	SOC_ENUM("Capture Bias", adau1761_capture_bias_enum),
	SOC_ENUM("Playback Bias", adau1761_playback_bias_enum),
	SOC_ENUM("Headphone Bias", adau1761_hp_bias_enum),
};

static const struct snd_kcontrol_new adau1761_mono_controls[] = {
	SOC_SINGLE_TLV("Mono Playback Volume", ADAU1761_PLAY_MONO_OUTPUT_VOL,
		2, 0x3f, 0, adau1761_out_tlv),
	SOC_SINGLE("Mono Playback Switch", ADAU1761_PLAY_MONO_OUTPUT_VOL,
		1, 1, 0),
};

static const struct snd_kcontrol_new adau1761_left_mixer_controls[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Left DAC Switch",
		ADAU1761_PLAY_MIXER_LEFT0, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("Right DAC Switch",
		ADAU1761_PLAY_MIXER_LEFT0, 6, 1, 0),
	SOC_DAPM_SINGLE_TLV("Aux Bypass Volume",
		ADAU1761_PLAY_MIXER_LEFT0, 1, 8, 0, adau1761_sidetone_tlv),
	SOC_DAPM_SINGLE_TLV("Right Bypass Volume",
		ADAU1761_PLAY_MIXER_LEFT1, 4, 8, 0, adau1761_sidetone_tlv),
	SOC_DAPM_SINGLE_TLV("Left Bypass Volume",
		ADAU1761_PLAY_MIXER_LEFT1, 0, 8, 0, adau1761_sidetone_tlv),
};

static const struct snd_kcontrol_new adau1761_right_mixer_controls[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Left DAC Switch",
		ADAU1761_PLAY_MIXER_RIGHT0, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("Right DAC Switch",
		ADAU1761_PLAY_MIXER_RIGHT0, 6, 1, 0),
	SOC_DAPM_SINGLE_TLV("Aux Bypass Volume",
		ADAU1761_PLAY_MIXER_RIGHT0, 1, 8, 0, adau1761_sidetone_tlv),
	SOC_DAPM_SINGLE_TLV("Right Bypass Volume",
		ADAU1761_PLAY_MIXER_RIGHT1, 4, 8, 0, adau1761_sidetone_tlv),
	SOC_DAPM_SINGLE_TLV("Left Bypass Volume",
		ADAU1761_PLAY_MIXER_RIGHT1, 0, 8, 0, adau1761_sidetone_tlv),
};

static const struct snd_kcontrol_new adau1761_left_lr_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("Left Volume",
		ADAU1761_PLAY_LR_MIXER_LEFT, 1, 2, 0, adau1761_boost_tlv),
	SOC_DAPM_SINGLE_TLV("Right Volume",
		ADAU1761_PLAY_LR_MIXER_LEFT, 3, 2, 0, adau1761_boost_tlv),
};

static const struct snd_kcontrol_new adau1761_right_lr_mixer_controls[] = {
	SOC_DAPM_SINGLE_TLV("Left Volume",
		ADAU1761_PLAY_LR_MIXER_RIGHT, 1, 2, 0, adau1761_boost_tlv),
	SOC_DAPM_SINGLE_TLV("Right Volume",
		ADAU1761_PLAY_LR_MIXER_RIGHT, 3, 2, 0, adau1761_boost_tlv),
};

static const char * const adau1761_input_mux_text[] = {
	"ADC", "DMIC",
};

static SOC_ENUM_SINGLE_DECL(adau1761_input_mux_enum,
	ADAU17X1_ADC_CONTROL, 2, adau1761_input_mux_text);

static const struct snd_kcontrol_new adau1761_input_mux_control =
	SOC_DAPM_ENUM("Input Select", adau1761_input_mux_enum);

static int adau1761_dejitter_fixup(struct snd_soc_dapm_widget *w,
	struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_codec *codec = snd_soc_dapm_to_codec(w->dapm);
	struct adau *adau = snd_soc_codec_get_drvdata(codec);

	/* After any power changes have been made the dejitter circuit
	 * has to be reinitialized. */
	regmap_write(adau->regmap, ADAU1761_DEJITTER, 0);
	if (!adau->master)
		regmap_write(adau->regmap, ADAU1761_DEJITTER, 3);

	return 0;
}

static const struct snd_soc_dapm_widget adau1x61_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Left Input Mixer", ADAU1761_REC_MIXER_LEFT0, 0, 0,
		NULL, 0),
	SND_SOC_DAPM_MIXER("Right Input Mixer", ADAU1761_REC_MIXER_RIGHT0, 0, 0,
		NULL, 0),

	SOC_MIXER_ARRAY("Left Playback Mixer", ADAU1761_PLAY_MIXER_LEFT0,
		0, 0, adau1761_left_mixer_controls),
	SOC_MIXER_ARRAY("Right Playback Mixer", ADAU1761_PLAY_MIXER_RIGHT0,
		0, 0, adau1761_right_mixer_controls),
	SOC_MIXER_ARRAY("Left LR Playback Mixer", ADAU1761_PLAY_LR_MIXER_LEFT,
		0, 0, adau1761_left_lr_mixer_controls),
	SOC_MIXER_ARRAY("Right LR Playback Mixer", ADAU1761_PLAY_LR_MIXER_RIGHT,
		0, 0, adau1761_right_lr_mixer_controls),

	SND_SOC_DAPM_SUPPLY("Headphone", ADAU1761_PLAY_HP_LEFT_VOL,
		0, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("SYSCLK", 2, SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_POST("Dejitter fixup", adau1761_dejitter_fixup),

	SND_SOC_DAPM_INPUT("LAUX"),
	SND_SOC_DAPM_INPUT("RAUX"),
	SND_SOC_DAPM_INPUT("LINP"),
	SND_SOC_DAPM_INPUT("LINN"),
	SND_SOC_DAPM_INPUT("RINP"),
	SND_SOC_DAPM_INPUT("RINN"),

	SND_SOC_DAPM_OUTPUT("LOUT"),
	SND_SOC_DAPM_OUTPUT("ROUT"),
	SND_SOC_DAPM_OUTPUT("LHP"),
	SND_SOC_DAPM_OUTPUT("RHP"),
};

static const struct snd_soc_dapm_widget adau1761_mono_dapm_widgets[] = {
	SND_SOC_DAPM_MIXER("Mono Playback Mixer", ADAU1761_PLAY_MIXER_MONO,
		0, 0, NULL, 0),

	SND_SOC_DAPM_OUTPUT("MONOOUT"),
};

static const struct snd_soc_dapm_widget adau1761_capless_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY_S("Headphone VGND", 1, ADAU1761_PLAY_MIXER_MONO,
		0, 0, NULL, 0),
};

static const struct snd_soc_dapm_route adau1x61_dapm_routes[] = {
	{ "Left Input Mixer", NULL, "LINP" },
	{ "Left Input Mixer", NULL, "LINN" },
	{ "Left Input Mixer", NULL, "LAUX" },

	{ "Right Input Mixer", NULL, "RINP" },
	{ "Right Input Mixer", NULL, "RINN" },
	{ "Right Input Mixer", NULL, "RAUX" },

	{ "Left Playback Mixer", NULL, "Left Playback Enable"},
	{ "Right Playback Mixer", NULL, "Right Playback Enable"},
	{ "Left LR Playback Mixer", NULL, "Left Playback Enable"},
	{ "Right LR Playback Mixer", NULL, "Right Playback Enable"},

	{ "Left Playback Mixer", "Left DAC Switch", "Left DAC" },
	{ "Left Playback Mixer", "Right DAC Switch", "Right DAC" },

	{ "Right Playback Mixer", "Left DAC Switch", "Left DAC" },
	{ "Right Playback Mixer", "Right DAC Switch", "Right DAC" },

	{ "Left LR Playback Mixer", "Left Volume", "Left Playback Mixer" },
	{ "Left LR Playback Mixer", "Right Volume", "Right Playback Mixer" },

	{ "Right LR Playback Mixer", "Left Volume", "Left Playback Mixer" },
	{ "Right LR Playback Mixer", "Right Volume", "Right Playback Mixer" },

	{ "LHP", NULL, "Left Playback Mixer" },
	{ "RHP", NULL, "Right Playback Mixer" },

	{ "LHP", NULL, "Headphone" },
	{ "RHP", NULL, "Headphone" },

	{ "LOUT", NULL, "Left LR Playback Mixer" },
	{ "ROUT", NULL, "Right LR Playback Mixer" },

	{ "Left Playback Mixer", "Aux Bypass Volume", "LAUX" },
	{ "Left Playback Mixer", "Left Bypass Volume", "Left Input Mixer" },
	{ "Left Playback Mixer", "Right Bypass Volume", "Right Input Mixer" },
	{ "Right Playback Mixer", "Aux Bypass Volume", "RAUX" },
	{ "Right Playback Mixer", "Left Bypass Volume", "Left Input Mixer" },
	{ "Right Playback Mixer", "Right Bypass Volume", "Right Input Mixer" },
};

static const struct snd_soc_dapm_route adau1761_mono_dapm_routes[] = {
	{ "Mono Playback Mixer", NULL, "Left Playback Mixer" },
	{ "Mono Playback Mixer", NULL, "Right Playback Mixer" },

	{ "MONOOUT", NULL, "Mono Playback Mixer" },
};

static const struct snd_soc_dapm_route adau1761_capless_dapm_routes[] = {
	{ "Headphone", NULL, "Headphone VGND" },
};

static const struct snd_soc_dapm_widget adau1761_dmic_widgets[] = {
	SND_SOC_DAPM_MUX("Left Decimator Mux", SND_SOC_NOPM, 0, 0,
		&adau1761_input_mux_control),
	SND_SOC_DAPM_MUX("Right Decimator Mux", SND_SOC_NOPM, 0, 0,
		&adau1761_input_mux_control),

	SND_SOC_DAPM_INPUT("DMIC"),
};

static const struct snd_soc_dapm_route adau1761_dmic_routes[] = {
	{ "Left Decimator Mux", "ADC", "Left Input Mixer" },
	{ "Left Decimator Mux", "DMIC", "DMIC" },
	{ "Right Decimator Mux", "ADC", "Right Input Mixer" },
	{ "Right Decimator Mux", "DMIC", "DMIC" },

	{ "Left Decimator", NULL, "Left Decimator Mux" },
	{ "Right Decimator", NULL, "Right Decimator Mux" },
};

static const struct snd_soc_dapm_route adau1761_no_dmic_routes[] = {
	{ "Left Decimator", NULL, "Left Input Mixer" },
	{ "Right Decimator", NULL, "Right Input Mixer" },
};

static const struct snd_soc_dapm_widget adau1761_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("Serial Port Clock", ADAU1761_CLK_ENABLE0,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Serial Input Routing Clock", ADAU1761_CLK_ENABLE0,
		1, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Serial Output Routing Clock", ADAU1761_CLK_ENABLE0,
		3, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Decimator Resync Clock", ADAU1761_CLK_ENABLE0,
		4, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("Interpolator Resync Clock", ADAU1761_CLK_ENABLE0,
		2, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY("Slew Clock", ADAU1761_CLK_ENABLE0, 6, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY("ALC Clock", ADAU1761_CLK_ENABLE0, 5, 0, NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("Digital Clock 0", 1, ADAU1761_CLK_ENABLE1,
		0, 0, NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("Digital Clock 1", 1, ADAU1761_CLK_ENABLE1,
		1, 0, NULL, 0),
};

static const struct snd_soc_dapm_route adau1761_dapm_routes[] = {
	{ "Left Decimator", NULL, "Digital Clock 0", },
	{ "Right Decimator", NULL, "Digital Clock 0", },
	{ "Left DAC", NULL, "Digital Clock 0", },
	{ "Right DAC", NULL, "Digital Clock 0", },

	{ "AIFCLK", NULL, "Digital Clock 1" },

	{ "Playback", NULL, "Serial Port Clock" },
	{ "Capture", NULL, "Serial Port Clock" },
	{ "Playback", NULL, "Serial Input Routing Clock" },
	{ "Capture", NULL, "Serial Output Routing Clock" },

	{ "Left Decimator", NULL, "Decimator Resync Clock" },
	{ "Right Decimator", NULL, "Decimator Resync Clock" },
	{ "Left DAC", NULL, "Interpolator Resync Clock" },
	{ "Right DAC", NULL, "Interpolator Resync Clock" },

	{ "DSP", NULL, "Digital Clock 0" },

	{ "Slew Clock", NULL, "Digital Clock 0" },
	{ "Right Playback Mixer", NULL, "Slew Clock" },
	{ "Left Playback Mixer", NULL, "Slew Clock" },

	{ "Left Input Mixer", NULL, "ALC Clock" },
	{ "Right Input Mixer", NULL, "ALC Clock" },

	{ "Digital Clock 0", NULL, "SYSCLK" },
	{ "Digital Clock 1", NULL, "SYSCLK" },
};

static int adau1761_set_bias_level(struct snd_soc_codec *codec,
				 enum snd_soc_bias_level level)
{
	struct adau *adau = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN);
		break;
	case SND_SOC_BIAS_OFF:
		regmap_update_bits(adau->regmap, ADAU17X1_CLOCK_CONTROL,
			ADAU17X1_CLOCK_CONTROL_SYSCLK_EN, 0);
		break;

	}
	
	dapm->bias_level = level;
	return 0;
}

static enum adau1761_output_mode adau1761_get_lineout_mode(
	struct snd_soc_codec *codec)
{
	struct adau1761_platform_data *pdata = codec->dev->platform_data;

	if (pdata)
		return pdata->lineout_mode;

	return ADAU1761_OUTPUT_MODE_LINE;
}

static int adau1761_setup_digmic_jackdetect(struct snd_soc_codec *codec)
{
	struct adau1761_platform_data *pdata = codec->dev->platform_data;
	struct adau *adau = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	enum adau1761_digmic_jackdet_pin_mode mode;
	unsigned int val = 0;
	int ret;

	if (pdata)
		mode = pdata->digmic_jackdetect_pin_mode;
	else
		mode = ADAU1761_DIGMIC_JACKDET_PIN_MODE_NONE;

	switch (mode) {
	case ADAU1761_DIGMIC_JACKDET_PIN_MODE_JACKDETECT:
		switch (pdata->jackdetect_debounce_time) {
		case ADAU1761_JACKDETECT_DEBOUNCE_5MS:
		case ADAU1761_JACKDETECT_DEBOUNCE_10MS:
		case ADAU1761_JACKDETECT_DEBOUNCE_20MS:
		case ADAU1761_JACKDETECT_DEBOUNCE_40MS:
			val |= pdata->jackdetect_debounce_time << 6;
			break;
		default:
			return -EINVAL;
		}
		if (pdata->jackdetect_active_low)
			val |= ADAU1761_DIGMIC_JACKDETECT_ACTIVE_LOW;

		ret = snd_soc_add_codec_controls(codec,
			adau1761_jack_detect_controls,
			ARRAY_SIZE(adau1761_jack_detect_controls));
		if (ret)
			return ret;
	case ADAU1761_DIGMIC_JACKDET_PIN_MODE_NONE: /* fallthrough */
		ret = snd_soc_dapm_add_routes(dapm,
			adau1761_no_dmic_routes,
			ARRAY_SIZE(adau1761_no_dmic_routes));
		if (ret)
			return ret;
		break;
	case ADAU1761_DIGMIC_JACKDET_PIN_MODE_DIGMIC:
		ret = snd_soc_dapm_new_controls(dapm,
			adau1761_dmic_widgets,
			ARRAY_SIZE(adau1761_dmic_widgets));
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm,
			adau1761_dmic_routes,
			ARRAY_SIZE(adau1761_dmic_routes));
		if (ret)
			return ret;

		val |= ADAU1761_DIGMIC_JACKDETECT_DIGMIC;
		break;
	default:
		return -EINVAL;
	}

	regmap_write(adau->regmap, ADAU1761_DIGMIC_JACKDETECT, val);

	return 0;
}

static int adau1761_setup_headphone_mode(struct snd_soc_codec *codec)
{
	struct adau *adau = snd_soc_codec_get_drvdata(codec);
	struct adau1761_platform_data *pdata = codec->dev->platform_data;
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	enum adau1761_output_mode mode;
	int ret;

	if (pdata)
		mode = pdata->headphone_mode;
	else
		mode = ADAU1761_OUTPUT_MODE_HEADPHONE;

	switch (mode) {
	case ADAU1761_OUTPUT_MODE_LINE:
		break;
	case ADAU1761_OUTPUT_MODE_HEADPHONE_CAPLESS:
		regmap_update_bits(adau->regmap, ADAU1761_PLAY_MONO_OUTPUT_VOL,
			ADAU1761_PLAY_MONO_OUTPUT_VOL_MODE_HP |
			ADAU1761_PLAY_MONO_OUTPUT_VOL_UNMUTE,
			ADAU1761_PLAY_MONO_OUTPUT_VOL_MODE_HP |
			ADAU1761_PLAY_MONO_OUTPUT_VOL_UNMUTE);
		/* fallthrough */
	case ADAU1761_OUTPUT_MODE_HEADPHONE:
		regmap_update_bits(adau->regmap, ADAU1761_PLAY_HP_RIGHT_VOL,
			ADAU1761_PLAY_HP_RIGHT_VOL_MODE_HP,
			ADAU1761_PLAY_HP_RIGHT_VOL_MODE_HP);
		break;
	default:
		return -EINVAL;
	}

	if (mode == ADAU1761_OUTPUT_MODE_HEADPHONE_CAPLESS) {
		ret = snd_soc_dapm_new_controls(dapm,
			adau1761_capless_dapm_widgets,
			ARRAY_SIZE(adau1761_capless_dapm_widgets));
		if (ret)
			return ret;
		ret = snd_soc_dapm_add_routes(dapm,
			adau1761_capless_dapm_routes,
			ARRAY_SIZE(adau1761_capless_dapm_routes));
	} else {
		ret = snd_soc_add_codec_controls(codec, adau1761_mono_controls,
			ARRAY_SIZE(adau1761_mono_controls));
		if (ret)
			return ret;
		ret = snd_soc_dapm_new_controls(dapm,
			adau1761_mono_dapm_widgets,
			ARRAY_SIZE(adau1761_mono_dapm_widgets));
		if (ret)
			return ret;
		ret = snd_soc_dapm_add_routes(dapm,
			adau1761_mono_dapm_routes,
			ARRAY_SIZE(adau1761_mono_dapm_routes));
	}

	return ret;
}

static bool adau1761_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case ADAU1761_DIGMIC_JACKDETECT:
	case ADAU1761_REC_MIXER_LEFT0:
	case ADAU1761_REC_MIXER_LEFT1:
	case ADAU1761_REC_MIXER_RIGHT0:
	case ADAU1761_REC_MIXER_RIGHT1:
	case ADAU1761_LEFT_DIFF_INPUT_VOL:
	case ADAU1761_RIGHT_DIFF_INPUT_VOL:
	case ADAU1761_PLAY_LR_MIXER_LEFT:
	case ADAU1761_PLAY_MIXER_LEFT0:
	case ADAU1761_PLAY_MIXER_LEFT1:
	case ADAU1761_PLAY_MIXER_RIGHT0:
	case ADAU1761_PLAY_MIXER_RIGHT1:
	case ADAU1761_PLAY_LR_MIXER_RIGHT:
	case ADAU1761_PLAY_MIXER_MONO:
	case ADAU1761_PLAY_HP_LEFT_VOL:
	case ADAU1761_PLAY_HP_RIGHT_VOL:
	case ADAU1761_PLAY_LINE_LEFT_VOL:
	case ADAU1761_PLAY_LINE_RIGHT_VOL:
	case ADAU1761_PLAY_MONO_OUTPUT_VOL:
	case ADAU1761_POP_CLICK_SUPPRESS:
	case ADAU1761_JACK_DETECT_PIN:
	case ADAU1761_DEJITTER:
	case ADAU1761_CLK_ENABLE0:
	case ADAU1761_CLK_ENABLE1:
		return true;
	default:
		break;
	}

	return adau17x1_readable_register(dev, reg);
}

static int adau1761_codec_probe(struct snd_soc_codec *codec)
{
	struct adau1761_platform_data *pdata = codec->dev->platform_data;
	struct adau *adau = snd_soc_codec_get_drvdata(codec);
	struct snd_soc_dapm_context *dapm = snd_soc_codec_get_dapm(codec);
	int ret;

	ADAU_DBG(" in %s\n",__func__);
	
	ret = adau17x1_add_widgets(codec);
	if (ret < 0)
		return ret;

	if (pdata && pdata->input_differential) {
		regmap_update_bits(adau->regmap, ADAU1761_LEFT_DIFF_INPUT_VOL,
			ADAU1761_DIFF_INPUT_VOL_LDEN,
			ADAU1761_DIFF_INPUT_VOL_LDEN);
		regmap_update_bits(adau->regmap, ADAU1761_RIGHT_DIFF_INPUT_VOL,
			ADAU1761_DIFF_INPUT_VOL_LDEN,
			ADAU1761_DIFF_INPUT_VOL_LDEN);
		ret = snd_soc_add_codec_controls(codec,
			adau1761_differential_mode_controls,
			ARRAY_SIZE(adau1761_differential_mode_controls));
		if (ret)
			return ret;
	} else {
		ret = snd_soc_add_codec_controls(codec,
			adau1761_single_mode_controls,
			ARRAY_SIZE(adau1761_single_mode_controls));
		if (ret)
			return ret;
	}

	switch (adau1761_get_lineout_mode(codec)) {
	case ADAU1761_OUTPUT_MODE_LINE:
		break;
	case ADAU1761_OUTPUT_MODE_HEADPHONE:
		regmap_update_bits(adau->regmap, ADAU1761_PLAY_LINE_LEFT_VOL,
			ADAU1761_PLAY_LINE_LEFT_VOL_MODE_HP,
			ADAU1761_PLAY_LINE_LEFT_VOL_MODE_HP);
		regmap_update_bits(adau->regmap, ADAU1761_PLAY_LINE_RIGHT_VOL,
			ADAU1761_PLAY_LINE_RIGHT_VOL_MODE_HP,
			ADAU1761_PLAY_LINE_RIGHT_VOL_MODE_HP);
		break;
	default:
		return -EINVAL;
	}

	ret = adau1761_setup_headphone_mode(codec);
	if (ret)
		return ret;

	ret = adau1761_setup_digmic_jackdetect(codec);
	if (ret)
		return ret;

	if (adau->type == ADAU1761) {
		ret = snd_soc_dapm_new_controls(dapm,
			adau1761_dapm_widgets,
			ARRAY_SIZE(adau1761_dapm_widgets));
		if (ret)
			return ret;

		ret = snd_soc_dapm_add_routes(dapm,
			adau1761_dapm_routes,
			ARRAY_SIZE(adau1761_dapm_routes));
		if (ret)
			return ret;
	}

	ret = adau17x1_add_routes(codec);
	if (ret < 0)
		return ret;


	ADAU_DBG(" out %s\n",__func__);
	return 0;
}

static const struct snd_soc_codec_driver adau1761_codec_driver = {
	.probe = adau1761_codec_probe,
	.resume	= adau17x1_resume,
	.set_bias_level	= adau1761_set_bias_level,
	.suspend_bias_off = true,

	.component_driver = {
		.controls = adau1761_controls,
		.num_controls = ARRAY_SIZE(adau1761_controls),
		.dapm_widgets = adau1x61_dapm_widgets,
		.num_dapm_widgets = ARRAY_SIZE(adau1x61_dapm_widgets),
		.dapm_routes = adau1x61_dapm_routes,
		.num_dapm_routes = ARRAY_SIZE(adau1x61_dapm_routes),
	},
};

#define ADAU1761_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE )
/*#define ADAU1761_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE | \
	SNDRV_PCM_FMTBIT_S32_LE)*/
static struct snd_soc_dai_driver adau1361_dai_driver = {
	.name = "adau-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ADAU1761_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 4,
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = ADAU1761_FORMATS,
	},
	.ops = &adau17x1_dai_ops,
};

int sigmadsp_write_i2c(void *control_data,
	unsigned int addr,  size_t len,const uint8_t data[])
{
	uint8_t *buf;
	int ret;

	buf = kzalloc(2 + len, GFP_KERNEL | GFP_DMA);
	if (!buf)
		return -ENOMEM;

	put_unaligned_be16(addr, buf);
	memcpy(buf + 2, data, len);

	ret = i2c_master_send ( control_data , buf , len + 2 );

	kfree(buf);

	return ret;
}
EXPORT_SYMBOL_GPL(sigmadsp_write_i2c);
int SIGMA_WRITE_REGISTER_BLOCK(unsigned int IC_addr,unsigned int addr,  size_t len,const uint8_t data[])
{
	int ret ;
	ret = sigmadsp_write_i2c(local_client,addr, len, data);
	if( ret < 0 ) printk(" dsp write failed !!\n");
	
 	return ret;
}
int SIGMA_WRITE_DELAY(unsigned int IC_addr, size_t len,const uint8_t data[] )
{
	mdelay(10);
	return 0;
}

static int adau1761_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	ADAU_DBG(" in %s\n",__func__);

	return 0;
}
static int adau1761_set_dai_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	ADAU_DBG(" in %s\n",__func__);
	return 0;
}
static int adau1761_set_dai_fmt(struct snd_soc_dai *dai,
		unsigned int fmt)
{
	ADAU_DBG(" in %s\n",__func__);
	return 0;
}

static int adau1761_startup(struct snd_pcm_substream *substream,
	struct snd_soc_dai *dai)
{	
	ADAU_DBG(" in %s %s\n",__func__,substream->stream == SNDRV_PCM_STREAM_CAPTURE ? "Capture":"Playback");

	return 0;
}
static int adau1761_set_dai_pll(struct snd_soc_dai *dai, int pll_id,
	int source, unsigned int freq_in, unsigned int freq_out)
{
	ADAU_DBG(" in %s\n",__func__);
	return 0;
}
static int adau1761_set_dai_tdm_slot(struct snd_soc_dai *dai,
	unsigned int tx_mask, unsigned int rx_mask, int slots, int slot_width)
{
	ADAU_DBG(" in %s : tx:%x rx:%x :",__func__,tx_mask,rx_mask);

	if( tx_mask == USERCOMMAND ) {
		
		switch (rx_mask) {


		case USERCOMMAND: {
			ADAU_DBG("volume set ");
			sigmadsp_write_i2c(local_client,0x0010,4,DSP_Volume[slots]);
			sigmadsp_write_i2c(local_client,0x0011,4, DSP_Volume[slots]);
		};break;
		case USERCOMMAND_MUTELOUT: {
			ADAU_DBG("mute lineout");
			sigmadsp_write_i2c( local_client, 0x4000, 1, CLK_PLL );
			sigmadsp_write_i2c(local_client,0x402B, 1,DAC_CHAIN);
			sigmadsp_write_i2c(local_client,0x402C, 1,DAC_CHAIN);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_LINE_LEFT_VOL, 1, LineOut[0]);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_LINE_RIGHT_VOL, 1, LineOut[0]);
			
		};break;
		case USERCOMMAND_UNMUTELOUT: {
			ADAU_DBG("unmute lineout");
			sigmadsp_write_i2c( local_client, 0x4000, 1, CLK_PLL );
			sigmadsp_write_i2c(local_client,0x402B, 1,DAC_CHAIN);
			sigmadsp_write_i2c(local_client,0x402C, 1,DAC_CHAIN);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_LINE_LEFT_VOL, 1, LineOut[1]);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_LINE_RIGHT_VOL, 1, LineOut[1]);
		};break;
		case USERCOMMAND_MUTEHP: {
			ADAU_DBG("mute headphone");
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_HP_LEFT_VOL, 1, HEADPHONE_L[0]);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_HP_RIGHT_VOL, 1, HEADPHONE_R[0]);
		};break;
		case USERCOMMAND_UNMUTEHP: {
			ADAU_DBG("unmute headphone");
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_HP_LEFT_VOL, 1, HEADPHONE_L[1]);
			sigmadsp_write_i2c(local_client,ADAU1761_PLAY_HP_RIGHT_VOL, 1, HEADPHONE_R[1]);
		};break;
		case USERCOMMAND_MUTELMIC: {
			ADAU_DBG("mute local mic");
			sigmadsp_write_i2c(local_client,0x400B,1,MIC_MIXER[0]);
			//sigmadsp_write_i2c(local_client,0x400D,1,MIC_MIXER[0]);
			//sigmadsp_write_i2c(local_client,0x400E,1,MIC_VOLUME_CTL_L[0]);
			//sigmadsp_write_i2c(local_client,0x400F,1,MIC_VOLUME_CTL_R[0]);
		};break;
		case USERCOMMAND_UNMUTELMIC: {
			ADAU_DBG("unmute local mic");
			sigmadsp_write_i2c(local_client,0x400B,1,MIC_MIXER[1]);

		};break;
		case USERCOMMAND_MUTEHPMIC: {
			ADAU_DBG("mute headphone mic");
			sigmadsp_write_i2c(local_client,0x400D,1,MIC_MIXER[0]);

		};break;

		case USERCOMMAND_UNMUTEHPMIC: {
			ADAU_DBG("unmute headphone mic");
			sigmadsp_write_i2c(local_client,0x400D,1,MIC_MIXER[1]);

		};break;

		case USERCOMMAND_INIT: {
			sigmadsp_write_i2c(local_client,0x4002,6,DISABLEPLL[0]); /*disable pll*/
			msleep(50);
			sigmadsp_write_i2c(local_client,0x4002,6,DISABLEPLL[1]); /*enable pll*/
			msleep(50);
			ADAU_DBG("init codec");
			default_download_IC_1();
			sigmadsp_write_i2c( local_client, 0x4000, 1, CLK_PLL );
			sigmadsp_write_i2c(local_client,0x402B, 1,DAC_CHAIN);
			sigmadsp_write_i2c(local_client,0x402C, 1,DAC_CHAIN);
		};break;
		
		case USERCOMMAND_MICBIASEN: {
			ADAU_DBG("enable mic bias");
			sigmadsp_write_i2c(local_client,0x4010,1,MIC_BIAS[0]);

		};break;
		
		case USERCOMMAND_MICBIASDISEN: {
			ADAU_DBG("disable mic bias");
			sigmadsp_write_i2c(local_client,0x4010,1,MIC_BIAS[1]);

		};break;
		case USERCOMMAND_MODESWITCH: {
			ADAU_DBG("mode switch ");
			switch(slots){
			case TOY_SAVETY_MODE: {
				ADAU_DBG(" toy safety mode\n");
				sigmadsp_write_i2c(local_client,TOY_SAVETY_MODE , 4 , MODESWITCHON);
				sigmadsp_write_i2c(local_client,NORMAL_MODE , 4  , MODESWITCHOFF);
				sigmadsp_write_i2c(local_client,HEADHPONE_MODE , 4  , MODESWITCHOFF);

			} ;break;
			case NORMAL_MODE: {
				ADAU_DBG(" normal mode\n");
				sigmadsp_write_i2c(local_client,TOY_SAVETY_MODE , 4 , MODESWITCHOFF);
				sigmadsp_write_i2c(local_client,NORMAL_MODE , 4  , MODESWITCHON);
				sigmadsp_write_i2c(local_client,HEADHPONE_MODE , 4  , MODESWITCHOFF);
			};break;
			case HEADHPONE_MODE:{ 		
				ADAU_DBG(" headphone mode\n");
				sigmadsp_write_i2c(local_client,TOY_SAVETY_MODE , 4 , MODESWITCHOFF);
				sigmadsp_write_i2c(local_client,NORMAL_MODE , 4  , MODESWITCHOFF);
				sigmadsp_write_i2c(local_client,HEADHPONE_MODE , 4  , MODESWITCHON);
				if(slot_width == TOY_SAVETY_MODE ) {
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR11 , 4 , 
						hp_gain_targit[HP_GAIN_TOYSAFTY]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR12 , 8 , 
						hp_gain_alpha[HP_GAIN_TOYSAFTY]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR21 , 4 , 
						hp_gain_targit[HP_GAIN_TOYSAFTY]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR22 , 8 , 
						hp_gain_alpha[HP_GAIN_TOYSAFTY]);
					
				} else {
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR11 , 4 , 
						hp_gain_targit[HP_GAIN_NORMAL]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR12 , 8 , 
						hp_gain_alpha[HP_GAIN_NORMAL]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR21 , 4 , 
						hp_gain_targit[HP_GAIN_NORMAL]);
					sigmadsp_write_i2c(local_client,
						HP_VOLME_ADDR22 , 8 , 
						hp_gain_alpha[HP_GAIN_NORMAL]);
				}
	
			};break;

			}
		};break;

		default:
			ADAU_DBG("unknow command");
			break;
		}
		
	}
	ADAU_DBG("\n");
	
	return 0;
}
const struct snd_soc_dai_ops adau1761_dai_ops = {
	.hw_params	= adau1761_hw_params,
	.set_sysclk	= adau1761_set_dai_sysclk,
	.set_fmt	= adau1761_set_dai_fmt,
	.set_pll	= adau1761_set_dai_pll,
	.set_tdm_slot	= adau1761_set_dai_tdm_slot,
	.startup	= adau1761_startup,
};

static struct snd_soc_dai_driver adau1761_dai_driver = {
	.name = "adau1761-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_32000|\
				SNDRV_PCM_RATE_44100|SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_96000,
		.formats = ADAU1761_FORMATS,
	},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates =  SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|SNDRV_PCM_RATE_32000|\
				SNDRV_PCM_RATE_44100|SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_96000,
		.formats = ADAU1761_FORMATS,
	},
#ifndef	USE_17X1_OPS
	.ops = &adau1761_dai_ops,
#else
	.ops = &adau17x1_dai_ops,
#endif
};
static struct adau1761_platform_data adau_pdata = {  
   	.input_differential = true,  
     	.lineout_mode = ADAU1761_OUTPUT_MODE_LINE,  
     	.headphone_mode = ADAU1761_OUTPUT_MODE_HEADPHONE,  
   	.digmic_jackdetect_pin_mode = ADAU1761_DIGMIC_JACKDET_PIN_MODE_NONE,  
};  
int adau1761_probe(struct device *dev, struct regmap *regmap,
	enum adau17x1_type type, void (*switch_mode)(struct device *dev))
{
	struct snd_soc_dai_driver *dai_drv;
	const char *firmware_name;
	int ret =0;
	ADAU_DBG(" in %s\n",__func__);

	if (!dev->platform_data)  
       	dev->platform_data = &adau_pdata;
	
	if (type == ADAU1361) {
		dai_drv = &adau1361_dai_driver;
		firmware_name = NULL;
	} else {
		dai_drv = &adau1761_dai_driver;
		firmware_name = ADAU1761_FIRMWARE;
	}

	ret = adau17x1_probe(dev, regmap, type, switch_mode, firmware_name);
	if (ret)
		return ret;
	ret = snd_soc_register_codec(dev, &adau1761_codec_driver, dai_drv, 1);
	if (ret)
		return ret;

	ADAU_DBG(" out %s\n",__func__);
	return ret;
}


const struct regmap_config adau1761_regmap_config = {
	.val_bits = 8,
	.reg_bits = 16,
	.max_register = 0x40fa,
	.reg_defaults = adau1761_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(adau1761_reg_defaults),
	.readable_reg = adau1761_readable_register,
	.volatile_reg = adau17x1_volatile_register,
	.precious_reg = adau17x1_precious_register,
	.cache_type = REGCACHE_RBTREE,
};

static int adau1761_i2c_probe(struct i2c_client *client,
							  const struct i2c_device_id *id)
{
	struct regmap_config config;
	ADAU_DBG(" in %s\n",__func__);
	
	config = adau1761_regmap_config;
	config.val_bits = 8;
	config.reg_bits = 16;
	local_client = client;
	return adau1761_probe(&client->dev,
						  devm_regmap_init_i2c(client, &config),
						  id->driver_data, NULL);
}

static int adau1761_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id adau1761_i2c_ids[] = {
	{ "adau1761", ADAU1761},
	{ }
};
MODULE_DEVICE_TABLE(i2c, adau1761_i2c_ids);

#if defined(CONFIG_OF)
static const struct of_device_id adau1761_i2c_dt_ids[] = {
	{ .compatible = "sigma,adau1761", },
	{ },
};
MODULE_DEVICE_TABLE(of, adau1761_i2c_dt_ids);
#endif

static struct i2c_driver adau1761_i2c_driver = {
	.driver = {
		.name = "adau1761",
		.of_match_table = of_match_ptr(adau1761_i2c_dt_ids),
	},
	.probe = adau1761_i2c_probe,
	.remove = adau1761_i2c_remove,
	.id_table = adau1761_i2c_ids,
};
module_i2c_driver(adau1761_i2c_driver);

MODULE_DESCRIPTION("ASoC ADAU1361/ADAU1461/ADAU1761/ADAU1961 CODEC driver");
MODULE_AUTHOR("kean.wu@hansong-china.com");
MODULE_LICENSE("GPL");
