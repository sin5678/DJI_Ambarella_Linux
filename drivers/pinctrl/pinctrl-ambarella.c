/*
 * drivers/pinctrl/ambarella/pinctrl-amb.c
 *
 * History:
 *	2013/12/18 - [Cao Rongrong] created file
 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
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
#include <linux/platform_device.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/syscore_ops.h>
#include <linux/pinctrl/pinctrl.h>
#include <linux/pinctrl/pinmux.h>
#include <linux/pinctrl/pinconf.h>
#include <plat/rct.h>
#include <plat/pinctrl.h>
#include "core.h"

#define PIN_NAME_LENGTH		8
#define SUFFIX_LENGTH		4

#define MUXIDS_TO_PINID(m)	((m) & 0xfff)
#define MUXIDS_TO_ALT(m)	(((m) >> 12) & 0xf)

#define CFG_PULL_PRESENT	(1 << 1)
#define CFG_PULL_SHIFT		0
#define CONFIG_TO_PULL(c)	((c) >> CFG_PULL_SHIFT & 0x1)

struct ambpin_group {
	const char		*name;
	unsigned int		*pins;
	unsigned		num_pins;
	u8			*alt;
	u32			config;
};

struct ambpin_function {
	const char		*name;
	const char		**groups;
	unsigned		num_groups;
	unsigned long		function;
};

struct amb_pinctrl_soc_data {
	struct device			*dev;
	struct pinctrl_dev		*pctl;
	void __iomem			*regbase[GPIO_INSTANCES];
	void __iomem			*iomux_base;
	unsigned long			used[BITS_TO_LONGS(AMBGPIO_SIZE)];

	struct ambpin_function		*functions;
	unsigned int			nr_functions;
	struct ambpin_group		*groups;
	unsigned int			nr_groups;
};

void __iomem *amb_iomux_base = NULL;

/* check if the selector is a valid pin group selector */
static int amb_get_group_count(struct pinctrl_dev *pctldev)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	return soc->nr_groups;
}

/* return the name of the group selected by the group selector */
static const char *amb_get_group_name(struct pinctrl_dev *pctldev,
						unsigned selector)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	return soc->groups[selector].name;
}

/* return the pin numbers associated with the specified group */
static int amb_get_group_pins(struct pinctrl_dev *pctldev,
		unsigned selector, const unsigned **pins, unsigned *num_pins)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	*pins = soc->groups[selector].pins;
	*num_pins = soc->groups[selector].num_pins;
	return 0;
}

static void amb_pin_dbg_show(struct pinctrl_dev *pctldev,
			struct seq_file *s, unsigned offset)
{
	seq_printf(s, " %s", dev_name(pctldev->dev));
}

static int amb_dt_node_to_map(struct pinctrl_dev *pctldev,
			struct device_node *np,
			struct pinctrl_map **map, unsigned *num_maps)
{
	struct pinctrl_map *new_map;
	char *grp_name = NULL;
	unsigned new_num = 1;
	unsigned long config = 0;
	unsigned long *pconfig;
	int length = strlen(np->name) + SUFFIX_LENGTH;
	bool purecfg = false;
	u32 val, reg;
	int ret, i = 0;

	/* Check for pin config node which has no 'reg' property */
	if (of_property_read_u32(np, "reg", &reg))
		purecfg = true;

	ret = of_property_read_u32(np, "amb,pull-up", &val);
	if (!ret)
		config |= val << CFG_PULL_SHIFT | CFG_PULL_PRESENT;

	/* Check for group node which has both mux and config settings */
	if (!purecfg && config)
		new_num = 2;

	new_map = kzalloc(sizeof(struct pinctrl_map) * new_num, GFP_KERNEL);
	if (!new_map)
		return -ENOMEM;

	if (!purecfg) {
		new_map[i].type = PIN_MAP_TYPE_MUX_GROUP;
		new_map[i].data.mux.function = np->name;

		/* Compose group name */
		grp_name = kzalloc(length, GFP_KERNEL);
		if (!grp_name) {
			ret = -ENOMEM;
			goto free;
		}
		snprintf(grp_name, length, "%s.%d", np->name, reg);
		new_map[i].data.mux.group = grp_name;
		i++;
	}

	if (config) {
		pconfig = kmemdup(&config, sizeof(config), GFP_KERNEL);
		if (!pconfig) {
			ret = -ENOMEM;
			goto free_group;
		}

		new_map[i].type = PIN_MAP_TYPE_CONFIGS_GROUP;
		new_map[i].data.configs.group_or_pin =
					purecfg ? np->name : grp_name;
		new_map[i].data.configs.configs = pconfig;
		new_map[i].data.configs.num_configs = 1;
	}

	*map = new_map;
	*num_maps = new_num;

	return 0;

free_group:
	if (!purecfg)
		kfree(grp_name);
free:
	kfree(new_map);
	return ret;
}

static void amb_dt_free_map(struct pinctrl_dev *pctldev,
			struct pinctrl_map *map, unsigned num_maps)
{
	u32 i;

	for (i = 0; i < num_maps; i++) {
		if (map[i].type == PIN_MAP_TYPE_MUX_GROUP)
			kfree(map[i].data.mux.group);
		if (map[i].type == PIN_MAP_TYPE_CONFIGS_GROUP)
			kfree(map[i].data.configs.configs);
	}

	kfree(map);
}

/* list of pinctrl callbacks for the pinctrl core */
static const struct pinctrl_ops amb_pctrl_ops = {
	.get_groups_count	= amb_get_group_count,
	.get_group_name		= amb_get_group_name,
	.get_group_pins		= amb_get_group_pins,
	.pin_dbg_show		= amb_pin_dbg_show,
	.dt_node_to_map		= amb_dt_node_to_map,
	.dt_free_map		= amb_dt_free_map,
};

/* check if the selector is a valid pin function selector */
static int amb_pinmux_request(struct pinctrl_dev *pctldev, unsigned pin)
{
	struct amb_pinctrl_soc_data *soc;
	int rval = 0;

	soc = pinctrl_dev_get_drvdata(pctldev);

	if (test_and_set_bit(pin, soc->used))
		rval = -EBUSY;

	return rval;
}

/* check if the selector is a valid pin function selector */
static int amb_pinmux_free(struct pinctrl_dev *pctldev, unsigned pin)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	clear_bit(pin, soc->used);

	return 0;
}

/* check if the selector is a valid pin function selector */
static int amb_pinmux_get_fcount(struct pinctrl_dev *pctldev)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	return soc->nr_functions;
}

/* return the name of the pin function specified */
static const char *amb_pinmux_get_fname(struct pinctrl_dev *pctldev,
			unsigned selector)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	return soc->functions[selector].name;
}

/* return the groups associated for the specified function selector */
static int amb_pinmux_get_groups(struct pinctrl_dev *pctldev,
			unsigned selector, const char * const **groups,
			unsigned * const num_groups)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	*groups = soc->functions[selector].groups;
	*num_groups = soc->functions[selector].num_groups;
	return 0;
}

static void amb_pinmux_set_altfunc(struct amb_pinctrl_soc_data *soc,
			u32 bank, u32 offset, enum amb_pin_altfunc altfunc)
{
	void __iomem *regbase = soc->regbase[bank];
	void __iomem *iomux_reg;
	u32 i, data;

	if (altfunc == AMB_ALTFUNC_GPIO) {
		amba_clrbitsl(regbase + GPIO_AFSEL_OFFSET, 0x1 << offset);
		amba_clrbitsl(regbase + GPIO_DIR_OFFSET, 0x1 << offset);
	} else {
		amba_setbitsl(regbase + GPIO_AFSEL_OFFSET, 0x1 << offset);
		amba_clrbitsl(regbase + GPIO_MASK_OFFSET, 0x1 << offset);
	}

	if (soc->iomux_base) {
		for (i = 0; i < 3; i++) {
			iomux_reg = soc->iomux_base + IOMUX_REG_OFFSET(bank, i);
			data = amba_readl(iomux_reg);
			data &= (~(0x1 << offset));
			data |= (((altfunc >> i) & 0x1) << offset);
			amba_writel(iomux_reg, data);
		}
		iomux_reg = soc->iomux_base + IOMUX_CTRL_SET_OFFSET;
		amba_writel(iomux_reg, 0x1);
		amba_writel(iomux_reg, 0x0);
	}
}

/* enable a specified pinmux by writing to registers */
static int amb_pinmux_enable(struct pinctrl_dev *pctldev,
			unsigned selector, unsigned group)
{
	struct amb_pinctrl_soc_data *soc;
	const struct ambpin_group *grp;
	u32 i, bank, offset;

	soc = pinctrl_dev_get_drvdata(pctldev);
	grp = &soc->groups[group];

	for (i = 0; i < grp->num_pins; i++) {
		bank = PINID_TO_BANK(grp->pins[i]);
		offset = PINID_TO_OFFSET(grp->pins[i]);
		amb_pinmux_set_altfunc(soc, bank, offset,  grp->alt[i]);
	}

	return 0;
}

/* disable a specified pinmux by writing to registers */
static void amb_pinmux_disable(struct pinctrl_dev *pctldev,
			unsigned selector, unsigned group)
{
	struct amb_pinctrl_soc_data *soc;
	const struct ambpin_group *grp;

	soc = pinctrl_dev_get_drvdata(pctldev);
	grp = &soc->groups[group];

	/* FIXME: poke out the mux, set the pin to some default state? */
	dev_dbg(soc->dev,
		"disable group %s, %u pins\n", grp->name, grp->num_pins);
}

static int amb_pinmux_gpio_request_enable(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range, unsigned pin)
{
	struct amb_pinctrl_soc_data *soc;
	u32 bank, offset;

	soc = pinctrl_dev_get_drvdata(pctldev);

	if (!range || !range->gc) {
		dev_err(soc->dev, "invalid range: %p\n", range);
		return -EINVAL;
	}

	if (test_and_set_bit(pin, soc->used))
		return -EBUSY;

	bank = PINID_TO_BANK(pin);
	offset = PINID_TO_OFFSET(pin);
	amb_pinmux_set_altfunc(soc, bank, offset, AMB_ALTFUNC_GPIO);

	return 0;
}

static void amb_pinmux_gpio_disable_free(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin)
{
	struct amb_pinctrl_soc_data *soc;

	soc = pinctrl_dev_get_drvdata(pctldev);
	dev_dbg(soc->dev, "disable pin %u as GPIO\n", pin);
	/* Set the pin to some default state, GPIO is usually default */

	clear_bit(pin, soc->used);
}

/*
 * The calls to gpio_direction_output() and gpio_direction_input()
 * leads to this function call (via the pinctrl_gpio_direction_{input|output}()
 * function called from the gpiolib interface).
 */
static int amb_pinmux_gpio_set_direction(struct pinctrl_dev *pctldev,
			struct pinctrl_gpio_range *range,
			unsigned pin, bool input)
{
	struct amb_pinctrl_soc_data *soc;
	void __iomem *regbase;
	u32 bank, offset, mask;

	soc = pinctrl_dev_get_drvdata(pctldev);

	bank = PINID_TO_BANK(pin);
	offset = PINID_TO_OFFSET(pin);
	regbase = soc->regbase[bank];
	mask = (0x1 << offset);

	amba_clrbitsl(regbase + GPIO_AFSEL_OFFSET, mask);
	if (input)
		amba_clrbitsl(regbase + GPIO_DIR_OFFSET, mask);
	else
		amba_setbitsl(regbase + GPIO_DIR_OFFSET, mask);

	return 0;
}

/* list of pinmux callbacks for the pinmux vertical in pinctrl core */
static const struct pinmux_ops amb_pinmux_ops = {
	.request		= amb_pinmux_request,
	.free			= amb_pinmux_free,
	.get_functions_count	= amb_pinmux_get_fcount,
	.get_function_name	= amb_pinmux_get_fname,
	.get_function_groups	= amb_pinmux_get_groups,
	.enable			= amb_pinmux_enable,
	.disable		= amb_pinmux_disable,
	.gpio_request_enable	= amb_pinmux_gpio_request_enable,
	.gpio_disable_free	= amb_pinmux_gpio_disable_free,
	.gpio_set_direction	= amb_pinmux_gpio_set_direction,
};

/* set the pin config settings for a specified pin */
static int amb_pinconf_set(struct pinctrl_dev *pctldev, unsigned int pin,
			unsigned long config)
{
	return 0;
}

/* get the pin config settings for a specified pin */
static int amb_pinconf_get(struct pinctrl_dev *pctldev,
			unsigned int pin, unsigned long *config)
{
	return 0;
}

/* set the pin config settings for a specified pin group */
static int amb_pinconf_group_set(struct pinctrl_dev *pctldev,
			unsigned group, unsigned long config)
{
	struct amb_pinctrl_soc_data *soc;
	const unsigned int *pins;
	unsigned int cnt;

	soc = pinctrl_dev_get_drvdata(pctldev);
	pins = soc->groups[group].pins;

	for (cnt = 0; cnt < soc->groups[group].num_pins; cnt++)
		amb_pinconf_set(pctldev, pins[cnt], config);

	return 0;
}

/* get the pin config settings for a specified pin group */
static int amb_pinconf_group_get(struct pinctrl_dev *pctldev,
			unsigned int group, unsigned long *config)
{
	struct amb_pinctrl_soc_data *soc;
	const unsigned int *pins;

	soc = pinctrl_dev_get_drvdata(pctldev);
	pins = soc->groups[group].pins;
	amb_pinconf_get(pctldev, pins[0], config);
	return 0;
}

/* list of pinconfig callbacks for pinconfig vertical in the pinctrl code */
static const struct pinconf_ops amb_pinconf_ops = {
	.pin_config_get		= amb_pinconf_get,
	.pin_config_set		= amb_pinconf_set,
	.pin_config_group_get	= amb_pinconf_group_get,
	.pin_config_group_set	= amb_pinconf_group_set,
};

static struct pinctrl_desc amb_pinctrl_desc = {
	.pctlops = &amb_pctrl_ops,
	.pmxops = &amb_pinmux_ops,
	.confops = &amb_pinconf_ops,
	.owner = THIS_MODULE,
};

static int amb_pinctrl_parse_group(struct amb_pinctrl_soc_data *soc,
			struct device_node *np, int idx, const char **out_name)
{
	struct ambpin_group *grp = &soc->groups[idx];
	struct property *prop;
	const char *prop_name = "amb,pinmux-ids";
	char *grp_name;
	int length = strlen(np->name) + SUFFIX_LENGTH;
	u32 val, i;

	grp_name = devm_kzalloc(soc->dev, length, GFP_KERNEL);
	if (!grp_name)
		return -ENOMEM;

	if (of_property_read_u32(np, "reg", &val))
		snprintf(grp_name, length, "%s", np->name);
	else
		snprintf(grp_name, length, "%s.%d", np->name, val);

	grp->name = grp_name;

	prop = of_find_property(np, prop_name, &length);
	if (!prop)
		return -EINVAL;
	grp->num_pins = length / sizeof(u32);

	grp->pins = devm_kzalloc(soc->dev,
				grp->num_pins * sizeof(u32), GFP_KERNEL);
	if (!grp->pins)
		return -ENOMEM;

	grp->alt = devm_kzalloc(soc->dev,
				grp->num_pins * sizeof(u8), GFP_KERNEL);
	if (!grp->alt)
		return -ENOMEM;

	of_property_read_u32_array(np, prop_name, grp->pins, grp->num_pins);

	for (i = 0; i < grp->num_pins; i++) {
		grp->alt[i] = MUXIDS_TO_ALT(grp->pins[i]);
		grp->pins[i] = MUXIDS_TO_PINID(grp->pins[i]);
	}

	if (out_name)
		*out_name = grp->name;

	return 0;
}

static int amb_pinctrl_parse_dt(struct amb_pinctrl_soc_data *soc)
{
	struct device_node *np = soc->dev->of_node;
	struct device_node *child;
	struct ambpin_function *f;
	const char *gpio_compat = "ambarella,gpio";
	const char *fn;
	int i = 0, idxf = 0, idxg = 0;
	int ret;
	u32 val;

	child = of_get_next_child(np, NULL);
	if (!child) {
		dev_err(soc->dev, "no group is defined\n");
		return -ENOENT;
	}

	/* Count total functions and groups */
	fn = "";
	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
			continue;
		soc->nr_groups++;
		/* Skip pure pinconf node */
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			fn = child->name;
			soc->nr_functions++;
		}
	}

	soc->functions = devm_kzalloc(soc->dev, soc->nr_functions *
				sizeof(struct ambpin_function), GFP_KERNEL);
	if (!soc->functions)
		return -ENOMEM;

	soc->groups = devm_kzalloc(soc->dev, soc->nr_groups *
				sizeof(struct ambpin_group), GFP_KERNEL);
	if (!soc->groups)
		return -ENOMEM;

	/* Count groups for each function */
	fn = "";
	f = &soc->functions[idxf];
	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
			continue;
		if (of_property_read_u32(child, "reg", &val))
			continue;
		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->name = fn = child->name;
		}
		f->num_groups++;
	};

	/* Get groups for each function */
	fn = "";
	idxf = 0;
	for_each_child_of_node(np, child) {
		if (of_device_is_compatible(child, gpio_compat))
			continue;
		if (of_property_read_u32(child, "reg", &val)) {
			ret = amb_pinctrl_parse_group(soc, child, idxg++, NULL);
			if (ret)
				return ret;
			continue;
		}

		if (strcmp(fn, child->name)) {
			f = &soc->functions[idxf++];
			f->groups = devm_kzalloc(soc->dev,
					f->num_groups * sizeof(*f->groups),
					GFP_KERNEL);
			if (!f->groups)
				return -ENOMEM;
			fn = child->name;
			i = 0;
		}

		ret = amb_pinctrl_parse_group(soc, child,
					idxg++, &f->groups[i++]);
		if (ret)
			return ret;
	}

	return 0;
}

/* register the pinctrl interface with the pinctrl subsystem */
static int amb_pinctrl_register(struct amb_pinctrl_soc_data *soc)
{
	struct pinctrl_pin_desc *pindesc;
	char *pin_names;
	int pin, rval;

	/* dynamically populate the pin number and pin name for pindesc */
	pindesc = devm_kzalloc(soc->dev,
			sizeof(*pindesc) * AMBGPIO_SIZE, GFP_KERNEL);
	if (!pindesc) {
		dev_err(soc->dev, "No memory for pin desc\n");
		return -ENOMEM;
	}

	pin_names = devm_kzalloc(soc->dev,
			PIN_NAME_LENGTH * AMBGPIO_SIZE, GFP_KERNEL);
	if (!pin_names) {
		dev_err(soc->dev, "No memory for pin names\n");
		return -ENOMEM;
	}

	for (pin = 0; pin < AMBGPIO_SIZE; pin++) {
		pindesc[pin].number = pin;
		sprintf(pin_names, "io%d", pin);
		pindesc[pin].name = pin_names;
		pin_names += PIN_NAME_LENGTH;
	}

	amb_pinctrl_desc.name = dev_name(soc->dev);
	amb_pinctrl_desc.pins = pindesc;
	amb_pinctrl_desc.npins = AMBGPIO_SIZE;

	rval = amb_pinctrl_parse_dt(soc);
	if (rval)
		return rval;

	soc->pctl = pinctrl_register(&amb_pinctrl_desc, soc->dev, soc);
	if (!soc->pctl) {
		dev_err(soc->dev, "could not register pinctrl driver\n");
		return -EINVAL;
	}

	return 0;
}

static int amb_pinctrl_probe(struct platform_device *pdev)
{
	struct amb_pinctrl_soc_data *soc;
	struct resource *res;
	int i, rval;

	soc = devm_kzalloc(&pdev->dev, sizeof(*soc), GFP_KERNEL);
	if (!soc) {
		dev_err(&pdev->dev, "failed to allocate memory for private data\n");
		return -ENOMEM;
	}
	soc->dev = &pdev->dev;

	for (i = 0; i < GPIO_INSTANCES; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i);
		if (res == NULL) {
			dev_err(&pdev->dev, "no mem resource for gpio[%d]!\n", i);
			return -ENXIO;
		}

		soc->regbase[i] = devm_ioremap(&pdev->dev,
						res->start, resource_size(res));
		if (soc->regbase[i] == 0) {
			dev_err(&pdev->dev, "devm_ioremap() failed\n");
			return -ENOMEM;
		}
	}

	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "iomux");
	if (res != NULL) {
		soc->iomux_base = devm_ioremap(&pdev->dev,
						res->start, resource_size(res));
		if (soc->iomux_base == 0) {
			dev_err(&pdev->dev, "devm_ioremap() failed\n");
			return -ENOMEM;
		}

		amb_iomux_base = soc->iomux_base;
	}

	rval = amb_pinctrl_register(soc);
	if (rval)
		return rval;

	platform_set_drvdata(pdev, soc);
	dev_info(&pdev->dev, "Ambarella pinctrl driver registered\n");

	return 0;
}

#if defined(CONFIG_PM)

static u32 amb_iomux_pm_reg[GPIO_INSTANCES][3];
#if (GPIO_PAD_PULL_CTRL_SUPPORT > 0)
static u32 amb_pull_pm_reg[2][GPIO_INSTANCES];
#endif
#if (GPIO_PAD_DS_SUPPORT > 0)
static u32 amb_ds_pm_reg[GPIO_INSTANCES][2];
#endif

static int amb_pinctrl_suspend(void)
{
	u32 i, j, reg;

#if (GPIO_PAD_PULL_CTRL_SUPPORT > 0)
	for (i = 0; i < GPIO_INSTANCES; i++) {
		reg = GPIO_PAD_PULL_REG(GPIO_PAD_PULL_EN_0_OFFSET + i * 4);
		amb_pull_pm_reg[0][i] = amba_readl(reg);
		reg = GPIO_PAD_PULL_REG(GPIO_PAD_PULL_DIR_0_OFFSET + i * 4);
		amb_pull_pm_reg[1][i] = amba_readl(reg);
	}
#endif

#if (GPIO_PAD_DS_SUPPORT > 0)
	for (i = 0; i < GPIO_INSTANCES; i++) {
		for (j = 0; j < 2; j++) {
			reg = RCT_REG(GPIO_DS_OFFSET(i, j));
			amb_ds_pm_reg[i][j] = amba_readl(reg);
		}
	}
#endif

	if (amb_iomux_base == NULL)
		return 0;

	for (i = 0; i < GPIO_INSTANCES; i++) {
		for (j = 0; j < 3; j++) {
			reg = (u32)amb_iomux_base + IOMUX_REG_OFFSET(i, j);
			amb_iomux_pm_reg[i][j] = amba_readl(reg);
		}
	}

	return 0;
}

static void amb_pinctrl_resume(void)
{
	/* Do not resume IOMUX, GPIO DS, and GPIO PULL  registers. */
	/* All these registers should be handled at RTOS side. */
#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK

	u32 i, j, reg;

#if (GPIO_PAD_PULL_CTRL_SUPPORT > 0)
	for (i = 0; i < GPIO_INSTANCES; i++) {
		reg = GPIO_PAD_PULL_REG(GPIO_PAD_PULL_EN_0_OFFSET + i * 4);
		amba_writel(reg, amb_pull_pm_reg[0][i]);
		reg = GPIO_PAD_PULL_REG(GPIO_PAD_PULL_DIR_0_OFFSET + i * 4);
		amba_writel(reg, amb_pull_pm_reg[1][i]);
	}
#endif

#if (GPIO_PAD_DS_SUPPORT > 0)
	for (i = 0; i < GPIO_INSTANCES; i++) {
		for (j = 0; j < 2; j++) {
			reg = RCT_REG(GPIO_DS_OFFSET(i, j));
			amba_writel(reg, amb_ds_pm_reg[i][j]);
		}
	}
#endif

	if (amb_iomux_base == NULL)
		return;

	for (i = 0; i < GPIO_INSTANCES; i++) {
		for (j = 0; j < 3; j++) {
			reg = (u32)amb_iomux_base + IOMUX_REG_OFFSET(i, j);
			amba_writel(reg, amb_iomux_pm_reg[i][j]);
		}
	}

	amba_writel(amb_iomux_base + IOMUX_CTRL_SET_OFFSET, 0x1);
	amba_writel(amb_iomux_base + IOMUX_CTRL_SET_OFFSET, 0x0);
#endif
}

static struct syscore_ops amb_pinctrl_syscore_ops = {
	.suspend	= amb_pinctrl_suspend,
	.resume		= amb_pinctrl_resume,
};

#endif /* CONFIG_PM */

static const struct of_device_id amb_pinctrl_dt_match[] = {
	{ .compatible = "ambarella,pinctrl" },
	{},
};
MODULE_DEVICE_TABLE(of, amb_pinctrl_dt_match);

static struct platform_driver amb_pinctrl_driver = {
	.probe	= amb_pinctrl_probe,
	.driver	= {
		.name	= "ambarella-pinctrl",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(amb_pinctrl_dt_match),
	},
};

static int __init amb_pinctrl_drv_register(void)
{
#ifdef CONFIG_PM
	register_syscore_ops(&amb_pinctrl_syscore_ops);
#endif
	return platform_driver_register(&amb_pinctrl_driver);
}
postcore_initcall(amb_pinctrl_drv_register);

MODULE_AUTHOR("Cao Rongrong <rrcao@ambarella.com>");
MODULE_DESCRIPTION("Ambarella SoC pinctrl driver");
MODULE_LICENSE("GPL v2");


