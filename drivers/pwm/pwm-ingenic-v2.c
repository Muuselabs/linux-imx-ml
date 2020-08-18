/* drivers/pwm/pwm-ingenic-x2000.c
 * PWM driver of Ingenic's SoC X2000
 *
 * Copyright (C) 2015 Ingenic Semiconductor Co., Ltd.
 *	http://www.ingenic.com
 * Author:	sihui.liu <sihui.liu@ingenic.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/pwm.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/stat.h>
#include <linux/pwm.h>
#include <linux/time.h>
#include <asm/io.h>
#include <asm-generic/div64.h>

//#include <mach/ingenicpwm.h>

#define INGENIC_PWM_NUM		(16)
#define NS_IN_HZ	(1000000000UL)

#define DEFAULT_PWM_CLK_RATE (50000000)

struct ingenic_pwm_channel {
	u32 period_ns;
	u32 duty_ns;
};

struct ingenic_pwm_chip {
	struct pwm_chip chip;
	struct clk *clk_pwm;
	struct clk *clk_gate;
	void __iomem    *iomem;
	struct pwm_platform_data *pdata;
	struct mutex mutex;
	unsigned int output_mask;

	struct pwm_device *debug_pwm[INGENIC_PWM_NUM];
	int debug_current_id;
};

struct pwm_platform_data {
                unsigned int init_level;
};


/**-----------------------------------------------------------------------------
 **                       reg offset
 **-----------------------------------------------------------------------------*/

#define	PWM_CCFG0	0x0
#define	PWM_CCFG1	0x4
#define	PWM_ENS		0x10
#define	PWM_ENC		0x14
#define	PWM_EN		0x18
#define	PWM_UP		0x20
#define	PWM_BUSY	0x24
#define	PWM_PER		0x30
#define PWM_WCFG	0xb0

/**-----------------------------------------------------------------------------
 **                       reg bit field
 **-----------------------------------------------------------------------------*/

/**
 **		PWM Waveform Configuration Register (PWMWCFG)
 **/
#define PWM_WCFG_START	(16)
#define PWM_WCFG_END	(0)

static void pwm_writel(struct ingenic_pwm_chip *chip, unsigned int value, unsigned int offset)
{
	writel(value, chip->iomem + offset);
}

static unsigned int pwm_readl(struct ingenic_pwm_chip *chip, unsigned int offset)
{
	return readl(chip->iomem + offset);
}

static void pwm_clk_config(struct ingenic_pwm_chip *chip, unsigned int channel, unsigned int prescale)
{
	int tmp = 0;
	if(channel < 8) {
		tmp = pwm_readl(chip, PWM_CCFG0);
		tmp |= prescale << (channel * 4);
		pwm_writel(chip, tmp, PWM_CCFG0);
	} else {
		tmp = pwm_readl(chip, PWM_CCFG1);
		tmp |= prescale << ((channel - 8) * 4);
		pwm_writel(chip, tmp, PWM_CCFG1);
	}
}

static unsigned int pwm_get_prescale(struct ingenic_pwm_chip *chip, unsigned int channel)
{
	int tmp = 0;
	int prescale = 0;
	if(channel < 8) {
		tmp = pwm_readl(chip, PWM_CCFG0);
		prescale = tmp & (0xf << channel * 4);
	} else {
		tmp = pwm_readl(chip, PWM_CCFG1);
		prescale = tmp & (0xf << (channel - 8) * 4);
	}
	return prescale;
}

static void pwm_enable_hw(struct ingenic_pwm_chip *chip, unsigned int channel)
{
	int tmp = 0;
	tmp = pwm_readl(chip, PWM_EN);
	tmp |= (1 << channel);
	pwm_writel(chip, tmp, PWM_ENS);
}

static void pwm_disable_hw(struct ingenic_pwm_chip *chip, unsigned int channel)
{
	pwm_writel(chip, 1 << channel, PWM_ENC);
}

static int pwm_enable_status(struct ingenic_pwm_chip *chip)
{
	return pwm_readl(chip, PWM_EN);
}

static void pwm_update(struct ingenic_pwm_chip *chip, unsigned int channel)
{
	pwm_writel(chip, 1 << channel, PWM_UP);
}

static int pwm_busy(struct ingenic_pwm_chip *chip, unsigned int channel)
{
	int tmp = 0;
	tmp = pwm_readl(chip, PWM_BUSY);
	return tmp & (1 << channel);
}

static void pwm_set_period_hw(struct ingenic_pwm_chip *chip, unsigned int channel, unsigned int period)
{
	pwm_writel(chip, period, PWM_PER + channel * 4);
}

static void pwm_waveform_start(struct ingenic_pwm_chip *chip, unsigned int channel, unsigned int start)
{
	int tmp = 0;
	tmp = pwm_readl(chip, PWM_WCFG + channel * 4);
	tmp &= ~(0xffff << PWM_WCFG_START);
	tmp |= start << PWM_WCFG_START;
	pwm_writel(chip, tmp, PWM_WCFG + channel * 4);
}

static void pwm_waveform_end(struct ingenic_pwm_chip *chip, unsigned int channel, unsigned int end)
{
	int tmp = 0;
	tmp = pwm_readl(chip, PWM_WCFG + channel * 4);
	tmp &= ~(0xffff);
	tmp |= end << PWM_WCFG_END;
	pwm_writel(chip, tmp, PWM_WCFG + channel * 4);
}

static void dump_pwm_reg(struct ingenic_pwm_chip *chip)
{
	int i;
	printk("PWM_CCFG0 = %x\n", pwm_readl(chip, PWM_CCFG0));
	printk("PWM_CCFG1 = %x\n", pwm_readl(chip, PWM_CCFG1));
	printk("PWM_ENS = %x\n", pwm_readl(chip, PWM_ENS));
	printk("PWM_ENC = %x\n", pwm_readl(chip, PWM_ENC));
	printk("PWM_EN = %x\n", pwm_readl(chip, PWM_EN));
	printk("PWM_UP = %x\n", pwm_readl(chip, PWM_UP));
	printk("PWM_BUSY = %x\n", pwm_readl(chip, PWM_BUSY));
	for(i = 0; i < 16; i++) {
		printk("PWM_PER%d = %x\n", i, pwm_readl(chip, PWM_PER + i * 4));
	}
	for(i = 0; i < 16; i++) {
		printk("PWM_WCFG%d = %x\n", i, pwm_readl(chip, PWM_WCFG + i * 4));
	}
}

static inline struct ingenic_pwm_chip *to_ingenic_chip(struct pwm_chip *chip)
{
	return container_of(chip, struct ingenic_pwm_chip, chip);
}

static int ingenic_pwm_request(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ingenic_pwm_chip *our_chip = to_ingenic_chip(chip);
	struct ingenic_pwm_channel *our_chan;

	if (!(our_chip->output_mask & BIT(pwm->hwpwm))) {
		dev_warn(chip->dev,
			"tried to request PWM channel %d without output\n",
			pwm->hwpwm);
		return -EINVAL;
	}

	our_chan = devm_kzalloc(chip->dev, sizeof(*our_chan), GFP_KERNEL);
	if (!our_chan)
		return -ENOMEM;

	pwm_set_chip_data(pwm, our_chan);

	return 0;
}

static void ingenic_pwm_free(struct pwm_chip *chip, struct pwm_device *pwm)
{
	devm_kfree(chip->dev, pwm_get_chip_data(pwm));
	pwm_set_chip_data(pwm, NULL);
}

static int ingenic_pwm_config(struct pwm_chip *chip, struct pwm_device *pwm,
			     int duty_ns, int period_ns)
{
	struct ingenic_pwm_chip *our_chip = to_ingenic_chip(chip);
	struct ingenic_pwm_channel *chan = pwm_get_chip_data(pwm);
	int channel = pwm->hwpwm;
	unsigned int duty = 0;
	unsigned short start = 0;
	unsigned short end = 0;
	unsigned int pwm_freq = 0;
	unsigned int clk_in = 0;
	unsigned int prescale = 0;
	unsigned int tmp = 1;
	int period = 0;
	int i = 0;
	int ret = 0;
	unsigned long long clk_ns = 1000000000ULL;

	mutex_lock(&our_chip->mutex);

	dev_dbg(chip->dev,"duty_ns=%d  period_ns=%d\n", duty_ns, period_ns);

	if (duty_ns < 0 || duty_ns > period_ns) {
		pr_err("%s, duty_ns(%d)< 0 or duty_ns > period_ns(%d)\n", __func__, duty_ns, period_ns);
		ret = -EINVAL;
		goto out;
	}

	if(period_ns > NSEC_PER_SEC || duty_ns > NSEC_PER_SEC) {
		ret = -ERANGE;
		goto out;
	}

	if (period_ns == chan->period_ns && duty_ns == chan->duty_ns)
		goto out;

	clk_in = clk_get_rate(our_chip->clk_pwm);
	prescale = pwm_get_prescale(our_chip, channel);

	for (i = 0; i < prescale; i++) {
		tmp = 2 * tmp;
	}
	pwm_freq = clk_in / tmp;
	do_div(clk_ns, pwm_freq);
	dev_dbg(chip->dev,"clk_ns=%lld\n", clk_ns);

	if(chan->period_ns != period_ns) {
		period = (period_ns / (unsigned int)clk_ns) - 1;
		if(period <= 0) {
			ret = -ERANGE;
			goto out;
		}
		dev_dbg(chip->dev,"period=%d\n", period);
		pwm_set_period_hw(our_chip, channel, period);
	} else {
		period = (chan->period_ns / (unsigned int)clk_ns) - 1;
	}

	duty = duty_ns / (unsigned int)clk_ns;


	 /* PWM_POLARITY_NORMAL :

          |<- --------period ---->|
          |<- duty ->|            |

         -+----------+            +-
          |          |            |
         -+          +------------+
	*/

	/* PWM_POLARITY_NORMAL means high for duty, low for left pulse. */
	if(pwm->polarity == PWM_POLARITY_NORMAL) {
		start = 0;
		end = period - duty;
	} else {
		start = period - duty;
		end = 0;
	}
	dev_dbg(chip->dev,"duty=%d start=%d end=%d\n", duty, start, end);
	pwm_waveform_start(our_chip, channel, start);
	pwm_waveform_end(our_chip, channel, end);

	/* if Already enabled, only do an update config. */
	if(our_chip->chip.pwms[channel].flags & (1 << PWMF_ENABLED)) {
		int timeout = 0x10000;
		pwm_update(our_chip, channel);
		while(pwm_busy(our_chip, channel) && --timeout);
		if(!timeout) {
			dev_err(chip->dev, "PWM wait update timeout .\n");
		}
	}

	chan->period_ns = period_ns;
	chan->duty_ns = duty_ns;

out:
	mutex_unlock(&our_chip->mutex);

	return ret;
}

static int ingenic_pwm_enable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ingenic_pwm_chip *our_chip = to_ingenic_chip(chip);
	int channel = pwm->hwpwm;

	mutex_lock(&our_chip->mutex);

	pwm_enable_hw(our_chip, channel);

	mutex_unlock(&our_chip->mutex);

	return 0;
}

static void ingenic_pwm_disable(struct pwm_chip *chip, struct pwm_device *pwm)
{
	struct ingenic_pwm_chip *our_chip = to_ingenic_chip(chip);
	int channel = pwm->hwpwm;

	mutex_lock(&our_chip->mutex);

	pwm_disable_hw(our_chip, channel);

	mutex_unlock(&our_chip->mutex);
}


static const struct pwm_ops ingenic_pwm_ops = {
	.request = ingenic_pwm_request,
	.free = ingenic_pwm_free,
	.config = ingenic_pwm_config,
	.enable = ingenic_pwm_enable,
	.disable = ingenic_pwm_disable,
	.owner = THIS_MODULE,
};

static void pwm_ingenic_parse_dt(struct ingenic_pwm_chip *chip)
{
	struct device_node *np = chip->chip.dev->of_node;
	struct property *prop;
	const __be32 *cur;
	u32 val;

	of_property_for_each_u32(np, "ingenic,pwm-outputs", prop, cur, val) {
		if (val >= INGENIC_PWM_NUM) {
			dev_err(chip->chip.dev,
				"%s: invalid channel index in samsung,pwm-outputs property\n",
								__func__);
			continue;
		}
		chip->output_mask |= BIT(val);
	}
}
static int ingenic_pwm_probe(struct platform_device *pdev)
{
	struct ingenic_pwm_chip *chip;
	struct resource *res;
	int err = 0;
	int ret = 0;

	chip = devm_kzalloc(&pdev->dev,
			sizeof(struct ingenic_pwm_chip), GFP_KERNEL);
	if (!chip) {
		pr_err("%s %d,malloc ingenic_pwm_chip error\n",
				__func__, __LINE__);
		return -ENOMEM;
	}

	chip->chip.dev = &pdev->dev;
	chip->chip.ops = &ingenic_pwm_ops;
	chip->chip.base = -1;
	chip->chip.npwm = INGENIC_PWM_NUM;

	if (IS_ENABLED(CONFIG_OF) && pdev->dev.of_node) {
		pwm_ingenic_parse_dt(chip);
		/* chip->chip.of_xlate = of_pwm_xlate_with_flags; */
	} else {
		if (!pdev->dev.platform_data) {
			dev_err(&pdev->dev, "no platform data specified\n");
			return -EINVAL;
		}
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "Cannot get IORESOURCE_MEM\n");
		return  -ENOENT;
	}

	chip->iomem = ioremap(res->start, (res->end - res->start) + 1);
	if (chip->iomem == NULL) {
		dev_err(&pdev->dev, "Cannot map IO\n");
		err = -ENXIO;
		goto err_no_iomap;
	}

	chip->clk_gate = devm_clk_get(&pdev->dev, "gate_pwm");
	if (IS_ERR(chip->clk_gate)) {
		dev_err(&pdev->dev, "get pwm clk gate failed %ld\n", PTR_ERR(chip->clk_gate));
		return PTR_ERR(chip->clk_gate);
	}

	chip->clk_pwm = devm_clk_get(&pdev->dev, "cgu_pwm");
	if(IS_ERR(chip->clk_pwm)){
		dev_err(&pdev->dev, "get pwm clk failed %ld\n", PTR_ERR(chip->clk_pwm));
		return PTR_ERR(chip->clk_pwm);
	}

	if(chip->clk_gate) {
		ret = clk_prepare_enable(chip->clk_gate);
		if(ret) {
			dev_err(&pdev->dev, "enable pwm clock gate failed!\n");
		}
	}
	if(chip->clk_pwm) {
		ret = clk_set_rate(chip->clk_pwm, DEFAULT_PWM_CLK_RATE);
		if(ret) {
			dev_err(&pdev->dev, "set pwm clock rate failed!\n");
		}
		ret = clk_prepare_enable(chip->clk_pwm);
		if(ret) {
			dev_err(&pdev->dev, "enable pwm clock failed!\n");
		}
	}

	ret = pwmchip_add(&chip->chip);
	if (ret < 0) {
		devm_kfree(&pdev->dev, chip);
		return ret;
	}

	platform_set_drvdata(pdev, chip);

	mutex_init(&chip->mutex);

	dev_info(&pdev->dev, "Probe of pwm success!\n");

	return 0;

err_no_iomap:
	iounmap(chip->iomem);

	return err;
}

static int ingenic_pwm_remove(struct platform_device *pdev)
{
	struct ingenic_pwm_chip *chip;
	chip =platform_get_drvdata(pdev);
	if (!chip)
		return -ENODEV;

	pwmchip_remove(&chip->chip);
	devm_clk_put(&pdev->dev,chip->clk_pwm);
	devm_clk_put(&pdev->dev,chip->clk_gate);
	devm_kfree(&pdev->dev, chip);

	return 0;
}

static const struct of_device_id ingenic_pwm_matches[] = {
	{ .compatible = "ingenic,x2000-pwm", .data = NULL },
	{},
};

#ifdef CONFIG_PM_SLEEP
static int ingenic_pwm_suspend(struct device *dev)
{
	struct ingenic_pwm_chip *chip = dev_get_drvdata(dev);
	unsigned int i;

	/*
	 * No one preserves these values during suspend so reset them.
	 * Otherwise driver leaves PWM unconfigured if same values are
	 * passed to pwm_config() next time.
	 */
	for (i = 0; i < INGENIC_PWM_NUM; ++i) {
		struct pwm_device *pwm = &chip->chip.pwms[i];
		struct ingenic_pwm_channel *chan = pwm_get_chip_data(pwm);

		if (!chan)
			continue;

		chan->period_ns = 0;
		chan->duty_ns = 0;
	}

	return 0;
}

static int ingenic_pwm_resume(struct device *dev)
{
	struct ingenic_pwm_chip *chip = dev_get_drvdata(dev);
	unsigned int chan;

	/*
	 * Inverter setting must be preserved across suspend/resume
	 * as nobody really seems to configure it more than once.
	 */
	for (chan = 0; chan < INGENIC_PWM_NUM; ++chan) {
		if (chip->output_mask & BIT(chan)) {
		/*TODO: ??*/
	}
	}

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(ingenic_pwm_pm_ops, ingenic_pwm_suspend, ingenic_pwm_resume);

static struct platform_driver ingenic_pwm_driver = {
	.driver = {
		.name = "ingenic-pwm",
		.pm = &ingenic_pwm_pm_ops,
		.owner = THIS_MODULE,
		.of_match_table = of_match_ptr(ingenic_pwm_matches),
	},
	.probe = ingenic_pwm_probe,
	.remove = ingenic_pwm_remove,
};
module_platform_driver(ingenic_pwm_driver);

MODULE_DESCRIPTION("Ingenic SoC PWM driver");
MODULE_ALIAS("platform:ingenic-pwm");
MODULE_LICENSE("GPL");
