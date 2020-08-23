/*
 * Copyright (C) 2014 Ingenic Semiconductor Co., Ltd.
 *	http://www.ingenic.com
 * Author: cli <chen.li@ingenic.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include "../as-v1/asoc-aic.h"

struct x1000_icdc {
	struct device *dev;
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
	int spk_gpio;
	int spk_en_level;
};

int x1000_i2s_hw_params(struct snd_pcm_substream *substream, struct snd_pcm_hw_params *params) {
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret;

	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_I2S|SND_SOC_DAIFMT_CBM_CFM);
	if (ret)
		return ret;

	ret = snd_soc_dai_set_sysclk(cpu_dai, INGENIC_I2S_INNER_CODEC,
								 24000000, SND_SOC_CLOCK_OUT);
	if (ret)
		return ret;
	return 0;
};

int x1000_i2s_hw_free(struct snd_pcm_substream *substream)
{
	/*notify release pll*/
	return 0;
};

static struct snd_soc_ops x1000_i2s_cdc_ops = {
	.hw_params = x1000_i2s_hw_params,
	.hw_free = x1000_i2s_hw_free,
};

static int x1000_spk_power(struct snd_soc_dapm_widget *w, struct snd_kcontrol *kcontrol, int event)
{
	struct x1000_icdc *x1000 = container_of(w->dapm->card, struct x1000_icdc, card);

	if (!gpio_is_valid(x1000->spk_gpio))
		return 0;

	if (SND_SOC_DAPM_EVENT_ON(event)) {
		gpio_direction_output(x1000->spk_gpio, x1000->spk_en_level);
		printk("gpio speaker enable %d\n", gpio_get_value(x1000->spk_gpio));
	} else {
		gpio_direction_output(x1000->spk_gpio, x1000->spk_en_level);
		printk("gpio speaker disable %d\n", gpio_get_value(x1000->spk_gpio));
	}
	return 0;
}

static const struct snd_soc_dapm_widget x1000_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", x1000_spk_power),
	SND_SOC_DAPM_MIC("Mic Buildin", NULL),
	SND_SOC_DAPM_MIC("DMic", NULL),
};

static int x1000_i2s_cdc_dai_link_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = &card->dapm;

	snd_soc_dapm_enable_pin(dapm, "Speaker");
	snd_soc_dapm_enable_pin(dapm, "Mic Buildin");
	snd_soc_dapm_enable_pin(dapm, "DMic");
	return 0;
}

static int snd_x1000_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *cpu_np = NULL, *platform_np = NULL, *codec_np = NULL;
	struct platform_device *cpu_pdev;
	struct x1000_icdc *x1000;
	struct snd_soc_dai_link_component *comp;
	int ret;
	struct device *dev = &pdev->dev;
    enum of_gpio_flags spk_gpio_flags;

    pr_info("snd_x1000_probe\n");
    
	cpu_np = of_parse_phandle(pdev->dev.of_node, "ingenic,cpu-dai", 0);
	platform_np = of_parse_phandle(pdev->dev.of_node, "ingenic,platform", 0);
	codec_np = of_parse_phandle(pdev->dev.of_node, "ingenic,codec", 0);
	if (!cpu_np || !platform_np || !codec_np) {
		dev_err(dev, "phandle missing or invalid\n");
		ret = -EINVAL;
		goto fail;
	}

	cpu_pdev = of_find_device_by_node(cpu_np);
	if (!cpu_pdev) {
		dev_err(dev, "failed to find CPU DAI platform device\n");
		ret = -EINVAL;
		goto fail;
	}

	x1000 = devm_kzalloc(&pdev->dev, sizeof(*x1000), GFP_KERNEL);
	if (!x1000)
		return -ENOMEM;
    
	comp = devm_kzalloc(dev, 3 * sizeof(*comp), GFP_KERNEL);
	if (!comp) {
		ret = -ENOMEM;
		goto fail;
	}

	x1000->dev = dev;

    x1000->spk_gpio = of_get_named_gpio_flags(np, "ingenic,spken-gpio", 0, &spk_gpio_flags);
    if (gpio_is_valid(x1000->spk_gpio)) {
		unsigned long init_flags;
		x1000->spk_en_level = (spk_gpio_flags == OF_GPIO_ACTIVE_LOW ? 0 : 1);
		init_flags = (spk_gpio_flags == OF_GPIO_ACTIVE_LOW ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW);
		ret = devm_gpio_request_one(x1000->dev, x1000->spk_gpio, init_flags, "speaker_en");
		if (ret)
			pr_warn("dorado speaker enable pin(%d) request failed\n", x1000->spk_gpio);
		else
			pr_info("dorado speaker enable pin(%d) request ok\n", x1000->spk_gpio);
	}
	
	x1000->dai.cpus		= &comp[0];
	x1000->dai.codecs	= &comp[1];
	x1000->dai.platforms	= &comp[2];

	x1000->dai.num_cpus	= 1;
	x1000->dai.num_codecs	= 1;
	x1000->dai.num_platforms	= 1;

	x1000->dai.name = "ingenic-hifi";
	x1000->dai.stream_name = "ingenic-hifi-stream";
	x1000->dai.codecs->dai_name = "ingenic-hifi-analog";
	x1000->dai.codecs->of_node = codec_np;
	x1000->dai.cpus->of_node = cpu_np;
	x1000->dai.platforms->of_node = platform_np;
    
    /* for i2s only */
	x1000->dai.init = &x1000_i2s_cdc_dai_link_init;
    x1000->dai.ops = &x1000_i2s_cdc_ops;
	x1000->dai.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;
                
	x1000->card.dev = dev;
	x1000->card.num_dapm_widgets = ARRAY_SIZE(x1000_dapm_widgets);
	x1000->card.dapm_widgets = x1000_dapm_widgets;
	ret = snd_soc_of_parse_card_name(&x1000->card, "ingenic,model");
	if (ret) {
		dev_err(dev, "Unable to parse card name\n");
		goto fail;
	}
	ret = snd_soc_of_parse_audio_routing(&x1000->card, "ingenic,audio-routing");
	if (ret) {
		dev_err(dev, "Unable to parse routing: %d\n", ret);
		goto fail;
	}
	x1000->card.num_links = 1;
	x1000->card.owner = THIS_MODULE;
	x1000->card.dai_link = &x1000->dai;

    
#if 0
	struct device_node *snd_node = pdev->dev.of_node;
	struct x1000_icdc *x1000;
	struct snd_soc_card *card;
	struct snd_soc_dai_link *dai_link;
	enum of_gpio_flags flags;
	int num_links;
	int ret = 0, i;

    num_links = of_property_count_strings(snd_node, "ingenic,dai-link");
	if (num_links < 0)
		return num_links;
	BUG_ON(!num_links);

	x1000 = (struct x1000_icdc *)devm_kzalloc(&pdev->dev,
			sizeof(struct x1000_icdc) +
			sizeof(struct snd_soc_dai_link) * num_links,
			GFP_KERNEL);
	if (!x1000)
		return -ENOMEM;
	card = &x1000->card;
	dai_link = (struct snd_soc_dai_link *)(x1000 + 1);

	card->num_dapm_widgets = ARRAY_SIZE(x1000_dapm_widgets);
	card->dapm_widgets = x1000_dapm_widgets;
	card->num_links = num_links;
	card->dai_link = dai_link;
	card->owner = THIS_MODULE;
	card->dev = &pdev->dev;

	ret = snd_soc_of_parse_card_name(card, "ingenic,model");
	if (ret)
		return ret;
#ifdef CONFIG_SND_ASOC_INGENIC_HALLEY2_ICDC
	ret = snd_soc_of_parse_audio_routing(card, "ingenic,audio-routing");
	if (ret)
		return ret;
#endif
	x1000->spk_gpio = of_get_named_gpio_flags(card->dev->of_node, "ingenic,spken-gpio", 0, &flags);
	if (gpio_is_valid(x1000->spk_gpio)) {
		unsigned long init_flags;
		x1000->spk_en_level = (flags == OF_GPIO_ACTIVE_LOW ? 0 : 1);
		init_flags = (flags == OF_GPIO_ACTIVE_LOW ? GPIOF_OUT_INIT_HIGH : GPIOF_OUT_INIT_LOW);
		ret = devm_gpio_request_one(card->dev, x1000->spk_gpio, init_flags, "Speaker_en");
		if (ret)
			pr_warn("dorado speaker enable pin(%d) request failed\n",x1000->spk_gpio);
		else
			pr_info("dorado speaker enable pin(%d) request ok\n", x1000->spk_gpio);
	}

	for (i = 0; i < card->num_links; i++) {
		dai_link[i].cpu_of_node = of_parse_phandle(snd_node, "ingenic,cpu-dai" , i);
		dai_link[i].platform_of_node = of_parse_phandle(snd_node, "ingenic,platform", i);
		dai_link[i].codec_of_node = of_parse_phandle(snd_node, "ingenic,codec", i);
		ret = of_property_read_string_index(snd_node, "ingenic,codec-dai", i,
				&(dai_link[i].codec_dai_name));
		if (ret || !dai_link[i].cpu_of_node ||
				!dai_link[i].codec_of_node ||
				!dai_link[i].platform_of_node)
			return -ENODEV;
		ret = of_property_read_string_index(snd_node, "ingenic,dai-link", i,
				&(dai_link[i].name));
		if (ret)
			return -ENODEV;
		ret = of_property_read_string_index(snd_node, "ingenic,stream", i,
				&(dai_link[i].stream_name));
		if (ret)
			dai_link[i].stream_name = dai_link[i].name;

		dev_dbg(&pdev->dev, "dai_link %s\n", dai_link[i].name);
		dev_dbg(&pdev->dev, "stream_name %s\n", dai_link[i].stream_name);
		dev_dbg(&pdev->dev, "cpu %s(%s)\n", dai_link[i].cpu_of_node->name,
				dai_link[i].cpu_of_node->full_name);
		dev_dbg(&pdev->dev, "platform %s(%s)\n", dai_link[i].platform_of_node->name,
				dai_link[i].platform_of_node->full_name);
		dev_dbg(&pdev->dev, "codec dai %s\n", dai_link[i].codec_dai_name);
		dev_dbg(&pdev->dev, "codec %s(%s)\n", dai_link[i].codec_of_node->name,
				dai_link[i].codec_of_node->full_name);

		if (!strcmp(dai_link[i].name, "i2s-icdc") ||
				!strcmp(dai_link[i].codec_dai_name, "icdc-d3-hifi")) {
			dai_link->ops = &x1000_i2s_cdc_ops;
			dai_link->init = x1000_i2s_cdc_dai_link_init;
		}
	}
#endif



	ret = snd_soc_register_card(&x1000->card);
	if (ret) {
		dev_err(dev, "snd_soc_register_card failed %d\n", ret);
		goto fail;
	}

	platform_set_drvdata(pdev, x1000);
	dev_info(dev, "Sound Card success\n");
    
fail:
	of_node_put(cpu_np);
	of_node_put(platform_np);
	of_node_put(codec_np);    

	return ret;
}

static int snd_x1000_remove(struct platform_device *pdev)
{
	struct x1000_icdc *x1000 = platform_get_drvdata(pdev);
	snd_soc_unregister_card(&x1000->card);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static const struct of_device_id sound_dt_match[] = {
	{ .compatible = "ingenic,x1000-sound", .data = NULL },
	{},
};
MODULE_DEVICE_TABLE(of, sound_dt_match);

static struct platform_driver snd_x1000_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "x1000-sound",
		.of_match_table = sound_dt_match,
	},
	.probe = snd_x1000_probe,
	.remove = snd_x1000_remove,
};
module_platform_driver(snd_x1000_driver);

MODULE_AUTHOR("sccheng<shicheng.cheng@ingenic.com>");
MODULE_DESCRIPTION("ALSA SoC x1000 Snd Card");
MODULE_LICENSE("GPL");
