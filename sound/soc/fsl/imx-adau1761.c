/*
 * Copyright (C) 2014 Freescale Semiconductor, Inc.
 *
 * Based on imx-sgtl5000.c
 * Copyright (C) 2012 Freescale Semiconductor, Inc.
 * Copyright (C) 2012 Linaro Ltd.
 *
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <linux/pinctrl/consumer.h>
#include <linux/delay.h>


#include "../codecs/adau_1761-i2c.h"

#include "imx-audmux.h"
#include "imx-ssi.h"

#define AMPMUTEOFF 1
#define AMPMUTEON 0
#define AMPBOOSTENON 1
#define AMPBOOSTENOFF 0


#define FOR_JOOKI_MP

#define AMPSDON 1
#define AMPSDOFF 0


#define DAI_NAME_SIZE	32
#define AMP_PIN_NUMBER 4

#define TOY_SAVETY_MODE  	0x01B5
#define NORMAL_MODE        	0x01B6
#define HEADHPONE_MODE      0x01B7


struct imx_adau1761_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	char codec_dai_name[DAI_NAME_SIZE];
	char platform_name[DAI_NAME_SIZE];
	struct i2c_client *codec_dev;
	/* audio_clocking_data */
	struct clk *pll;
	struct clk *clock_root;
	struct gpio_desc *gpios[AMP_PIN_NUMBER];
	long sysclk;
	long current_rate;
	/* platfor data */
	unsigned int ssi_num;
	unsigned int src_port;
	unsigned int ext_port;
	int jeckdetirq; 
	int irqenable;  /*0:disenble 1:endable*/
	int jecksetenable;/*0:AMP out 1: Headphone out*/
	int dspmode; /*0:normal mode  1:toy safety mode*/
	int playstatus; /*-1:init_statu  0:not playing 1: playing*/
	int stream; /*0:playback 1:capture*/
	struct work_struct adauwq;
};

static int imx_adau1761_init(struct snd_soc_pcm_runtime *rtd);
static int imx_hifi_hw_params_slv_mode(struct snd_pcm_substream *substream,
                                       struct snd_pcm_hw_params *params);
static void imx_hifi_shutdown(struct snd_pcm_substream *substream);

// this hack is in place because currently there is something wrong in the 48kHz playback mode
#define RATE_CONSTRAIN_HACK
#ifdef RATE_CONSTRAIN_HACK
static int imx_hifi_startup(struct snd_pcm_substream *substream);
#endif

struct imx_priv {
	struct platform_device *pdev;
	struct imx_adau1761_data *info;
};

static struct imx_priv adau_priv;

static struct snd_soc_ops imx_hifi_ops = {
#ifdef RATE_CONSTRAIN_HACK
	.startup   = imx_hifi_startup,
#endif
	.shutdown	= imx_hifi_shutdown,
	.hw_params	= imx_hifi_hw_params_slv_mode,
};

/* imx card dapm widgets */
static const struct snd_soc_dapm_widget imx_dapm_widgets[] = {
     SND_SOC_DAPM_SPK("Line Out", NULL),  
     SND_SOC_DAPM_HP("Headphone Out", NULL),  
     SND_SOC_DAPM_MIC("LMic In", NULL),  
     SND_SOC_DAPM_MIC("HMic In", NULL),  
};

/* imx machine connections to the codec pins */
static const struct snd_soc_dapm_route audio_map[] = {
     { "Line Out", NULL, "LOUT" },  
     { "Line Out", NULL, "ROUT" },  
     { "Headphone Out", NULL, "LHP" },  
     { "Headphone Out", NULL, "RHP" },  
//     { "LMic In", NULL, "LINN" },  
//     { "HMic In", NULL, "RINN" },  
//     { "LINN", NULL, "Mic In" },  
//     { "RINN", NULL, "Mic In" },  
};


static void imx_adau1761_amp_enable(struct imx_adau1761_data *data, int en)
{
	struct gpio_desc *amp_mute = data->gpios[0]; 
	struct gpio_desc *amp_sd = data->gpios[1]; 
	struct gpio_desc *boosten = data->gpios[2]; 
    
	if(en) {
		ADAU_DBG("turn on amp\n");        


		// enable boost converter and un-mute amp
		gpiod_set_value(boosten, AMPBOOSTENON);

		// sleep until the supply is ready
		mdelay(10);
        					// enable the amplifier
		gpiod_set_value(amp_sd, AMPSDON);
				
		mdelay(200);

        	gpiod_set_value(amp_mute, AMPMUTEOFF);
		//mdelay(50);
		
	} else {
		ADAU_DBG("turn off amp\n");        
		// mute amp and disable boost converter
		gpiod_set_value(amp_mute, AMPMUTEON);

		mdelay(5);
		gpiod_set_value(amp_sd, AMPSDOFF);

		mdelay(5);
		gpiod_set_value(boosten, AMPBOOSTENOFF);

	}
}

static int adau1761_slv_mode_init(struct imx_adau1761_data *data)
{
	struct clk *new_parent;
	struct clk *ssi_clk;
	struct i2c_client *codec_dev = data->codec_dev;

	new_parent = devm_clk_get(&codec_dev->dev, "mclk");
	if (IS_ERR(new_parent)) {
		pr_err("Could not get \"mclk\" clock \n");
		return PTR_ERR(new_parent);
	}

	ssi_clk = devm_clk_get(&codec_dev->dev, "mclk");
	if (IS_ERR(ssi_clk)) {
		pr_err("Could not get \"mclk\" clock \n");
		return PTR_ERR(ssi_clk);
	}

	clk_set_parent(ssi_clk, new_parent);

	data->pll = new_parent;
	data->clock_root = ssi_clk;
	data->current_rate = 0;

	data->sysclk = 0;

	return 0;
}

static int adau1761_slv_mode_clock_enable(int enable, struct imx_adau1761_data *data)
{
	long pll_rate;
	long rate_req;
	long rate_avail;
	if (!enable)
		return 0;

	if (data->sysclk == data->current_rate)
		return 0;

	switch (data->sysclk) {
		case 11289600:
			pll_rate = 632217600;
			break;

		case 12288000:
			pll_rate = 688128000;
			break;

		default:
			return -EINVAL;
	}

	rate_req = pll_rate;
	rate_avail = clk_round_rate(data->pll, rate_req);
	clk_set_rate(data->pll, rate_avail);

	rate_req = data->sysclk;
	rate_avail = clk_round_rate(data->clock_root,
								rate_req);
	clk_set_rate(data->clock_root, rate_avail);
	/*
	pr_info("%s: \"imx-ssi.1\" rate = %ld (= %ld)\n",
			__func__, rate_avail, rate_req);
	*/
	data->current_rate = data->sysclk;

	return 0;
}

static int imx_hifi_hw_params_slv_mode(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct imx_adau1761_data *data = snd_soc_card_get_drvdata(card);

	u32 dai_format;
	snd_pcm_format_t sample_format;
	unsigned int channels;
	unsigned int tx_mask, rx_mask;
	unsigned int sampling_rate;
	unsigned int div_2, div_psr, div_pm;
	int ret;

	sampling_rate = params_rate(params);
	sample_format = params_format(params);

	channels = params_channels(params);
	
	ADAU_DBG("%s:%s  sampling rate = %u  channels = %u \n", __FUNCTION__,
		   (substream->stream == SNDRV_PCM_STREAM_PLAYBACK ? "Playback" : "Capture"),
		   sampling_rate, channels);

	adau1761_slv_mode_clock_enable(1,data);

	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_IF |
		SND_SOC_DAIFMT_CBS_CFS;

	ret = snd_soc_dai_set_fmt(cpu_dai, dai_format);
	if ( ret < 0 ){
		ADAU_DBG(" %s:%d: failed!\n",__func__,__LINE__);
		return ret;
	}

	/* set i.MX active slot mask */
	/* S[TR]CCR:DC */
	tx_mask = ~((1 << channels) - 1);
	rx_mask = tx_mask;
	snd_soc_dai_set_tdm_slot(cpu_dai, tx_mask, rx_mask, 2, 32);

	/*
	 * SSI sysclk divider:
	 * div_2:	/1 or /2
	 * div_psr:	/1 or /8
	 * div_pm:	/1 .. /256
	 */
	div_2	= 0;
	div_psr	= 0;
	switch (sampling_rate) {
		case 8000:
			// 1x1x12
			div_pm	= 11;
			break;
		case 32000:
			// 1x1x3
			div_pm	= 2;
			break;
		case 48000:
			// 1x1x2
			div_pm	= 1;
			break;
		case 96000:
			// 1x1x1
			div_pm	= 0;
			break;
		case 44100:
			// 1x1x2
			div_pm	= 1;
			break;
		case 88200:
			// 1x1x1
			div_pm	= 0;
			break;
		default:
			return -EINVAL;
	}

	/* sync mode: a single clock controls both playback and capture */
	snd_soc_dai_set_clkdiv(cpu_dai, IMX_SSI_TX_DIV_2, (div_2 ? SSI_STCCR_DIV2 : 0));
	snd_soc_dai_set_clkdiv(cpu_dai, IMX_SSI_TX_DIV_PSR, (div_psr ? SSI_STCCR_PSR : 0));
	snd_soc_dai_set_clkdiv(cpu_dai, IMX_SSI_TX_DIV_PM, div_pm);
	
#if 0
	/* set codec DAI configuration */
	dai_format = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS;

	ret = snd_soc_dai_set_fmt(codec_dai, dai_format);
	if ( ret < 0 ){
		ADAU_DBG(" %s:%d: failed!\n",__func__,__LINE__);
		return ret;
	}
#endif
	/* set codec DAI configuration */
	switch (sampling_rate) {
		case 8000:
		case 32000:
		case 48000:
		case 96000:
			data->sysclk = 48000 * 1024;
			break;
		case 44100:
		case 88200:
			data->sysclk = 44100 * 1024;
			break;

		default:
			return -EINVAL;
	}
	ret = snd_soc_dai_set_pll (codec_dai, ADAU17X1_PLL,
			ADAU17X1_PLL_SRC_MCLK, 12288000, data->sysclk);
	if ( ret < 0 ) {
		ADAU_DBG(" %s:%d: failed!\n",__func__,__LINE__);
		return ret;
	}
	
	ret = snd_soc_dai_set_sysclk(codec_dai,
				     ADAU17X1_CLK_SRC_PLL,
				     data->sysclk,
				     SND_SOC_CLOCK_IN);
#ifndef USE_17X1_OPS
	if( data->jecksetenable < 0 ) {
		disable_irq(data->jeckdetirq);
		ret = gpiod_get_value(data->gpios[3]);
		if( ret == CODEC_OUTPUT_HEADPHONE ) {
			 ADAU_DBG(" gpio jeck detect high\n");
			 snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MUTELOUT, 0, 0); /*mute Lineout*/
			 snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_UNMUTEHP, 0, 0); /*unmute headphone*/
		} else {
			ADAU_DBG(" gpio jeck detect low\n");
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_UNMUTELOUT, 0, 0); /*unmute Lineout*/
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MUTEHP, 0, 0); /*mute headphone*/
		}
		data->jecksetenable = ret;
		enable_irq(data->jeckdetirq);
	}
#endif
	if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK ) {
		if( data->jecksetenable == CODEC_OUTPUT_LINEOUT && data->playstatus != -1)
			imx_adau1761_amp_enable(data, 1); /*enable amp out*/
	} else {
		snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MICBIASEN, 0, 0); /*mute Local MIC*/
		if( data->jecksetenable == CODEC_OUTPUT_HEADPHONE ) {
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_UNMUTEHPMIC, 0, 0); /*unmute  headphone MIC*/
		} else {
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_UNMUTELMIC, 0, 0); /*unmute Local MIC */
		}
	} 
	
	data->stream = substream->stream;
	data->playstatus =1;
	ADAU_DBG(" out imx_hifi_hw_params_slv_mode\n");
	return 0;
}

#ifdef RATE_CONSTRAIN_HACK
static u32 imx_adau1761_rates[] = { 44100 };
static struct snd_pcm_hw_constraint_list imx_adau1761_rate_constraints = {
	.count = ARRAY_SIZE(imx_adau1761_rates),
	.list = imx_adau1761_rates,
};

static int imx_hifi_startup(struct snd_pcm_substream *substream)
{

	int ret;
	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
																	 SNDRV_PCM_HW_PARAM_RATE, &imx_adau1761_rate_constraints);
		return ret;
}
#endif

static void imx_hifi_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_card *card = rtd->card;
	struct imx_adau1761_data *data = snd_soc_card_get_drvdata(card);

	if (!codec_dai->active)
		adau1761_slv_mode_clock_enable(0,data);
	if( substream->stream == SNDRV_PCM_STREAM_PLAYBACK ){
		if( data->jecksetenable == CODEC_OUTPUT_LINEOUT ) 
			//gpiod_direction_output(data->gpios[0], 0); /*mute amp out*/
			imx_adau1761_amp_enable(data, 0);
	} else {
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MICBIASDISEN, 0, 0); /*mute Local MIC*/
		if( data->jecksetenable == CODEC_OUTPUT_HEADPHONE ) {
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MUTEHPMIC, 0, 0); /*unmute  headphone MIC*/
		} else {
			snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_MUTELMIC, 0, 0); /*unmute Local MIC */
		}
	}
	data->playstatus = 0;
	//snd_soc_dai_set_tdm_slot(codec_dai, USERCOMMAND, USERCOMMAND_DISABLEPLL, 0, 0); /*Disable DSP PLL*/
	return;
}

static int imx_adau1761_init (struct snd_soc_pcm_runtime *rtd)
{
	int ret = 0;
	struct snd_soc_codec *codec = rtd->codec;

	/* Add imx specific widgets */
	ret = snd_soc_dapm_new_controls(&codec->dapm, imx_dapm_widgets,
									ARRAY_SIZE(imx_dapm_widgets));
	if (ret)
			goto out_retcode;

	/* Set up imx specific audio path audio_map */
	ret = snd_soc_dapm_add_routes(&codec->dapm, audio_map, ARRAY_SIZE(audio_map));
	if (ret)
			goto out_retcode;

	ret = snd_soc_dapm_enable_pin(&codec->dapm, "Headphone Out");
	if (ret)
			goto out_retcode;

	ret = snd_soc_dapm_nc_pin(&codec->dapm, "Line Out");
	if (ret)
			goto out_retcode;
#ifndef USE_17X1_OPS
	ret = snd_soc_dai_set_tdm_slot(rtd->codec_dai, USERCOMMAND, USERCOMMAND_INIT, 0, 0); /*init codec*/
	if ( ret < 0 ) {
		pr_err("Failed to set snd_soc_dai_set_tdm_slot  %d\n", ret);
	
		return ret;
	}
#endif

out_retcode:

	if (ret)
			pr_err("%s: failed with error code: %d \n", __FUNCTION__, ret);
	else
			pr_info("%s: success \n", __FUNCTION__);

	return ret;
}

/**
 * Configure AUDMUX interconnection between
 * _slave (CPU side) and _master (codec size)
 *
 * When SSI operates in master mode, 5-wire interconnect with
 * audio codec is required:
 * TXC  - BCLK
 * TXD  - DAC data
 * RXD  - ADC data
 * TXFS - {DAC|ADC}LRC, i.e. word clock
 * RXC  - MCLK, i.e. oversampling clock
 * Audmux is operated in asynchronous mode to enable 6-wire
 * interface (as opposed to 4-wire interface in sync mode).
 */
static int imx_audmux_config_slv_mode(int _slave, int _master)
{
	unsigned int ptcr, pdcr;
	int slave = _slave - 1;
	int master = _master - 1;

	ptcr = IMX_AUDMUX_V2_PTCR_SYN |
		IMX_AUDMUX_V2_PTCR_TFSDIR |
		IMX_AUDMUX_V2_PTCR_TFSEL(slave) |
		IMX_AUDMUX_V2_PTCR_RCLKDIR |
		IMX_AUDMUX_V2_PTCR_RCSEL(slave | 0x8) |
		IMX_AUDMUX_V2_PTCR_TCLKDIR |
		IMX_AUDMUX_V2_PTCR_TCSEL(slave);

	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(slave);
	imx_audmux_v2_configure_port(master, ptcr, pdcr);
	ptcr = ptcr & ~IMX_AUDMUX_V2_PTCR_SYN;
	imx_audmux_v2_configure_port(master, ptcr, pdcr);

	ptcr = IMX_AUDMUX_V2_PTCR_SYN |
		IMX_AUDMUX_V2_PTCR_RCLKDIR |
		IMX_AUDMUX_V2_PTCR_RCSEL(master | 0x8) |
		IMX_AUDMUX_V2_PTCR_TCLKDIR |
		IMX_AUDMUX_V2_PTCR_TCSEL(master);

	pdcr = IMX_AUDMUX_V2_PDCR_RXDSEL(master);
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);
	ptcr = ptcr & ~IMX_AUDMUX_V2_PTCR_SYN;
	imx_audmux_v2_configure_port(slave, ptcr, pdcr);

	return 0;
}

static int mastervolume =80;

int snd_pcm_info_volsw(struct snd_kcontrol *kcontrol,
		struct	snd_ctl_elem_info	*uinfo)
{

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;	//the info of elem count
	uinfo->value.integer.min = 0;				//the info of min value
	uinfo->value.integer.max = 100;				//the info of max value
	return	0;
}
int snd_switch_info_volsw(struct snd_kcontrol *kcontrol,
		struct	snd_ctl_elem_info	*uinfo)
{

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;

	uinfo->count = 1;	//the info of elem count
	uinfo->value.integer.min = 0;				//the info of min value
	uinfo->value.integer.max = 1;				//the info of max value
	return	0;
}
int snd_pcm_get_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{

	ucontrol->value.integer.value[0] = mastervolume;	
	ucontrol->value.integer.value[1] =0;

	return 0;
}
int snd_switch_get_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{

	ucontrol->value.integer.value[0] = adau_priv.info->dspmode;	
	ucontrol->value.integer.value[1] =0;

	return 0;
}
int snd_pcm_put_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{
	unsigned int tmp;
	unsigned char val;
	int ret ;
	tmp = ucontrol->value.integer.value[0] ;	
	//ucontrol->value.integer.value[1] =0;
	if( tmp > 100)
		tmp =100;
	mastervolume = tmp;
	val = tmp * 38 / 100;
	ret = snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai, USERCOMMAND, USERCOMMAND, val, val); /*mute Lineout*/
	if ( ret < 0 ) {
		pr_err("Failed to set snd_soc_dai_set_tdm_slot  %d\n", ret);
	
		return ret;
	}
	return 0;
}
int snd_amp_put_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{
	unsigned int tmp;

	tmp = ucontrol->value.integer.value[0];
	if(adau_priv.info->irqenable == 0) {
			ADAU_DBG("enable ject det irq\n");
			enable_irq(adau_priv.info->jeckdetirq);
			adau_priv.info->irqenable = 1;
	}

	return 0;
}	

int snd_switch_put_volsw(struct snd_kcontrol	*kcontrol,
		struct	snd_ctl_elem_value	*ucontrol)
{
	unsigned int tmp ,ret;
	if( adau_priv.info->jecksetenable ) return -1;
	
	tmp = ucontrol->value.integer.value[0];
	if(tmp >1 || tmp <0) return -1;
	
	if( tmp != 0 ) {
		ret = snd_soc_dai_set_tdm_slot(
			adau_priv.info->card.rtd->codec_dai,
			USERCOMMAND, USERCOMMAND_MODESWITCH,
			TOY_SAVETY_MODE, 0); /*enable toy safety*/
		if ( ret < 0 ) {
		pr_err("Failed to set snd_soc_dai_set_tdm_slot  %d\n", ret);
	
		return ret;
		}
		adau_priv.info->dspmode = 1;
	} else {
		
		ret = snd_soc_dai_set_tdm_slot(
			adau_priv.info->card.rtd->codec_dai, 
			USERCOMMAND, USERCOMMAND_MODESWITCH,
			NORMAL_MODE, 0); /*disable toy safety*/
		if ( ret < 0 ) {
		pr_err("Failed to set snd_soc_dai_set_tdm_slot  %d\n", ret);
	
		return ret;
		}
		adau_priv.info->dspmode = 0;
	}
	return 0;
}	


static const struct snd_kcontrol_new mastervolume_controls[] = {
#if 1
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		//.name = "Master Playback Volume" ,
		.name = "Master Volume" ,
		.info	= snd_pcm_info_volsw,
		.get = snd_pcm_get_volsw,
		.put	= snd_pcm_put_volsw,
		.private_value	= 1,
	},
#endif
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		//.name = "Master Playback Volume" ,
		.name = "AmpHeadphone" ,
		.info	= snd_pcm_info_volsw,
		.get=snd_pcm_get_volsw,
		.put	= snd_amp_put_volsw,
		.private_value	= 1,
	},
	{
		.iface	= SNDRV_CTL_ELEM_IFACE_MIXER,
		//.name = "Master Playback Volume" ,
		.name = "DSPTOYSAVETY" ,
		.info	= snd_switch_info_volsw,
		.get=snd_switch_get_volsw,
		.put	= snd_switch_put_volsw,
		.private_value	= 1,
	},
	
};

irqreturn_t jeck_handler(int sig,void * data)
{
	int value;
	struct imx_adau1761_data *adaudata = (struct imx_adau1761_data *)data;
	disable_irq_nosync(sig);
	value = gpiod_get_value(adaudata->gpios[3]);
	ADAU_DBG("jeck sigle come in value:%d\n",value);	
	
	if(adaudata->jecksetenable != value ) {
		adaudata->jecksetenable = value;
		if( adau_priv.info->playstatus == 1 && adau_priv.info->stream == SNDRV_PCM_STREAM_PLAYBACK )
        	{
			//gpiod_direction_output(adau_priv.info->gpios[0], value ? 0:1); /*value=1:mute amp out; value=0:unmute amp out*/	
			//imx_adau1761_amp_enable(adau_priv.info, );
			imx_adau1761_amp_enable(data, value ? 0:1);
			
       	 }
		//mdelay(100);
		schedule_work(&adaudata->adauwq);
	}
	enable_irq(sig);
	return IRQ_HANDLED;

}

void workquerehandler(struct work_struct *data)
{
	if( adau_priv.info->jecksetenable ) {

		 snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
		 	USERCOMMAND, USERCOMMAND_MUTELOUT, 0, 0); /*mute Lineout*/
		 snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai, 
		 	USERCOMMAND, USERCOMMAND_UNMUTEHP, 0, 0); /*unmute headphone*/
		 snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai, 
		 	USERCOMMAND, USERCOMMAND_MUTELMIC, 0, 0); /*mute Local MIC*/
		 snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
		 	USERCOMMAND, USERCOMMAND_UNMUTEHPMIC, 0, 0); /*unmute  headphone MIC*/
		 snd_soc_dai_set_tdm_slot(
		 	adau_priv.info->card.rtd->codec_dai, 
		 	USERCOMMAND, USERCOMMAND_MODESWITCH,
		 	HEADHPONE_MODE, 0); /*switch to headphone mode*/
		 
	} else {
		if(adau_priv.info->dspmode >= 0 && adau_priv.info->dspmode < 2)
		snd_soc_dai_set_tdm_slot(
			adau_priv.info->card.rtd->codec_dai, 
		 	USERCOMMAND, USERCOMMAND_MODESWITCH, 
		 	adau_priv.info->dspmode ==1 ?  TOY_SAVETY_MODE : NORMAL_MODE , 
		 	0); /*out headphone mode*/
		
		snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
			USERCOMMAND, USERCOMMAND_UNMUTELOUT, 0, 0); /*unmute Lineout*/
		snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
			USERCOMMAND, USERCOMMAND_MUTEHP, 0, 0); /*mute headphone*/
		snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
			USERCOMMAND, USERCOMMAND_UNMUTELMIC, 0, 0); /*unmute Local MIC */
		snd_soc_dai_set_tdm_slot(adau_priv.info->card.rtd->codec_dai,
			USERCOMMAND, USERCOMMAND_MUTEHPMIC, 0, 0); /*mute headphone MIC*/


	}
}

static int imx_adau1761_probe(struct platform_device *pdev)
{
	struct device_node *ssi_np, *codec_np;
	struct platform_device *ssi_pdev;
	struct imx_priv *priv = &adau_priv;
	struct i2c_client *codec_dev;
	struct imx_adau1761_data *data;
	struct gpio_desc *amp_mute, *amp_sd, *boosten,*jeck_detect;
	int ret;
	
	ADAU_DBG( " in %s\n" , __func__ );
	
	priv->pdev = pdev;

	ssi_np = of_parse_phandle(pdev->dev.of_node, "cpu-dai", 0);
	codec_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	if (!ssi_np || !codec_np) {
		dev_err(&pdev->dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	codec_dev = of_find_i2c_device_by_node(codec_np);
	if (!codec_dev || !codec_dev->dev.driver) {
		dev_err(&pdev->dev, "failed to find codec platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	amp_mute = gpiod_get_index(&pdev->dev, "amp", 0);
	if(!amp_mute) {
		dev_err(&pdev->dev, "failed to get gpio amp mute\n");
		ret = -EINVAL;
		goto fail;
	}
	amp_sd = gpiod_get_index(&pdev->dev, "amp", 1);
	if(!amp_sd)
	{
		dev_err(&pdev->dev, "failed to get gpio amp sd\n");
		ret = -EINVAL;
		goto fail;
	}
	boosten = gpiod_get_index(&pdev->dev, "amp", 2);
	if(!boosten)
	{
		dev_err(&pdev->dev, "failed to get gpio boosten\n");
		ret = -EINVAL;
		goto fail;
	}
	jeck_detect = gpiod_get_index(&pdev->dev, "jeckdetect", 0);
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}
	
	adau_priv.info = data;
	
	data->codec_dev = codec_dev;

	data->dai.name = "adau1x61";
	data->dai.stream_name = "adau1x61";
	data->dai.codec_dai_name = "adau1761-hifi";
	data->dai.codec_of_node = codec_np;
	data->dai.cpu_dai_name = dev_name(&ssi_pdev->dev);
	data->dai.platform_of_node = ssi_np;
	data->dai.ops = &imx_hifi_ops;
	data->dai.init = &imx_adau1761_init;

	data->ssi_num = 2; /* 1-based */
	data->src_port = 2;
	data->ext_port = 4;

	data->gpios[0] = amp_mute;
	data->gpios[1] = amp_sd;
	data->gpios[2] = boosten;
	data->gpios[3] = jeck_detect;

	imx_audmux_config_slv_mode(data->src_port, data->ext_port);

	/* Slave Mode Init */
	adau1761_slv_mode_init(data);

	data->card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;

	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret)
		goto fail;

	data->card.num_links = 1;
	data->card.dai_link = &data->dai;

	data->card.controls = mastervolume_controls;
	data->card.num_controls=ARRAY_SIZE(mastervolume_controls);

	data->card.dapm_widgets = imx_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(imx_dapm_widgets);

    // mute amp and shutdown boost converter
    gpiod_direction_output(amp_mute, AMPMUTEON);
    gpiod_direction_output(amp_sd, AMPSDOFF);
    gpiod_direction_output(boosten, AMPBOOSTENOFF);
    gpiod_direction_input(jeck_detect);
    
#ifndef USE_17X1_OPS
	data->jeckdetirq = -1;
	data->jecksetenable = -1;
	data->playstatus =-1;
	data->dspmode =1;
	if ( jeck_detect )
	{
        int irqno;
		ADAU_DBG(" init irq\n");
		irqno = gpiod_to_irq(jeck_detect);
		
		if(irqno<0)
			ADAU_DBG(" failed to get gpio jeckdetect\n");
		else
			ret = request_irq(irqno, jeck_handler, IRQF_TRIGGER_RISING|IRQF_TRIGGER_FALLING, "ject_detect", data);
		if( ret<0 )
			ADAU_DBG(" failed to request ject irq \n");
		else  {
			data->jeckdetirq = irqno;
			disable_irq_nosync(data->jeckdetirq);
			data->irqenable = 0;
		}
	} else 
		ADAU_DBG(" failed to get gpio jeckdetect\n");
	
	INIT_WORK(&data->adauwq, workquerehandler);
#endif
	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);

	ret = snd_soc_register_card(&data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	return 0;

fail:
	if (ssi_np)
		of_node_put(ssi_np);

	if (codec_np)
		of_node_put(codec_np);

	return ret;
}

static int imx_adau1761_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	//gpiod_direction_input(adau_priv.info->gpios[1]);
	return 0;
}

static const struct of_device_id imx_adau1761_dt_ids[] = {
	{ .compatible = "fsl,imx-audio-adau1761", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, imx_adau1761_dt_ids);

static struct platform_driver imx_adau1761_driver = {
	.driver = {
		.name = "imx-adau1761",
		.owner = THIS_MODULE,
		.of_match_table = imx_adau1761_dt_ids,
	},
	.probe = imx_adau1761_probe,
	.remove = imx_adau1761_remove,
};
module_platform_driver(imx_adau1761_driver);

MODULE_AUTHOR("kean.wu@hansong-china.com");
MODULE_DESCRIPTION("Hansong i.MX ADAU1761 ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:imx-adau1761");
