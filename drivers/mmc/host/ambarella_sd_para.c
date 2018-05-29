/*
 * drivers/mmc/host/ambarella_sd_para.c
 *
 * Copyright (C) 2015, Ambarella, Inc.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/irq.h>
#include <asm/dma.h>
#include <plat/rct.h>
#include <mach/hardware.h>
#include <plat/sd.h>
#include <plat/event.h>

#ifdef CONFIG_PLAT_AMBARELLA_S2L
#define SDIO_DS0 GPIO_DS0_2_REG
#define SDIO_DS1 GPIO_DS1_2_REG
//clock [17], GPIO 81
#define DS_CLK_MASK 0x00020000
//data [22:19], GPIO 83~86
#define DS_DATA_MASK 0x00780000
//cmd [18], GPIO 82
#define DS_CMD_MASK 0x00040000
//cdwp [24:23], GPIO 87~88
#define DS_CDWP_MASK 0x01800000

#elif defined(CONFIG_PLAT_AMBARELLA_S3L)
#define SDIO_DS0 GPIO_DS0_2_REG
#define SDIO_DS1 GPIO_DS1_2_REG
#define SDIO_DELAY_SEL0 (SD1_BASE + 0xd8)
#define SDIO_DELAY_SEL2 (SD1_BASE + 0xdc)
//[29:27] D3 output delay control
#define SDIO_SEL0_OD3LY 27
//[23:21] D2 output delay control
#define SDIO_SEL0_OD2LY 21
//[17:15] D1 output delay control
#define SDIO_SEL0_OD1LY 15
//[11:9]  D0 output delay control
#define SDIO_SEL0_OD0LY 9
//[26:24] D3 input delay control
#define SDIO_SEL0_ID3LY 24
//[20:18] D2 input delay control
#define SDIO_SEL0_ID2LY 18
//[14:12] D1 input delay control
#define SDIO_SEL0_ID1LY 12
//[8:6]   D0 input delay control
#define SDIO_SEL0_ID0LY 6
//[5:3]   CMD output delay control
#define SDIO_SEL0_OCMDLY 3
//[2:0]   CMD input delay control
#define SDIO_SEL0_ICMDLY 0
//[24:22] clk_sdcard output delay control
#define SDIO_SEL2_OCLY 22
//clock [18], GPIO 82
#define DS_CLK_MASK 0x00040000
//data [23:20], GPIO 84~87
#define DS_DATA_MASK 0x00F00000
//cmd [19], GPIO 83
#define DS_CMD_MASK 0x00080000
//cdwp [25:24], GPIO 88~89
#define DS_CDWP_MASK 0x03000000

#else /*A9, A9S*/
#define SDIO_DS0 RCT_REG(0x278)
#define SDIO_DS1 RCT_REG(0x28c)
//[31:29] SDIO output data delay control
#define SDIO_ODLY 29
//[23:21] SDIO output clock delay control
#define SDIO_OCLY 21
//[15:13] SDIO input data delay control
#define SDIO_IDLY 13
//[7:5] SDIO clock input delay control
#define SDID_ICLY 5
//clock [5], GPIO 69
#define DS_CLK_MASK 0x00000020
//data [10:7], GPIO 71~74
#define DS_DATA_MASK 0x00000780
//cmd [6], GPIO 70
#define DS_CMD_MASK 0x00000040
//cdwp [12:11], GPIO 75~76
#define DS_CDWP_MASK 0x00001800
#endif

static int ambarella_sdio_ds_value_2ma(u32 value)
{
	enum amba_sdio_ds_value {
		DS_3MA = 0,
		DS_12MA,
		DS_6MA,
		DS_18MA
	};

	switch (value) {
	case DS_3MA:
		return 3;
	case DS_12MA:
		return 12;
	case DS_6MA:
		return 6;
	case DS_18MA:
		return 18;
	default:
		printk(KERN_ERR "%s: unknown driving strength\n", __func__);
		return 0;
	}
}

static int sdio_info_proc_read(struct seq_file *m, void *v)
{
	int len = 0;
	u32 reg, ds1, ds_value, b0, b1;

	reg = amba_readl(SD2_REG(SD_HOST_OFFSET));
	pr_debug("amba_readl 0x%x = 0x%x\n", SD2_REG(SD_HOST_OFFSET), reg);
	if (reg & SD_HOST_HIGH_SPEED)
		len += seq_printf(m, "SDIO high speed mode:            yes\n");
	else
		len += seq_printf(m, "SDIO high speed mode:            no\n");

#ifdef CONFIG_PLAT_AMBARELLA_S2L
	len += seq_printf(m, "SDIO output data delay:          0\n");
	len += seq_printf(m, "SDIO output clock delay:         0\n");
	len += seq_printf(m, "SDIO input data delay:           0\n");
	len += seq_printf(m, "SDIO input clock delay:          0\n");
#elif defined(CONFIG_PLAT_AMBARELLA_S3L)
	reg = amba_readl(SDIO_DELAY_SEL0);
	ds1 = amba_readl(SDIO_DELAY_SEL2);
	pr_debug("amba_rct_readl 0x%x = 0x%x\n", MS_DELAY_CTRL_REG, reg);
	//SDIO output data delay control, assume all bits have same value
	len += seq_printf(m, "SDIO output data delay:          %u\n",
		(reg & (0x7 << SDIO_SEL0_OD0LY)) >> SDIO_SEL0_OD0LY);
	//SDIO output clock delay control
	len += seq_printf(m, "SDIO output clock delay:         %u\n",
		(ds1 & (0x7 << SDIO_SEL2_OCLY)) >> SDIO_SEL2_OCLY);
	//SDIO input data delay control, assume all bits have same value
	len += seq_printf(m, "SDIO input data delay:           %u\n",
		(reg & (0x7 << SDIO_SEL0_ID0LY)) >> SDIO_SEL0_ID0LY);
	//SDIO clock input delay control, not available
	len += seq_printf(m, "SDIO input clock delay:          0\n");
#else
	reg = amba_rct_readl(MS_DELAY_CTRL_REG);
	pr_debug("amba_rct_readl 0x%x = 0x%x\n", MS_DELAY_CTRL_REG, reg);
	//SDIO output data delay control
	len += seq_printf(m, "SDIO output data delay:          %u\n",
		(reg & (0x7 << SDIO_ODLY)) >> SDIO_ODLY);
	//SDIO output clock delay control
	len += seq_printf(m, "SDIO output clock delay:         %u\n",
		(reg & (0x7 << SDIO_OCLY)) >> SDIO_OCLY);
	//SDIO input data delay control
	len += seq_printf(m, "SDIO input data delay:           %u\n",
		(reg & (0x7 << SDIO_IDLY)) >> SDIO_IDLY);
	//SDIO clock input delay control
	len += seq_printf(m, "SDIO input clock delay:          %u\n",
		(reg & (0x7 << SDID_ICLY)) >> SDID_ICLY);
#endif
	reg = amba_rct_readl(SDIO_DS0);
	pr_debug("b0 amba_rct_readl 0x%x = 0x%x\n", SDIO_DS0, reg);
	ds1 = amba_rct_readl(SDIO_DS1);
	pr_debug("b1 amba_rct_readl 0x%x = 0x%x\n", SDIO_DS1, ds1);

	// SDIO data, assume all bits have same value
	b0 = (reg & DS_DATA_MASK) ? 1 : 0;
	b1 = (ds1 & DS_DATA_MASK) ? 1 : 0;
	ds_value = b0 + (b1 << 0x1);
	len += seq_printf(m, "SDIO data driving strength:      %u mA(%d)\n",
		ambarella_sdio_ds_value_2ma(ds_value), ds_value);
	// SDIO clock
	b0 = (reg & DS_CLK_MASK) ? 1 : 0;
	b1 = (ds1 & DS_CLK_MASK) ? 1 : 0;
	ds_value = b0 + (b1 << 0x1);
	len += seq_printf(m, "SDIO clock driving strength:     %u mA(%d)\n",
		ambarella_sdio_ds_value_2ma(ds_value), ds_value);
	// SDIO cmd
	b0 = (reg & DS_CMD_MASK) ? 1 : 0;
	b1 = (ds1 & DS_CMD_MASK) ? 1 : 0;
	ds_value = b0 + (b1 << 0x1);
	len += seq_printf(m, "SDIO command driving strength:   %u mA(%d)\n",
		ambarella_sdio_ds_value_2ma(ds_value), ds_value);
	// SDIO cdwp, assume both bits have same value
	b0 = (reg & DS_CDWP_MASK) ? 1 : 0;
	b1 = (ds1 & DS_CDWP_MASK) ? 1 : 0;
	ds_value = b0 + (b1 << 0x1);
	len += seq_printf(m, "SDIO CD and WP driving strength: %u mA(%d)\n",
		ambarella_sdio_ds_value_2ma(ds_value), ds_value);

	return len;
}

static int sdio_info_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, sdio_info_proc_read, NULL);
}

const struct file_operations proc_fops_sdio_info = {
	.open = sdio_info_proc_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};

/*
 * return 0 if timing changed
 */
int amba_sdio_delay_post_apply(const int odly, const int ocly,
	const int idly, const int icly)
{
#ifdef CONFIG_PLAT_AMBARELLA_S2L /*A12 cannot control SDIO delay*/
	printk(KERN_ERR "err: not supported!\n");
	return 1;	//not changed
#elif defined(CONFIG_PLAT_AMBARELLA_S3L)
	u32 reg_ori, reg_new;
	int i, retval;
	int biti;

	reg_ori = amba_readl(SDIO_DELAY_SEL0);
	reg_new = reg_ori;
	pr_debug("readl 0x%08x = 0x%08x\n", MS_DELAY_CTRL_REG, reg_ori);

	//SDIO output data delay control
	if (-1 != odly) {
		for (i = 0; i < 3; i++) {
			biti = odly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_OD0LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_OD0LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = odly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_OD1LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_OD1LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = odly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_OD2LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_OD2LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = odly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_OD3LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_OD3LY + i);
		}
	}

	//SDIO input data delay control
	if (-1 != idly) {
		for (i = 0; i < 3; i++) {
			biti = idly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_ID0LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_ID0LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = idly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_ID1LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_ID1LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = idly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_ID2LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_ID2LY + i);
		}
		for (i = 0; i < 3; i++) {
			biti = idly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL0_ID3LY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL0_ID3LY + i);
		}
	}

	//SDIO clock input delay control, not available
	if (-1 != icly) {
		printk("%s: SDIO clock input delay control is not available\n", __func__);
	}

	if (reg_ori != reg_new) {
		amba_writel(SDIO_DELAY_SEL0, reg_new);
		pr_debug("amba_writel 0x%x 0x%x\n", MS_DELAY_CTRL_REG, reg_new);
		retval = 0;
	} else
		retval = 1;

	reg_ori = amba_readl(SDIO_DELAY_SEL2);
	reg_new = reg_ori;
	pr_debug("readl 0x%08x = 0x%08x\n", MS_DELAY_CTRL_REG, reg_ori);

	//SDIO output clock delay control
	if (-1 != ocly) {
		for (i = 0; i < 3; i++) {
			biti = ocly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_SEL2_OCLY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_SEL2_OCLY + i);
		}
	}

	if (reg_ori != reg_new) {
		amba_writel(SDIO_DELAY_SEL2, reg_new);
		pr_debug("amba_writel 0x%x 0x%x\n", MS_DELAY_CTRL_REG, reg_new);
	} else
		retval = 1;

	return retval;
#else
	u32 reg_ori, reg_new;
	int i;
	int biti;

	reg_ori = amba_rct_readl(MS_DELAY_CTRL_REG);
	reg_new = reg_ori;
	pr_debug("readl 0x%08x = 0x%08x\n", MS_DELAY_CTRL_REG, reg_ori);

	//SDIO output data delay control
	if (-1 != odly) {
		for (i = 0; i < 3; i++) {
			biti = odly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_ODLY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_ODLY + i);
		}
	}

	//SDIO output clock delay control
	if (-1 != ocly) {
		for (i = 0; i < 3; i++) {
			biti = ocly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_OCLY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_OCLY + i);
		}
	}

	//SDIO input data delay control
	if (-1 != idly) {
		for (i = 0; i < 3; i++) {
			biti = idly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDIO_IDLY + i);
			else
				reg_new = reg_new & ~BIT(SDIO_IDLY + i);
		}
	}

	//SDIO clock input delay control
	if (-1 != icly) {
		for (i = 0; i < 3; i++) {
			biti = icly >> i;
			if (biti & 1)
				reg_new = reg_new | BIT(SDID_ICLY + i);
			else
				reg_new = reg_new & ~BIT(SDID_ICLY + i);
		}
	}

	if (reg_ori != reg_new) {
		amba_rct_writel(MS_DELAY_CTRL_REG, reg_new);
		pr_debug("amba_rct_writel 0x%x 0x%x\n", MS_DELAY_CTRL_REG, reg_new);
		return 0;
	} else
		return 1;	//not changed
#endif
}

/*
 * return 0 if timing changed
 */
int amba_sdio_ds_post_apply(const int clk_ds, const int data_ds,
	const int cmd_ds, const int cdwp_ds)
{
	int biti, ret = 1;
	u32 reg_ori, reg_new;

	/* DS0 */
	reg_ori = amba_rct_readl(SDIO_DS0);
	reg_new = reg_ori;
	pr_debug("readl 0x%08x = 0x%08x\n", SDIO_DS0, reg_ori);

	if (-1 != clk_ds) {
		biti = clk_ds & 0x1;
		if (biti)
			reg_new |= DS_CLK_MASK;
		else
			reg_new &= ~DS_CLK_MASK;
	}
	if (-1 != data_ds) {
		biti = data_ds & 0x1;
		if (biti)
			reg_new |= DS_DATA_MASK;
		else
			reg_new &= ~DS_DATA_MASK;
	}
	if (-1 != cmd_ds) {
		biti = cmd_ds & 0x1;
		if (biti)
			reg_new |= DS_CMD_MASK;
		else
			reg_new &= ~DS_CMD_MASK;
	}
	if (-1 != cdwp_ds) {
		biti = cdwp_ds & 0x1;
		if (biti)
			reg_new |= DS_CDWP_MASK;
		else
			reg_new &= ~DS_CDWP_MASK;
	}

	if (reg_ori != reg_new) {
		amba_rct_writel(SDIO_DS0, reg_new);
		pr_debug("amba_rct_writel 0x%x 0x%x\n", SDIO_DS0, reg_new);
		ret = 0;
	}

	/* DS1 */
	reg_ori = amba_rct_readl(SDIO_DS1);
	reg_new = reg_ori;
	pr_debug("readl 0x%08x = 0x%08x\n", SDIO_DS1, reg_ori);

	if (-1 != clk_ds) {
		biti = (clk_ds & 0x2) >> 1;
		if (biti)
			reg_new |= DS_CLK_MASK;
		else
			reg_new &= ~DS_CLK_MASK;
	}
	if (-1 != data_ds) {
		biti = (data_ds & 0x2) >> 1;
		if (biti)
			reg_new |= DS_DATA_MASK;
		else
			reg_new &= ~DS_DATA_MASK;
	}
	if (-1 != cmd_ds) {
		biti = (cmd_ds & 0x2) >> 1;
		if (biti)
			reg_new |= DS_CMD_MASK;
		else
			reg_new &= ~DS_CMD_MASK;
	}
	if (-1 != cdwp_ds) {
		biti = (cdwp_ds & 0x2) >> 1;
		if (biti)
			reg_new |= DS_CDWP_MASK;
		else
			reg_new &= ~DS_CDWP_MASK;
	}
	if (reg_ori != reg_new) {
		amba_rct_writel(SDIO_DS1, reg_new);
		pr_debug("amba_rct_writel 0x%x 0x%x\n", SDIO_DS1, reg_new);
		ret = 0;
	}

	return ret;
}

int ambarella_set_sdio_host_high_speed(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;
	u8 hostr;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	hostr = amba_readb((unsigned char *)SD2_REG(SD_HOST_OFFSET));
	pr_debug("amba_readb 0x%x = 0x%x\n", SD2_REG(SD_HOST_OFFSET), hostr);

	if(value == 1)
		hostr |= SD_HOST_HIGH_SPEED;
	else if(value == 0)
		hostr &= ~SD_HOST_HIGH_SPEED;

	amba_writeb((unsigned char *)(SD2_REG(SD_HOST_OFFSET)), hostr);
	pr_debug("amba_writeb 0x%x 0x%x\n", SD2_REG(SD_HOST_OFFSET), hostr);
	return retval;
}

int ambarella_set_sdio_clk_ds(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_ds_post_apply(value, -1, -1, -1);

	return retval;
}

int ambarella_set_sdio_data_ds(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_ds_post_apply(-1, value, -1, -1);

	return retval;
}

int ambarella_set_sdio_cmd_ds(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_ds_post_apply(-1, -1, value, -1);

	return retval;
}

int ambarella_set_sdio_host_odly(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_delay_post_apply(value, -1, -1, -1);

	return retval;
}

int ambarella_set_sdio_host_ocly(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_delay_post_apply(-1, value, -1, -1);

	return retval;
}

int ambarella_set_sdio_host_idly(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_delay_post_apply(-1, -1, value, -1);

	return retval;
}

int ambarella_set_sdio_host_icly(const char *str, const struct kernel_param *kp)
{
	int retval;
	int value;

	param_set_int(str, kp);
	retval = kstrtos32(str, 10, &value);
	amba_sdio_delay_post_apply(-1, -1, -1, value);

	return retval;
}
