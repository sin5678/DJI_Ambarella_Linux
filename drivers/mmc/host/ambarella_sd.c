/*
 * drivers/mmc/host/ambarella_sd.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/of_gpio.h>
#include <linux/pinctrl/consumer.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/scatterlist.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include <asm/dma.h>

#include <mach/hardware.h>
#include <plat/fio.h>
#include <plat/sd.h>
#include <plat/event.h>
#include <linux/proc_fs.h>

#if defined(CONFIG_RPMSG_SD)
#include <linux/aipc/rpmsg_sd.h>

static struct rpdev_sdinfo G_rpdev_sdinfo[SD_INSTANCES];
void ambarella_sd_cd_detect(struct work_struct *work);
#endif

static struct mmc_host *G_mmc[SD_INSTANCES];

#define SD_PIN_FIXED (1)
#define SD_PIN_BYPIN (-1)

/* ==========================================================================*/
#define CONFIG_SD_AMBARELLA_TIMEOUT_VAL		(0xe)
#define CONFIG_SD_AMBARELLA_WAIT_TIMEOUT	(HZ / 100)
#define CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT	(100000)
#define CONFIG_SD_AMBARELLA_MAX_TIMEOUT		(10 * HZ)
#define CONFIG_SD_AMBARELLA_VSW_PRE_SPEC	(5)
#define CONFIG_SD_AMBARELLA_VSW_WAIT_LIMIT	(1000)

#undef CONFIG_SD_AMBARELLA_DEBUG
#undef CONFIG_SD_AMBARELLA_DEBUG_VERBOSE

#define ambsd_printk(level, phcinfo, format, arg...)	\
	printk(level "%s.%u: " format, dev_name(phcinfo->pinfo->dev), \
	phcinfo->slot_id, ## arg)

#define ambsd_err(phcinfo, format, arg...)		\
	ambsd_printk(KERN_ERR, phcinfo, format, ## arg)
#define ambsd_warn(phcinfo, format, arg...)		\
	ambsd_printk(KERN_WARNING, phcinfo, format, ## arg)
#define ambsd_info(phcinfo, format, arg...)		\
	ambsd_printk(KERN_INFO, phcinfo, format, ## arg)
#define ambsd_rtdbg(phcinfo, format, arg...)		\
	ambsd_printk(KERN_DEBUG, phcinfo, format, ## arg)

#ifdef CONFIG_SD_AMBARELLA_DEBUG
#define ambsd_dbg(phcinfo, format, arg...)		\
	ambsd_printk(KERN_DEBUG, phcinfo, format, ## arg)
#else
#define ambsd_dbg(phcinfo, format, arg...)		\
	({ if (0) ambsd_printk(KERN_DEBUG, phcinfo, format, ##arg); 0; })
#endif

/* ==========================================================================*/
enum ambarella_sd_state {
	AMBA_SD_STATE_IDLE,
	AMBA_SD_STATE_CMD,
	AMBA_SD_STATE_DATA,
	AMBA_SD_STATE_RESET,
	AMBA_SD_STATE_ERR
};

struct ambarella_sd_phy_timing {
	u32				mode;
	u32				val0;
	u32				val1;
};

struct ambarella_sd_mmc_info {
	struct mmc_host			*mmc;
	struct mmc_request		*mrq;

	wait_queue_head_t		wait;

	enum ambarella_sd_state		state;

	struct scatterlist		*sg;
	u32				sg_len;
	u32				wait_tmo;
	u16				blk_sz;
	u16				blk_cnt;
	u32				arg_reg;
	u16				xfr_reg;
	u16				cmd_reg;
	u16				sta_reg;
	u8				tmo;
	u8				use_adma;
	u32				sta_counter;

	char				*buf_vaddress;
	dma_addr_t			buf_paddress;
	u32				dma_address;
	u32				dma_size;

	void				(*pre_dma)(void *data);
	void				(*post_dma)(void *data);

	u32				slot_id;
	struct ambarella_sd_controller_info *pinfo;
	u32				valid;
	int				fixed_cd;
	int				fixed_wp;

	int				pwr_gpio;
	u8				pwr_gpio_active;
	int				v18_gpio;
	u8				v18_gpio_active;
	u32				no_1_8_v : 1,
					caps_ddr : 1,
					caps_adma : 1,
					force_gpio : 1;

	struct pinctrl			*pinctrl;
	struct pinctrl_state		*state_work;
	struct pinctrl_state		*state_idle;

	struct notifier_block		system_event;
	struct semaphore		system_event_sem;

#if defined(CONFIG_RPMSG_SD)
	struct delayed_work		detect;
	u32				insert;
#endif
};

struct ambarella_sd_controller_info {
	unsigned char __iomem 		*regbase;
	unsigned char __iomem 		*fio_reg;
	unsigned char __iomem 		*timing_reg;
	struct device			*dev;
	unsigned int			irq;
	u32				dma_fix;
	u32				reset_error;

	u32				max_blk_sz;
	struct kmem_cache		*buf_cache;

	struct clk			*clk;
	u32				default_wait_tmo;
	u32				switch_voltage_tmo;
	u8				slot_num;
	u32				soft_phy : 1;
	struct ambarella_sd_phy_timing	*phy_timing;
	u32				phy_timing_num;

	struct ambarella_sd_mmc_info	*pslotinfo[SD_INSTANCES];
	struct mmc_ios			controller_ios;

#if defined(CONFIG_RPMSG_SD)
	u32			       id;
#endif
};

#define SDIO_GLOBAL_ID 1
static int force_sdio_host_high_speed = -1;
static int sdio_clk_ds = -1;
static int sdio_data_ds = -1;
static int sdio_cmd_ds = -1;
static int sdio_host_odly = -1;
static int sdio_host_ocly = -1;
static int sdio_host_idly = -1;
static int sdio_host_icly = -1;
static int sdio_host_max_frequency = -1;

extern int amba_sdio_delay_post_apply(const int odly, const int ocly,
	const int idly, const int icly);
extern int amba_sdio_ds_post_apply(const int clk_ds, const int data_ds,
	const int cmd_ds, const int cdwp_ds);
extern int ambarella_set_sdio_host_high_speed(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_clk_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_data_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_cmd_ds(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_odly(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_ocly(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_idly(const char *val, const struct kernel_param *kp);
extern int ambarella_set_sdio_host_icly(const char *val, const struct kernel_param *kp);
extern const struct file_operations proc_fops_sdio_info;
int ambarella_set_sdio_host_max_frequency(const char *str, const struct kernel_param *kp);

static struct kernel_param_ops param_ops_sdio_host_high_speed = {
	.set = ambarella_set_sdio_host_high_speed,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_clk_ds = {
	.set = ambarella_set_sdio_clk_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_data_ds = {
	.set = ambarella_set_sdio_data_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_cmd_ds = {
	.set = ambarella_set_sdio_cmd_ds,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_odly = {
	.set = ambarella_set_sdio_host_odly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_ocly = {
	.set = ambarella_set_sdio_host_ocly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_idly = {
	.set = ambarella_set_sdio_host_idly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_icly = {
	.set = ambarella_set_sdio_host_icly,
	.get = param_get_int,
};
static struct kernel_param_ops param_ops_sdio_host_max_frequency = {
	.set = ambarella_set_sdio_host_max_frequency,
	.get = param_get_int,
};

module_param_cb(force_sdio_host_high_speed, &param_ops_sdio_host_high_speed,
	&(force_sdio_host_high_speed), 0644);
module_param_cb(sdio_clk_ds, &param_ops_sdio_clk_ds,
	&(sdio_clk_ds), 0644);
module_param_cb(sdio_data_ds, &param_ops_sdio_data_ds,
	&(sdio_data_ds), 0644);
module_param_cb(sdio_cmd_ds, &param_ops_sdio_cmd_ds,
	&(sdio_cmd_ds), 0644);
module_param_cb(sdio_host_odly, &param_ops_sdio_host_odly,
	&(sdio_host_odly), 0644);
module_param_cb(sdio_host_ocly, &param_ops_sdio_host_ocly,
	&(sdio_host_ocly), 0644);
module_param_cb(sdio_host_idly, &param_ops_sdio_host_idly,
	&(sdio_host_idly), 0644);
module_param_cb(sdio_host_icly, &param_ops_sdio_host_icly,
	&(sdio_host_icly), 0644);
module_param_cb(sdio_host_max_frequency, &param_ops_sdio_host_max_frequency,
	&(sdio_host_max_frequency), 0644);

int ambarella_set_sdio_host_max_frequency(const char *str, const struct kernel_param *kp)
{
	int retval;
	unsigned int value;
	struct mmc_host *mmc = G_mmc[SDIO_GLOBAL_ID];

	param_set_uint(str, kp);

	retval = kstrtou32(str, 10, &value);

	mmc->f_max = value;
	pr_debug("mmc->f_max = %u\n", value);

	return retval;
}
/* ==========================================================================*/
#ifdef CONFIG_SD_AMBARELLA_DEBUG_VERBOSE
static void ambarella_sd_show_info(struct ambarella_sd_mmc_info *pslotinfo)
{
	ambsd_dbg(pslotinfo, "Enter %s\n", __func__);
	ambsd_dbg(pslotinfo, "sg = 0x%x.\n", (u32)pslotinfo->sg);
	ambsd_dbg(pslotinfo, "sg_len = 0x%x.\n", pslotinfo->sg_len);
	ambsd_dbg(pslotinfo, "tmo = 0x%x.\n", pslotinfo->tmo);
	ambsd_dbg(pslotinfo, "blk_sz = 0x%x.\n", pslotinfo->blk_sz);
	ambsd_dbg(pslotinfo, "blk_cnt = 0x%x.\n", pslotinfo->blk_cnt);
	ambsd_dbg(pslotinfo, "arg_reg = 0x%x.\n", pslotinfo->arg_reg);
	ambsd_dbg(pslotinfo, "xfr_reg = 0x%x.\n", pslotinfo->xfr_reg);
	ambsd_dbg(pslotinfo, "cmd_reg = 0x%x.\n", pslotinfo->cmd_reg);
	ambsd_dbg(pslotinfo, "buf_vaddress = 0x%x.\n",
		(u32)pslotinfo->buf_vaddress);
	ambsd_dbg(pslotinfo, "buf_paddress = 0x%x.\n", pslotinfo->buf_paddress);
	ambsd_dbg(pslotinfo, "dma_address = 0x%x.\n", pslotinfo->dma_address);
	ambsd_dbg(pslotinfo, "dma_size = 0x%x.\n", pslotinfo->dma_size);
	ambsd_dbg(pslotinfo, "pre_dma = 0x%x.\n", (u32)pslotinfo->pre_dma);
	ambsd_dbg(pslotinfo, "post_dma = 0x%x.\n", (u32)pslotinfo->post_dma);
	ambsd_dbg(pslotinfo, "SD: state = 0x%x.\n", pslotinfo->state);
	ambsd_dbg(pslotinfo, "Exit %s\n", __func__);
}
#endif

#if defined(CONFIG_RPMSG_SD)
void ambarella_sd_cd_detect(struct work_struct *work)
{
	struct ambarella_sd_mmc_info *pslotinfo =
		container_of(work, struct ambarella_sd_mmc_info, detect.work);

	if (pslotinfo->insert == 0x1) {
		rpmsg_sd_detect_insert(pslotinfo->slot_id);
	} else {
		rpmsg_sd_detect_eject(pslotinfo->slot_id);
	}

}

/**
 * Get the sdinfo inited by rpmsg.
 */
struct rpdev_sdinfo *ambarella_sd_sdinfo_get(struct mmc_host *mmc)
{
	u32 slot_id;
	struct ambarella_sd_controller_info	*pinfo;
	struct ambarella_sd_mmc_info		*pslotinfo = mmc_priv(mmc);

	BUG_ON(!mmc);

	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;
	slot_id = (pinfo->id == SD0_BASE) ? SD_HOST_0 :
		  (pinfo->id == SD1_BASE) ? SD_HOST_1 : SD_HOST_2;

	return &G_rpdev_sdinfo[slot_id];
}
EXPORT_SYMBOL(ambarella_sd_sdinfo_get);

/**
 * Send SD command through rpmsg.
 */
int ambarella_sd_rpmsg_cmd_send(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct rpdev_sdresp 			resp;
	struct ambarella_sd_controller_info	*pinfo;
	struct ambarella_sd_mmc_info		*pslotinfo = mmc_priv(mmc);

	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;

	resp.opcode = cmd->opcode;
	resp.slot_id = (pinfo->id == SD0_BASE) ? SD_HOST_0 :
		       (pinfo->id == SD1_BASE) ? SD_HOST_1 : SD_HOST_2;

	if (G_rpdev_sdinfo[resp.slot_id].from_rpmsg == 0)
		return -1;

	/* send IPC */
	rpmsg_sdresp_get((void *) &resp);
	if (resp.ret != 0)
		return resp.ret;

	memcpy(cmd->resp, resp.resp, sizeof(u32) * 4);

	if (cmd->data != NULL) {
		memcpy(cmd->data->buf, resp.buf, cmd->data->blksz);
	}

	return 0;
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_cmd_send);

/**
 * Service initialization.
 */
int ambarella_sd_rpmsg_sdinfo_init(struct mmc_host *mmc)
{
	u32 slot_id;
	struct rpdev_sdinfo sdinfo;
	struct ambarella_sd_controller_info	*pinfo;
	struct ambarella_sd_mmc_info		*pslotinfo = mmc_priv(mmc);

	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;
	slot_id = (pinfo->id == SD0_BASE) ? SD_HOST_0 :
		  (pinfo->id == SD1_BASE) ? SD_HOST_1 : SD_HOST_2;

	memset(&sdinfo, 0x0, sizeof(sdinfo));
	sdinfo.slot_id = slot_id;
	rpmsg_sdinfo_get((void *) &sdinfo);

	G_rpdev_sdinfo[slot_id].is_init 	= sdinfo.is_init;
	G_rpdev_sdinfo[slot_id].is_sdmem 	= sdinfo.is_sdmem;
	G_rpdev_sdinfo[slot_id].is_mmc 		= sdinfo.is_mmc;
	G_rpdev_sdinfo[slot_id].is_sdio 	= sdinfo.is_sdio;
	G_rpdev_sdinfo[slot_id].bus_width	= sdinfo.bus_width;
	G_rpdev_sdinfo[slot_id].clk 	= sdinfo.clk;
	G_rpdev_sdinfo[slot_id].ocr 	= sdinfo.ocr;
	G_rpdev_sdinfo[slot_id].hcs 	= sdinfo.hcs;
	G_rpdev_sdinfo[slot_id].rca 	= sdinfo.rca;

	G_mmc[slot_id] = mmc;

#if 0
	printk("%s: sdinfo.slot_id   = %d\n", __func__, sdinfo.slot_id);
	printk("%s: sdinfo.is_init   = %d\n", __func__, sdinfo.is_init);
	printk("%s: sdinfo.is_sdmem  = %d\n", __func__, sdinfo.is_sdmem);
	printk("%s: sdinfo.is_mmc    = %d\n", __func__, sdinfo.is_mmc);
	printk("%s: sdinfo.is_sdio   = %d\n", __func__, sdinfo.is_sdio);
	printk("%s: sdinfo.bus_width = %d\n", __func__, sdinfo.bus_width);
	printk("%s: sdinfo.clk       = %d\n", __func__, sdinfo.clk);
#endif
	return 0;
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_sdinfo_init);

/**
 * Enable to use the rpmsg sdinfo.
 */
void ambarella_sd_rpmsg_sdinfo_en(struct mmc_host *mmc, u8 enable)
{
	u32 slot_id;
	struct ambarella_sd_controller_info	*pinfo;
	struct ambarella_sd_mmc_info		*pslotinfo = mmc_priv(mmc);

	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;
	slot_id = (pinfo->id == SD0_BASE) ? SD_HOST_0 :
		  (pinfo->id == SD1_BASE) ? SD_HOST_1 : SD_HOST_2;

	if (enable)
		G_rpdev_sdinfo[slot_id].from_rpmsg = enable;
	else
		memset(&G_rpdev_sdinfo[slot_id], 0x0, sizeof(struct rpdev_sdinfo));
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_sdinfo_en);

/**
 * ambarella_sd_rpmsg_cd
 */
void ambarella_sd_rpmsg_cd(int slot_id)
{
	struct mmc_host 			*mmc = G_mmc[slot_id];

	mmc_detect_change(mmc, msecs_to_jiffies(1000));
}
EXPORT_SYMBOL(ambarella_sd_rpmsg_cd);
#endif

static void ambarella_sd_check_dma_boundary(u32 address, u32 size, u32 max_size)
{
	u32 start_512kb, end_512kb;

	start_512kb = (address) & (~(max_size - 1));
	end_512kb = (address + size - 1) & (~(max_size - 1));
	BUG_ON(start_512kb != end_512kb);
}

static void ambarella_sd_pre_sg_to_dma(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;
	u32 i, offset;

	for (i = 0, offset = 0; i < pslotinfo->sg_len; i++) {
		memcpy(pslotinfo->buf_vaddress + offset,
			sg_virt(&pslotinfo->sg[i]),
			pslotinfo->sg[i].length);
		offset += pslotinfo->sg[i].length;
	}
	BUG_ON(offset != pslotinfo->dma_size);
	dma_sync_single_for_device(pslotinfo->pinfo->dev, pslotinfo->buf_paddress,
		pslotinfo->dma_size, DMA_TO_DEVICE);
	pslotinfo->dma_address = pslotinfo->buf_paddress;
	pslotinfo->blk_sz |= SD_BLK_SZ_512KB;
}

static void ambarella_sd_pre_sg_to_adma(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;
	int i;
	u32 offset;
	u32 dma_len;
	u32 remain_size;
	u32 current_addr;
	u32 word_num, byte_num;

	dma_len = dma_map_sg(pslotinfo->pinfo->dev, pslotinfo->sg,
		pslotinfo->sg_len, DMA_TO_DEVICE);
	for (i = 0, offset = 0; i < dma_len; i++) {
		remain_size = sg_dma_len(&pslotinfo->sg[i]);
		current_addr = sg_dma_address(&pslotinfo->sg[i]);
		current_addr |= pslotinfo->pinfo->dma_fix;
		if (sd_addr_is_unlign(current_addr)) {
			ambsd_err(pslotinfo, "Please disable ADMA\n");
			BUG();
		}

		while (unlikely(remain_size > SD_ADMA_TBL_LINE_MAX_LEN)) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_WORD |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
			offset += SD_ADMA_TBL_LINE_SIZE;
			current_addr += SD_ADMA_TBL_LINE_MAX_LEN;
			remain_size -= SD_ADMA_TBL_LINE_MAX_LEN;
		}
		word_num = remain_size >> 2;
		byte_num = remain_size - (word_num << 2);
		if (word_num) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_WORD |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				(word_num << 16);
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
			current_addr += (word_num << 2);
			if (byte_num) {
				offset += SD_ADMA_TBL_LINE_SIZE;
			}
		}
		if (byte_num) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				byte_num << 16;
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
		}
		if (unlikely(i == dma_len - 1)) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				SD_ADMA_TBL_ATTR_END;
		}
		offset += SD_ADMA_TBL_LINE_SIZE;
	}
	dma_sync_single_for_device(pslotinfo->pinfo->dev, pslotinfo->buf_paddress,
		offset, DMA_TO_DEVICE);
	pslotinfo->dma_address = pslotinfo->buf_paddress;
	pslotinfo->blk_sz |= SD_BLK_SZ_512KB;
}

static void ambarella_sd_post_sg_to_dma(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;

	dma_sync_single_for_cpu(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress,
			pslotinfo->dma_size,
			DMA_TO_DEVICE);
}

static void ambarella_sd_post_sg_to_adma(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;

	dma_sync_single_for_cpu(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress,
			pslotinfo->dma_size,
			DMA_FROM_DEVICE);

	dma_unmap_sg(pslotinfo->pinfo->dev,
			pslotinfo->sg,
			pslotinfo->sg_len,
			DMA_TO_DEVICE);
}

static void ambarella_sd_pre_dma_to_sg(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;

	dma_sync_single_for_device(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress,
			pslotinfo->dma_size,
			DMA_FROM_DEVICE);

	pslotinfo->dma_address = pslotinfo->buf_paddress;
	pslotinfo->blk_sz |= SD_BLK_SZ_512KB;
}

static void ambarella_sd_pre_adma_to_sg(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;
	int i;
	u32 dma_len;
	u32 offset;
	u32 remain_size;
	u32 current_addr;
	u32 word_num, byte_num;

	dma_len = dma_map_sg(pslotinfo->pinfo->dev, pslotinfo->sg,
		pslotinfo->sg_len, DMA_FROM_DEVICE);
	for (i = 0, offset = 0; i < dma_len; i++) {
		remain_size = sg_dma_len(&pslotinfo->sg[i]);
		current_addr = sg_dma_address(&pslotinfo->sg[i]);
		current_addr |= pslotinfo->pinfo->dma_fix;
		if (sd_addr_is_unlign(current_addr)) {
			ambsd_err(pslotinfo, "Please disable ADMA\n");
			BUG();
		}

		while (unlikely(remain_size > SD_ADMA_TBL_LINE_MAX_LEN)) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_WORD |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
			offset += SD_ADMA_TBL_LINE_SIZE;
			current_addr += SD_ADMA_TBL_LINE_MAX_LEN;
			remain_size -= SD_ADMA_TBL_LINE_MAX_LEN;
		}
		word_num = remain_size >> 2;
		byte_num = remain_size - (word_num << 2);
		if (word_num) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_WORD |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				(word_num << 16);
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
			current_addr += (word_num << 2);
			if (byte_num) {
				offset += SD_ADMA_TBL_LINE_SIZE;
			}
		}
		if (byte_num) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) =
				(SD_ADMA_TBL_ATTR_TRAN |
				SD_ADMA_TBL_ATTR_VALID);
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				(byte_num << 16);
			*(u32 *)(pslotinfo->buf_vaddress + offset + 4) =
				current_addr;
		}
		if (unlikely(i == dma_len - 1)) {
			*(u32 *)(pslotinfo->buf_vaddress + offset) |=
				SD_ADMA_TBL_ATTR_END;
		}
		offset += SD_ADMA_TBL_LINE_SIZE;
	}
	dma_sync_single_for_device(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress, offset,
			DMA_TO_DEVICE);
	pslotinfo->dma_address = pslotinfo->buf_paddress;
	pslotinfo->blk_sz |= SD_BLK_SZ_512KB;
}

static void ambarella_sd_post_dma_to_sg(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;
	u32 i, offset;

	dma_sync_single_for_cpu(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress,
			pslotinfo->dma_size,
			DMA_FROM_DEVICE);

	for (i = 0, offset = 0; i < pslotinfo->sg_len; i++) {
		memcpy(sg_virt(&pslotinfo->sg[i]),
			pslotinfo->buf_vaddress + offset,
			pslotinfo->sg[i].length);
		offset += pslotinfo->sg[i].length;
	}
	BUG_ON(offset != pslotinfo->dma_size);
}

static void ambarella_sd_post_adma_to_sg(void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo = data;

	dma_sync_single_for_cpu(pslotinfo->pinfo->dev,
			pslotinfo->buf_paddress,
			pslotinfo->dma_size,
			DMA_FROM_DEVICE);

	dma_unmap_sg(pslotinfo->pinfo->dev,
			pslotinfo->sg,
			pslotinfo->sg_len,
			DMA_FROM_DEVICE);
}

static void ambarella_sd_request_bus(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info	*pinfo;

	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;

	down(&pslotinfo->system_event_sem);

	if ((u32) pinfo->regbase == SD0_BASE) {
		fio_select_lock(SELECT_FIO_SD);
	} else if ((u32) pinfo->regbase == SD1_BASE) {
		fio_select_lock(SELECT_FIO_SDIO);
		if (pslotinfo->force_gpio)
			pinctrl_select_state(pslotinfo->pinctrl, pslotinfo->state_work);
	} else {
	        //fio_select_lock(SELECT_FIO_SD2);
	}

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	boss_set_irq_owner(pinfo->irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
#if defined(CONFIG_RPMSG_SD) && !defined(CONFIG_PLAT_AMBARELLA_BOSS)
	disable_irq(pinfo->irq);
	enable_irq(pinfo->irq);
	//enable_irq(pslotinfo->plat_info->gpio_cd.irq_line);
#endif
}

static void ambarella_sd_release_bus(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
        struct ambarella_sd_controller_info     *pinfo;

        pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;

	if ((u32) pinfo->regbase == SD0_BASE) {
		fio_unlock(SELECT_FIO_SD);
	} else if ((u32) pinfo->regbase == SD1_BASE) {
		if (pslotinfo->force_gpio)
			pinctrl_select_state(pslotinfo->pinctrl, pslotinfo->state_idle);
		fio_unlock(SELECT_FIO_SDIO);
	} else {
	        //fio_unlock(SELECT_FIO_SD2);
	}

	up(&pslotinfo->system_event_sem);
}

static void ambarella_sd_enable_int(struct mmc_host *mmc, u32 mask)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	if (pinfo->slot_num > 1) {
		if ((u32) pinfo->regbase == SD0_BASE)
			fio_amb_sd0_set_int(mask, 1);
		else
			fio_amb_sdio0_set_int(mask, 1);
	} else {
		amba_setbitsl(pinfo->regbase + SD_NISEN_OFFSET, mask);
		amba_setbitsl(pinfo->regbase + SD_NIXEN_OFFSET, mask);
	}

}

static void ambarella_sd_disable_int(struct mmc_host *mmc, u32 mask)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	if (pinfo->slot_num > 1) {
		if ((u32) pinfo->regbase == SD0_BASE)
			fio_amb_sd0_set_int(mask, 0);
		else
			fio_amb_sdio0_set_int(mask, 0);
	} else {
		amba_clrbitsl(pinfo->regbase + SD_NISEN_OFFSET, mask);
		amba_clrbitsl(pinfo->regbase + SD_NIXEN_OFFSET, mask);
	}
}

static void ambarella_sd_set_iclk(struct mmc_host *mmc, u16 clk_div)
{
	u16 clkreg;
	u32 counter = 0;
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

#if defined(CONFIG_RPMSG_SD)
        struct rpdev_sdinfo *sdinfo = ambarella_sd_sdinfo_get(mmc);

        if (clk_div == 0 && sdinfo->is_init && sdinfo->from_rpmsg) {
		clk_div = amba_readw(pinfo->regbase + SD_CLK_OFFSET);
	} else {
		clk_div <<= 8;
	}
#else
	clk_div <<= 8;
#endif
	clk_div |= SD_CLK_ICLK_EN;
	amba_writew(pinfo->regbase + SD_CLK_OFFSET, clk_div);
	while (1) {
		clkreg = amba_readw(pinfo->regbase + SD_CLK_OFFSET);
		if (clkreg & SD_CLK_ICLK_STABLE)
			break;
		if ((clkreg & ~SD_CLK_ICLK_STABLE) != clk_div) {
			amba_writew(pinfo->regbase + SD_CLK_OFFSET, clk_div);
			udelay(1);
		}
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo,
				"Wait SD_CLK_ICLK_STABLE = %d @ 0x%x\n",
				counter, clkreg);
			break;
		}
	}
}

static void ambarella_sd_clear_clken(struct mmc_host *mmc)
{
	u16 clkreg;
	u32 counter = 0;
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	while (1) {
		clkreg = amba_readw(pinfo->regbase + SD_CLK_OFFSET);
		if (clkreg & SD_CLK_EN) {
			amba_writew(pinfo->regbase + SD_CLK_OFFSET,
				(clkreg & ~SD_CLK_EN));
			udelay(1);
		} else {
			break;
		}
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo, "%s(%d @ 0x%x)\n",
				__func__, counter, clkreg);
			break;
		}
	}
}

static void ambarella_sd_set_clken(struct mmc_host *mmc)
{
	u16 clkreg;
	u32 counter = 0;
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	while (1) {
		clkreg = amba_readw(pinfo->regbase + SD_CLK_OFFSET);
		if (clkreg & SD_CLK_EN) {
			break;
		} else {
			amba_writew(pinfo->regbase + SD_CLK_OFFSET,
				(clkreg | SD_CLK_EN));
			udelay(1);
		}
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo, "%s(%d @ 0x%x)\n",
				__func__, counter, clkreg);
			break;
		}
	}
}

static void ambarella_sd_reset_all(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 nis_flag = 0;
	u32 eis_flag = 0;
	u32 counter = 0;
	u8 reset_reg;

	ambsd_dbg(pslotinfo, "Enter %s with state %u\n",
		__func__, pslotinfo->state);

	ambarella_sd_disable_int(mmc, 0xFFFFFFFF);
	amba_write2w(pinfo->regbase + SD_NIS_OFFSET, 0xFFFF, 0xFFFF);
	amba_writeb(pinfo->regbase + SD_RESET_OFFSET, SD_RESET_ALL);
	while (1) {
		reset_reg = amba_readb(pinfo->regbase + SD_RESET_OFFSET);
		if (!(reset_reg & SD_RESET_ALL))
			break;
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo, "Wait SD_RESET_ALL....\n");
			break;
		}
	}

	ambarella_sd_set_iclk(mmc, 0x0000);
	amba_writeb(pinfo->regbase + SD_TMO_OFFSET,
		CONFIG_SD_AMBARELLA_TIMEOUT_VAL);

	nis_flag = SD_NISEN_DMA		|
		SD_NISEN_BLOCK_GAP	|
		SD_NISEN_XFR_DONE	|
		SD_NISEN_CMD_DONE;

	if (pslotinfo->fixed_cd != SD_PIN_BYPIN) {
		/* CD pin is fixed or controlled. */
		nis_flag &= ~(SD_NISEN_REMOVAL | SD_NISEN_INSERT);
	} else {
		nis_flag |= SD_NISEN_REMOVAL | SD_NISEN_INSERT;
	}
	eis_flag = SD_EISEN_ACMD12_ERR	|
		SD_EISEN_CURRENT_ERR	|
		SD_EISEN_DATA_BIT_ERR	|
		SD_EISEN_DATA_CRC_ERR	|
		SD_EISEN_DATA_TMOUT_ERR	|
		SD_EISEN_CMD_IDX_ERR	|
		SD_EISEN_CMD_BIT_ERR	|
		SD_EISEN_CMD_CRC_ERR	|
		SD_EISEN_CMD_TMOUT_ERR;

	if (pslotinfo->use_adma == 1)
		eis_flag |= SD_EISEN_ADMA_ERR;
	else
		eis_flag &= ~SD_EISEN_ADMA_ERR;

	ambarella_sd_enable_int(mmc, (eis_flag << 16) | nis_flag);

	pslotinfo->state = AMBA_SD_STATE_RESET;
	pinfo->reset_error = 0;

	ambsd_dbg(pslotinfo, "Exit %s with counter %u\n", __func__, counter);
}

static void ambarella_sd_reset_cmd_line(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 counter = 0;
	u8 reset_reg;

	ambsd_dbg(pslotinfo, "Enter %s with state %u\n",
		__func__, pslotinfo->state);

	amba_writeb(pinfo->regbase + SD_RESET_OFFSET, SD_RESET_CMD);
	while (1) {
		reset_reg = amba_readb(pinfo->regbase + SD_RESET_OFFSET);
		if (!(reset_reg & SD_RESET_CMD))
			break;
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo, "Wait SD_RESET_CMD...\n");
			pinfo->reset_error = 1;
			break;
		}
	}

	ambsd_dbg(pslotinfo, "Exit %s with counter %u\n", __func__, counter);
}

static void ambarella_sd_reset_data_line(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 counter = 0;
	u8 reset_reg;

	ambsd_dbg(pslotinfo, "Enter %s with state %u\n",
		__func__, pslotinfo->state);

	amba_writeb(pinfo->regbase + SD_RESET_OFFSET, SD_RESET_DAT);
	while (1) {
		reset_reg = amba_readb(pinfo->regbase + SD_RESET_OFFSET);
		if (!(reset_reg & SD_RESET_DAT))
			break;
		counter++;
		if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
			ambsd_warn(pslotinfo, "Wait SD_RESET_DAT...\n");
			pinfo->reset_error = 1;
			break;
		}
	}

	ambsd_dbg(pslotinfo, "Exit %s with counter %u\n", __func__, counter);
}

static inline void ambarella_sd_data_done(
	struct ambarella_sd_mmc_info *pslotinfo, u16 nis, u16 eis)
{
	struct mmc_data *data;

	if ((pslotinfo->state == AMBA_SD_STATE_CMD) &&
		((pslotinfo->cmd_reg & 0x3) == SD_CMD_RSP_48BUSY)) {
		if (eis) {
			pslotinfo->state = AMBA_SD_STATE_ERR;
		} else {
			pslotinfo->state = AMBA_SD_STATE_IDLE;
		}
		wake_up(&pslotinfo->wait);
		return;
	}
	if (pslotinfo->mrq == NULL) {
		ambsd_dbg(pslotinfo, "%s: mrq is NULL, nis[0x%x] eis[0x%x]\n",
			__func__, nis, eis);
		return;
	}
	if (pslotinfo->mrq->data == NULL) {
		ambsd_dbg(pslotinfo, "%s: data is NULL, nis[0x%x] eis[0x%x]\n",
			__func__, nis, eis);
		return;
	}

	data = pslotinfo->mrq->data;
	if (eis) {
		if (eis & SD_EIS_DATA_BIT_ERR) {
			data->error = -EILSEQ;
		} else if (eis & SD_EIS_DATA_CRC_ERR) {
			data->error = -EILSEQ;
		} else if (eis & SD_EIS_ADMA_ERR) {
			data->error = -EILSEQ;
		} else if (eis & SD_EIS_DATA_TMOUT_ERR) {
			data->error = -ETIMEDOUT;
		} else {
			data->error = -EIO;
		}
#ifdef CONFIG_SD_AMBARELLA_DEBUG_VERBOSE
		ambsd_err(pslotinfo, "%s: CMD[%u] get eis[0x%x]\n", __func__,
			pslotinfo->mrq->cmd->opcode, eis);
#endif
		pslotinfo->state = AMBA_SD_STATE_ERR;
		wake_up(&pslotinfo->wait);
		return;
	} else {
		data->bytes_xfered = pslotinfo->dma_size;
	}

	pslotinfo->state = AMBA_SD_STATE_IDLE;
	wake_up(&pslotinfo->wait);
}

static inline void ambarella_sd_cmd_done(
	struct ambarella_sd_mmc_info *pslotinfo, u16 nis, u16 eis)
{
	struct mmc_command *cmd;
	u32 rsp0, rsp1, rsp2, rsp3;
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u16 ac12es;

	if (pslotinfo->mrq == NULL) {
		ambsd_dbg(pslotinfo, "%s: mrq is NULL, nis[0x%x] eis[0x%x]\n",
			__func__, nis, eis);
		return;
	}
	if (pslotinfo->mrq->cmd == NULL) {
		ambsd_dbg(pslotinfo, "%s: cmd is NULL, nis[0x%x] eis[0x%x]\n",
			__func__, nis, eis);
		return;
	}

	cmd = pslotinfo->mrq->cmd;
	if (eis) {
		if (eis & SD_EIS_CMD_BIT_ERR) {
			cmd->error = -EILSEQ;
		} else if (eis & SD_EIS_CMD_CRC_ERR) {
			cmd->error = -EILSEQ;
		} else if (eis & SD_EIS_CMD_TMOUT_ERR) {
			cmd->error = -ETIMEDOUT;
		} else if (eis & SD_EIS_ACMD12_ERR) {
			ac12es = amba_readl(pinfo->regbase + SD_AC12ES_OFFSET);
			if (ac12es & SD_AC12ES_TMOUT_ERROR) {
				cmd->error = -ETIMEDOUT;
			} else if (eis & SD_AC12ES_CRC_ERROR) {
				cmd->error = -EILSEQ;
			} else {
				cmd->error = -EIO;
			}

			if (pslotinfo->mrq->stop) {
				pslotinfo->mrq->stop->error = cmd->error;
			} else {
				ambsd_err(pslotinfo, "%s NULL stop 0x%x %u\n",
					__func__, ac12es, cmd->error);
			}
		} else {
			cmd->error = -EIO;
		}
#ifdef CONFIG_SD_AMBARELLA_DEBUG_VERBOSE
		ambsd_err(pslotinfo, "%s: CMD[%u] get eis[0x%x]\n", __func__,
			pslotinfo->mrq->cmd->opcode, eis);
#endif
		pslotinfo->state = AMBA_SD_STATE_ERR;
		wake_up(&pslotinfo->wait);
		return;
	}

	if (cmd->flags & MMC_RSP_136) {
		rsp0 = amba_readl(pinfo->regbase + SD_RSP0_OFFSET);
		rsp1 = amba_readl(pinfo->regbase + SD_RSP1_OFFSET);
		rsp2 = amba_readl(pinfo->regbase + SD_RSP2_OFFSET);
		rsp3 = amba_readl(pinfo->regbase + SD_RSP3_OFFSET);
		cmd->resp[0] = ((rsp3 << 8) | (rsp2 >> 24));
		cmd->resp[1] = ((rsp2 << 8) | (rsp1 >> 24));
		cmd->resp[2] = ((rsp1 << 8) | (rsp0 >> 24));
		cmd->resp[3] = (rsp0 << 8);
	} else {
		cmd->resp[0] = amba_readl(pinfo->regbase + SD_RSP0_OFFSET);
	}

	if ((pslotinfo->state == AMBA_SD_STATE_CMD) &&
		((pslotinfo->cmd_reg & 0x3) != SD_CMD_RSP_48BUSY)) {
		pslotinfo->state = AMBA_SD_STATE_IDLE;
		wake_up(&pslotinfo->wait);
	}
}

static irqreturn_t ambarella_sd_irq(int irq, void *devid)
{
	struct ambarella_sd_controller_info *pinfo = devid;
	struct ambarella_sd_mmc_info *pslotinfo = NULL;
	u16 nis, eis, slot_id;

	/* Read and clear the interrupt registers */
	amba_read2w(pinfo->regbase + SD_NIS_OFFSET, &nis, &eis);
	amba_write2w(pinfo->regbase + SD_NIS_OFFSET, nis, eis);

	if (pinfo->slot_num > 1) {
		u32 fio_ctrl = amba_readl(pinfo->fio_reg + FIO_CTR_OFFSET);
		slot_id = (fio_ctrl & FIO_CTR_XD) ? 1 : 0;
	} else
		slot_id = 0;

	pslotinfo = pinfo->pslotinfo[slot_id];

	ambsd_dbg(pslotinfo, "%s nis = 0x%x, eis = 0x%x & %u\n",
				__func__, nis, eis, pslotinfo->state);

	if (nis & SD_NIS_CARD) {
		ambsd_dbg(pslotinfo, "SD_NIS_CARD\n");
		mmc_signal_sdio_irq(pslotinfo->mmc);
	}

#if defined(CONFIG_RPMSG_SD)
	if (nis & SD_NIS_REMOVAL) {
		ambsd_dbg(pslotinfo, "SD_NIS_REMOVAL\n");
		pslotinfo->insert = 0;
		schedule_delayed_work(&pslotinfo->detect, msecs_to_jiffies(1000));
	} else if (nis & SD_NIS_INSERT) {
		ambsd_dbg(pslotinfo, "SD_NIS_INSERT\n");
		pslotinfo->insert = 1;
		schedule_delayed_work(&pslotinfo->detect, msecs_to_jiffies(1000));
	}
#else
	if (nis & SD_NIS_REMOVAL) {
		ambsd_dbg(pslotinfo, "SD_NIS_REMOVAL\n");
		mmc_detect_change(pslotinfo->mmc, msecs_to_jiffies(1000));
	} else if (nis & SD_NIS_INSERT) {
		ambsd_dbg(pslotinfo, "SD_NIS_INSERT\n");
		mmc_detect_change(pslotinfo->mmc, msecs_to_jiffies(1000));
	}
#endif

	if (eis) {
		if (eis & (SD_EIS_CMD_TMOUT_ERR | SD_EIS_CMD_CRC_ERR |
			SD_EIS_CMD_BIT_ERR | SD_EIS_CMD_IDX_ERR |
			SD_EIS_ACMD12_ERR)) {
			ambarella_sd_reset_cmd_line(pslotinfo->mmc);
		}
		if (eis & (SD_EIS_DATA_TMOUT_ERR | SD_EIS_DATA_CRC_ERR)) {
			ambarella_sd_reset_data_line(pslotinfo->mmc);
		}
		if (eis & (SD_EIS_DATA_BIT_ERR | SD_EIS_CURRENT_ERR)) {
			ambarella_sd_reset_all(pslotinfo->mmc);
		}
		if (pslotinfo->state == AMBA_SD_STATE_CMD) {
			ambarella_sd_cmd_done(pslotinfo, nis, eis);
		} else if (pslotinfo->state == AMBA_SD_STATE_DATA) {
			ambarella_sd_data_done(pslotinfo, nis, eis);
		}
	} else {
		if (nis & SD_NIS_CMD_DONE) {
			ambarella_sd_cmd_done(pslotinfo, nis, eis);
		}
		if (nis & SD_NIS_XFR_DONE) {
			ambarella_sd_data_done(pslotinfo, nis, eis);
		}
#if 0
		if (nis & SD_NIS_DMA) {
			amba_writel(pinfo->regbase + SD_DMA_ADDR_OFFSET,
			amba_readl(pinfo->regbase + SD_DMA_ADDR_OFFSET));
		}
#endif
	}

	return IRQ_HANDLED;
}

static void ambarella_sd_set_clk(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 sd_clk, desired_clk, actual_clk, bneed_div = 1;
	u16 clk_div = 0x0000;

	ambarella_sd_clear_clken(mmc);
	if (ios->clock != 0) {
#if defined(CONFIG_RPMSG_SD)
		struct rpdev_sdinfo *sdinfo = ambarella_sd_sdinfo_get(mmc);

		if (sdinfo->is_init && sdinfo->from_rpmsg)
			goto done;
#endif
		desired_clk = ios->clock;
		if (desired_clk > mmc->f_max)
			desired_clk = mmc->f_max;

		if (desired_clk < 10000000) {
			/* Below 10Mhz, divide by sd controller */
			clk_set_rate(pinfo->clk, mmc->f_max);
		} else {
			clk_set_rate(pinfo->clk, desired_clk);
			actual_clk = clk_get_rate(pinfo->clk);
			bneed_div = 0;
		}

		if (bneed_div) {
			sd_clk = clk_get_rate(pinfo->clk);
			for (clk_div = 0x0; clk_div <= 0x80;) {
				if (clk_div == 0)
					actual_clk = sd_clk;
				else
					actual_clk = sd_clk / (clk_div << 1);

				if (actual_clk <= desired_clk)
					break;

				if (clk_div >= 0x80)
					break;

				if (clk_div == 0x0)
					clk_div = 0x1;
				else
					clk_div <<= 1;
			}
		}
#if defined(CONFIG_RPMSG_SD)
done:
#endif
		ambsd_dbg(pslotinfo, "sd_pll = %lu.\n", clk_get_rate(pinfo->clk));
		ambsd_dbg(pslotinfo, "desired_clk = %u.\n", desired_clk);
		ambsd_dbg(pslotinfo, "actual_clk = %u.\n", actual_clk);
		ambsd_dbg(pslotinfo, "clk_div = %u.\n", clk_div);

		ambarella_sd_set_iclk(mmc, clk_div);
		ambarella_sd_set_clken(mmc);
	}
}

static void ambarella_sd_set_pwr(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	if (ios->power_mode == MMC_POWER_OFF) {
		ambarella_sd_reset_all(pslotinfo->mmc);
		amba_writeb(pinfo->regbase + SD_PWR_OFFSET, SD_PWR_OFF);

		if (gpio_is_valid(pslotinfo->pwr_gpio)) {
			gpio_set_value_cansleep(pslotinfo->pwr_gpio,
						!pslotinfo->pwr_gpio_active);
			msleep(300);
		}

		if (gpio_is_valid(pslotinfo->v18_gpio)) {
			gpio_set_value_cansleep(pslotinfo->v18_gpio,
						!pslotinfo->v18_gpio_active);
			msleep(10);
		}
	} else if (ios->power_mode == MMC_POWER_UP) {
		if (gpio_is_valid(pslotinfo->v18_gpio)) {
			gpio_set_value_cansleep(pslotinfo->v18_gpio,
						!pslotinfo->v18_gpio_active);
			msleep(10);
		}

		if (gpio_is_valid(pslotinfo->pwr_gpio)) {
			gpio_set_value_cansleep(pslotinfo->pwr_gpio,
						pslotinfo->pwr_gpio_active);
			msleep(300);
		}

		amba_writeb(pinfo->regbase + SD_PWR_OFFSET,
			(SD_PWR_ON | SD_PWR_3_3V));
	} else if (ios->power_mode == MMC_POWER_ON) {
		switch (1 << ios->vdd) {
		case MMC_VDD_165_195:
		case MMC_VDD_32_33:
		case MMC_VDD_33_34:
			break;
		default:
			ambsd_err(pslotinfo, "%s Wrong voltage[%u]!\n",
				__func__, ios->vdd);
			break;
		}
	}
	msleep(1);
	ambsd_dbg(pslotinfo, "pwr = 0x%x.\n",
		amba_readb(pinfo->regbase + SD_PWR_OFFSET));
}

static void ambarella_sd_set_phy_timing(
		struct ambarella_sd_controller_info *pinfo, int mode)
{
	u32 i, val0, val1;

	if ((!pinfo->soft_phy && pinfo->timing_reg == NULL) || pinfo->phy_timing_num == 0)
		return;

	for (i = 0; i < pinfo->phy_timing_num; i++) {
		if (pinfo->phy_timing[i].mode & (0x1 << mode))
			break;
	}

	/* phy setting is not defined in DTS for this mode, so we use the
	 * default phy setting defined for DS mode. */
	if (i >= pinfo->phy_timing_num)
		i = 0;

	val0 = pinfo->phy_timing[i].val0;
	val1 = pinfo->phy_timing[i].val1;

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	/* Only touch the setting controlled by linux. */
	if (pinfo->pslotinfo[0]->mmc == G_mmc[SDIO_GLOBAL_ID]) {
#endif
	if (pinfo->soft_phy) {
		if(pinfo->timing_reg != NULL) {
			amba_writel(pinfo->timing_reg, val0 | 0x02000000);
			amba_writel(pinfo->timing_reg, val0);
			amba_writel(pinfo->regbase + SD_LAT_CTRL_OFFSET, val1);
		} else {
			amba_writel(pinfo->regbase + SD_DELAY_SEL_L, val0);
			amba_writel(pinfo->regbase + SD_DELAY_SEL_H, val1);
		}
	} else {
		u32 ms_delay = amba_rct_readl(pinfo->timing_reg);
		ms_delay &= val0;
		ms_delay |= val1;
		amba_rct_writel(pinfo->timing_reg, ms_delay);
	}
#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	}
#endif
}

static void ambarella_sd_set_bus(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u8 hostr = 0;

	hostr = amba_readb(pinfo->regbase + SD_HOST_OFFSET);
	if (ios->bus_width == MMC_BUS_WIDTH_8) {
		hostr |= SD_HOST_8BIT;
		hostr &= ~(SD_HOST_4BIT);
	} else if (ios->bus_width == MMC_BUS_WIDTH_4) {
		hostr &= ~(SD_HOST_8BIT);
		hostr |= SD_HOST_4BIT;
	} else if (ios->bus_width == MMC_BUS_WIDTH_1) {
		hostr &= ~(SD_HOST_8BIT);
		hostr &= ~(SD_HOST_4BIT);
	} else {
		ambsd_err(pslotinfo, "Unknown bus_width[%u], assume 1bit.\n",
			ios->bus_width);
		hostr &= ~(SD_HOST_8BIT);
		hostr &= ~(SD_HOST_4BIT);
	}
	if (force_sdio_host_high_speed == 1)
		hostr |= SD_HOST_HIGH_SPEED;
	else
		hostr &= ~SD_HOST_HIGH_SPEED;
	switch (ios->timing) {
	case MMC_TIMING_LEGACY:
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR12:
	case MMC_TIMING_UHS_SDR25:
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
		amba_clrbitsl(pinfo->regbase + SD_XC_CTR_OFFSET,
			SD_XC_CTR_DDR_EN);
		amba_clrbitsw(pinfo->regbase + SD_HOST2_OFFSET, 0x0004);
		break;
	case MMC_TIMING_UHS_DDR50:
		hostr |= SD_HOST_HIGH_SPEED;
		amba_setbitsl(pinfo->regbase + SD_XC_CTR_OFFSET,
			SD_XC_CTR_DDR_EN);
		amba_setbitsw(pinfo->regbase + SD_HOST2_OFFSET, 0x0004);
		break;
	default:
		amba_clrbitsl(pinfo->regbase + SD_XC_CTR_OFFSET,
			SD_XC_CTR_DDR_EN);
		amba_clrbitsw(pinfo->regbase + SD_HOST2_OFFSET, 0x0004);
		ambsd_err(pslotinfo, "Unknown timing[%d], assume legacy.\n",
			ios->timing);
		break;
	}

	if ((-1!=sdio_host_odly) || (-1!=sdio_host_ocly) || \
		(-1!=sdio_host_idly) ||	(-1!=sdio_host_icly)) {
		amba_sdio_delay_post_apply(sdio_host_odly,
			sdio_host_ocly, sdio_host_idly, sdio_host_icly);
	}

	amba_writeb(pinfo->regbase + SD_HOST_OFFSET, hostr);
	amba_sdio_ds_post_apply(sdio_clk_ds, sdio_data_ds, sdio_cmd_ds, -1);
	ambsd_dbg(pslotinfo, "hostr = 0x%x.\n", hostr);

	ambarella_sd_set_phy_timing(pinfo, ios->timing);
}

static void ambarella_sd_check_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

#if defined(CONFIG_RPMSG_SD)
	struct rpdev_sdinfo *sdinfo = ambarella_sd_sdinfo_get(mmc);

	if (sdinfo->from_rpmsg && !sdinfo->is_sdio) {
		ios->bus_width = (sdinfo->bus_width == 8) ? MMC_BUS_WIDTH_8:
				 (sdinfo->bus_width == 4) ? MMC_BUS_WIDTH_4:
				 MMC_BUS_WIDTH_1;
		ios->clock = sdinfo->clk;
	}
#endif

	if ((pinfo->controller_ios.power_mode != ios->power_mode) ||
		(pinfo->controller_ios.vdd != ios->vdd) ||
		(pslotinfo->state == AMBA_SD_STATE_RESET)) {
		ambarella_sd_set_pwr(mmc, ios);
		pinfo->controller_ios.power_mode = ios->power_mode;
		pinfo->controller_ios.vdd = ios->vdd;
	}

#if defined(CONFIG_RPMSG_SD)
	if (((amba_readw(pinfo->regbase + SD_CLK_OFFSET) & SD_CLK_EN) == 0) &&
            sdinfo->is_init && sdinfo->from_rpmsg) {
		/* RTOS will on/off clock every sd request,
		 * if clock is disable, that means RTOS has ever access sd
		 * controller and we need to set to correct clock again. */
		pinfo->controller_ios.clock = 0;
	}
#endif

	if ((pinfo->controller_ios.clock != ios->clock) ||
		(pslotinfo->state == AMBA_SD_STATE_RESET)) {
		ambarella_sd_set_clk(mmc, ios);
		pinfo->controller_ios.clock = ios->clock;
	}

	if ((pinfo->controller_ios.bus_width != ios->bus_width) ||
		(pinfo->controller_ios.timing != ios->timing) ||
		(pslotinfo->state == AMBA_SD_STATE_RESET)) {
		ambarella_sd_set_bus(mmc, ios);
		pinfo->controller_ios.bus_width = ios->bus_width;
		pinfo->controller_ios.timing = ios->timing;
	}

	if (pslotinfo->state == AMBA_SD_STATE_RESET) {
		pslotinfo->state = AMBA_SD_STATE_IDLE;
	}
}

static u32 ambarella_sd_check_cd(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	int cdpin;

	if (pslotinfo->fixed_cd != SD_PIN_BYPIN) {
		cdpin = !!pslotinfo->fixed_cd;
	} else {
		cdpin = mmc_gpio_get_cd(mmc);
		if (cdpin < 0) {
			cdpin = amba_readl(pinfo->regbase + SD_STA_OFFSET);
			cdpin &= SD_STA_CARD_INSERTED;
		}
	}

	return !!cdpin;
}

static inline void ambarella_sd_prepare_tmo(
	struct ambarella_sd_mmc_info *pslotinfo,
	struct mmc_data *pmmcdata)
{
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	pslotinfo->tmo = CONFIG_SD_AMBARELLA_TIMEOUT_VAL;
	pslotinfo->wait_tmo = min_t(u32, pinfo->default_wait_tmo, CONFIG_SD_AMBARELLA_MAX_TIMEOUT);
	if ((pslotinfo->wait_tmo > 0) && (pslotinfo->wait_tmo <
		CONFIG_SD_AMBARELLA_MAX_TIMEOUT)) {
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
		/* quick recovery from cmd53 timeout */
		pslotinfo->sta_counter = 0;
#else
	        pslotinfo->sta_counter = pinfo->default_wait_tmo / CONFIG_SD_AMBARELLA_MAX_TIMEOUT + 1;
#endif
	} else {
		pslotinfo->sta_counter = 1;
		pslotinfo->wait_tmo = (1 * HZ);
	}

	ambsd_dbg(pslotinfo, "timeout_ns = %u, timeout_clks = %u, "
		"wait_tmo = %u, tmo = %u, sta_counter = %u.\n",
		pmmcdata->timeout_ns, pmmcdata->timeout_clks,
		pslotinfo->wait_tmo, pslotinfo->tmo, pslotinfo->sta_counter);
}

static inline void ambarella_sd_pre_cmd(struct ambarella_sd_mmc_info *pslotinfo)
{
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	pslotinfo->state = AMBA_SD_STATE_CMD;
	pslotinfo->sg_len = 0;
	pslotinfo->sg = NULL;
	pslotinfo->blk_sz = 0;
	pslotinfo->blk_cnt = 0;
	pslotinfo->arg_reg = 0;
	pslotinfo->cmd_reg = 0;
	pslotinfo->sta_reg = 0;
	pslotinfo->tmo = CONFIG_SD_AMBARELLA_TIMEOUT_VAL;
	pslotinfo->wait_tmo = (1 * HZ);
	pslotinfo->xfr_reg = 0;
	pslotinfo->dma_address = 0;
	pslotinfo->dma_size = 0;

	if (pslotinfo->mrq->stop) {
		if (likely(pslotinfo->mrq->stop->opcode ==
			MMC_STOP_TRANSMISSION)) {
			pslotinfo->xfr_reg = SD_XFR_AC12_EN;
		} else {
			ambsd_err(pslotinfo, "%s strange stop cmd%u\n",
				__func__, pslotinfo->mrq->stop->opcode);
		}
	}

	if (!(pslotinfo->mrq->cmd->flags & MMC_RSP_PRESENT))
		pslotinfo->cmd_reg = SD_CMD_RSP_NONE;
	else if (pslotinfo->mrq->cmd->flags & MMC_RSP_136)
		pslotinfo->cmd_reg = SD_CMD_RSP_136;
	else if (pslotinfo->mrq->cmd->flags & MMC_RSP_BUSY)
		pslotinfo->cmd_reg = SD_CMD_RSP_48BUSY;
	else
		pslotinfo->cmd_reg = SD_CMD_RSP_48;
	if (pslotinfo->mrq->cmd->flags & MMC_RSP_CRC)
		pslotinfo->cmd_reg |= SD_CMD_CHKCRC;
	if (pslotinfo->mrq->cmd->flags & MMC_RSP_OPCODE)
		pslotinfo->cmd_reg |= SD_CMD_CHKIDX;
	pslotinfo->cmd_reg |= SD_CMD_IDX(pslotinfo->mrq->cmd->opcode);
	pslotinfo->arg_reg = pslotinfo->mrq->cmd->arg;

	if (pslotinfo->mrq->data) {
		pslotinfo->state = AMBA_SD_STATE_DATA;
		ambarella_sd_prepare_tmo(pslotinfo, pslotinfo->mrq->data);
		pslotinfo->blk_sz = (pslotinfo->mrq->data->blksz & 0xFFF);
		pslotinfo->dma_size = pslotinfo->mrq->data->blksz *
			pslotinfo->mrq->data->blocks;
		pslotinfo->sg_len = pslotinfo->mrq->data->sg_len;
		pslotinfo->sg = pslotinfo->mrq->data->sg;
		pslotinfo->xfr_reg |= SD_XFR_DMA_EN;
		pslotinfo->cmd_reg |= SD_CMD_DATA;
		pslotinfo->blk_cnt = pslotinfo->mrq->data->blocks;
		if (pslotinfo->blk_cnt > 1) {
			pslotinfo->xfr_reg |= SD_XFR_MUL_SEL;
			pslotinfo->xfr_reg |= SD_XFR_BLKCNT_EN;
		}
		if (pslotinfo->mrq->data->flags & MMC_DATA_STREAM) {
			pslotinfo->xfr_reg |= SD_XFR_MUL_SEL;
			pslotinfo->xfr_reg &= ~SD_XFR_BLKCNT_EN;
		}
		if (pslotinfo->mrq->data->flags & MMC_DATA_WRITE) {
			pslotinfo->xfr_reg &= ~SD_XFR_CTH_SEL;
			pslotinfo->sta_reg = (SD_STA_WRITE_XFR_ACTIVE |
				SD_STA_DAT_ACTIVE);
			if (pslotinfo->use_adma == 1) {
				pslotinfo->pre_dma = &ambarella_sd_pre_sg_to_adma;
				pslotinfo->post_dma = &ambarella_sd_post_sg_to_adma;
			} else {
				pslotinfo->pre_dma = &ambarella_sd_pre_sg_to_dma;
				pslotinfo->post_dma = &ambarella_sd_post_sg_to_dma;
			}
		} else {
			pslotinfo->xfr_reg |= SD_XFR_CTH_SEL;
			pslotinfo->sta_reg = (SD_STA_READ_XFR_ACTIVE |
				SD_STA_DAT_ACTIVE);
			if (pslotinfo->use_adma == 1) {
				pslotinfo->pre_dma = &ambarella_sd_pre_adma_to_sg;
				pslotinfo->post_dma = &ambarella_sd_post_adma_to_sg;
			} else {
				pslotinfo->pre_dma = &ambarella_sd_pre_dma_to_sg;
				pslotinfo->post_dma = &ambarella_sd_post_dma_to_sg;
			}
		}
		pslotinfo->pre_dma(pslotinfo);
		if (pinfo->dma_fix) {
			pslotinfo->dma_address |= pinfo->dma_fix;
		}
	}
}

static inline void ambarella_sd_send_cmd(struct ambarella_sd_mmc_info *pslotinfo)
{
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 valid_request, sta_reg, tmpreg, counter = 0;
	long timeout;

	ambarella_sd_request_bus(pslotinfo->mmc);

	valid_request = ambarella_sd_check_cd(pslotinfo->mmc);
	ambsd_dbg(pslotinfo, "cmd = %u valid_request = %u.\n",
		pslotinfo->mrq->cmd->opcode, valid_request);
	if (!valid_request) {
		pslotinfo->mrq->cmd->error = -ENOMEDIUM;
		pslotinfo->state = AMBA_SD_STATE_ERR;
		goto ambarella_sd_send_cmd_exit;
	}

	ambarella_sd_check_ios(pslotinfo->mmc, &pslotinfo->mmc->ios);
	if (pslotinfo->mrq->data) {
		while (1) {
			sta_reg = amba_readl(pinfo->regbase + SD_STA_OFFSET);
			if ((sta_reg & SD_STA_CMD_INHIBIT_DAT) == 0) {
				break;
			}
			counter++;
			if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
				ambsd_warn(pslotinfo,
					"Wait SD_STA_CMD_INHIBIT_DAT...\n");
				pslotinfo->state = AMBA_SD_STATE_ERR;
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
				/* system will hang after reset */
#else
				pinfo->reset_error = 1;
#endif
				goto ambarella_sd_send_cmd_exit;
			}
		}

		amba_writeb(pinfo->regbase + SD_TMO_OFFSET, pslotinfo->tmo);
		if (pslotinfo->use_adma == 1) {
			amba_setbitsb(pinfo->regbase + SD_HOST_OFFSET,
				SD_HOST_ADMA);
			amba_writel(pinfo->regbase + SD_ADMA_ADDR_OFFSET,
				pslotinfo->dma_address);
		} else {
			amba_clrbitsb(pinfo->regbase + SD_HOST_OFFSET,
				SD_HOST_ADMA);
			amba_writel(pinfo->regbase + SD_DMA_ADDR_OFFSET,
				pslotinfo->dma_address);
		}
		amba_write2w(pinfo->regbase + SD_BLK_SZ_OFFSET,
			pslotinfo->blk_sz, pslotinfo->blk_cnt);
		amba_writel(pinfo->regbase + SD_ARG_OFFSET, pslotinfo->arg_reg);
		amba_write2w(pinfo->regbase + SD_XFR_OFFSET,
			pslotinfo->xfr_reg, pslotinfo->cmd_reg);
	} else {
		while (1) {
			sta_reg = amba_readl(pinfo->regbase + SD_STA_OFFSET);
			if ((sta_reg & SD_STA_CMD_INHIBIT_CMD) == 0) {
				break;
			}
			counter++;
			if (counter > CONFIG_SD_AMBARELLA_WAIT_COUNTER_LIMIT) {
				ambsd_warn(pslotinfo,
					"Wait SD_STA_CMD_INHIBIT_CMD...\n");
				pslotinfo->state = AMBA_SD_STATE_ERR;
				pinfo->reset_error = 1;
				goto ambarella_sd_send_cmd_exit;
			}
		}

		amba_writel(pinfo->regbase + SD_ARG_OFFSET, pslotinfo->arg_reg);
		amba_write2w(pinfo->regbase + SD_XFR_OFFSET,
			0x00, pslotinfo->cmd_reg);
	}

ambarella_sd_send_cmd_exit:
	if (pslotinfo->state == AMBA_SD_STATE_CMD) {
		timeout = wait_event_timeout(pslotinfo->wait,
			(pslotinfo->state != AMBA_SD_STATE_CMD),
			pslotinfo->wait_tmo);
		if (pslotinfo->state == AMBA_SD_STATE_CMD) {
			ambsd_err(pslotinfo,
				"cmd%u %u@[%ld:%u], sta=0x%04x\n",
				pslotinfo->mrq->cmd->opcode,
				pslotinfo->state, timeout, pslotinfo->wait_tmo,
				amba_readl(pinfo->regbase + SD_STA_OFFSET));
			pslotinfo->mrq->cmd->error = -ETIMEDOUT;
		}
	} else if (pslotinfo->state == AMBA_SD_STATE_DATA) {
		do {
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	/* quick recovery from cmd53 timeout */
	pslotinfo->wait_tmo = (1 * HZ);
#endif
			timeout = wait_event_timeout(pslotinfo->wait,
				(pslotinfo->state != AMBA_SD_STATE_DATA),
				pslotinfo->wait_tmo);
			sta_reg = amba_readl(pinfo->regbase + SD_STA_OFFSET);
			if ((pslotinfo->state == AMBA_SD_STATE_DATA) &&
				(sta_reg & pslotinfo->sta_reg)) {
				ambsd_rtdbg(pslotinfo, "data%u %u@"
					"[%ld:%u:%u:%u], sta=0x%04x:0x%04x\n",
					pslotinfo->mrq->cmd->opcode,
					pslotinfo->state, timeout,
					pslotinfo->wait_tmo,
					pslotinfo->mrq->data->timeout_ns,
					pslotinfo->mrq->data->timeout_clks,
					sta_reg, pslotinfo->sta_reg);
				ambsd_rtdbg(pslotinfo,
					"DMA %u in %u sg [0x%08x:0x%08x]\n",
					pslotinfo->dma_size,
					pslotinfo->sg_len,
					pslotinfo->dma_address,
					pslotinfo->dma_size);
				tmpreg = amba_readw(pinfo->regbase +
					SD_BLK_CNT_OFFSET);
				if (tmpreg) {
					ambsd_rtdbg(pslotinfo,
						"SD_DMA_ADDR_OFFSET[0x%08x]\n",
						amba_readl(pinfo->regbase +
						SD_DMA_ADDR_OFFSET));
					amba_writel((pinfo->regbase +
						SD_DMA_ADDR_OFFSET),
						amba_readl(pinfo->regbase +
						SD_DMA_ADDR_OFFSET));
				} else {
					ambsd_rtdbg(pslotinfo,
						"SD_DATA_OFFSET[0x%08x]\n",
						amba_readl(pinfo->regbase +
						SD_DATA_OFFSET));
					ambsd_rtdbg(pslotinfo,
						"SD_STA_OFFSET[0x%08x]\n",
						amba_readl(pinfo->regbase +
						SD_STA_OFFSET));
				}
			} else {
				break;
			}
		} while (pslotinfo->sta_counter--);
		if (pslotinfo->state == AMBA_SD_STATE_DATA) {
			ambsd_err(pslotinfo,
				"data%u %u@%u, sta=0x%04x:0x%04x\n",
				pslotinfo->mrq->cmd->opcode,
				pslotinfo->state,
				pslotinfo->wait_tmo,
				sta_reg, pslotinfo->sta_reg);
			pslotinfo->mrq->data->error = -ETIMEDOUT;
		}
	}
}

static inline void ambarella_sd_post_cmd(struct ambarella_sd_mmc_info *pslotinfo)
{
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	if (pslotinfo->state == AMBA_SD_STATE_IDLE) {
		ambarella_sd_release_bus(pslotinfo->mmc);
		if (pslotinfo->mrq->data) {
			pslotinfo->post_dma(pslotinfo);
		}
	} else {
#ifdef CONFIG_SD_AMBARELLA_DEBUG_VERBOSE
		u32 counter = 0;

		ambsd_err(pslotinfo, "CMD%u retries[%u] state[%u].\n",
			pslotinfo->mrq->cmd->opcode,
			pslotinfo->mrq->cmd->retries,
			pslotinfo->state);
		for (counter = 0; counter < 0x100; counter += 4) {
			ambsd_err(pslotinfo, "0x%04x: 0x%08x\n",
			counter, amba_readl(pinfo->regbase + counter));
		}
		ambarella_sd_show_info(pslotinfo);
#endif
		if (pinfo->reset_error) {
			ambarella_sd_reset_all(pslotinfo->mmc);
		}
		ambarella_sd_release_bus(pslotinfo->mmc);
	}
}

static void ambarella_sd_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);

	pslotinfo->mrq = mrq;
	ambarella_sd_pre_cmd(pslotinfo);
	ambarella_sd_send_cmd(pslotinfo);
	ambarella_sd_post_cmd(pslotinfo);
	pslotinfo->mrq = NULL;
	mmc_request_done(mmc, mrq);
}

static void ambarella_sd_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	ambarella_sd_request_bus(mmc);
	ambarella_sd_check_ios(mmc, ios);
	ambarella_sd_release_bus(mmc);
}

static int ambarella_sd_get_ro(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	int wpspl;

	ambarella_sd_request_bus(mmc);

	if (pslotinfo->fixed_wp != SD_PIN_BYPIN) {
		wpspl = !!pslotinfo->fixed_wp;
	} else {
		wpspl = mmc_gpio_get_ro(mmc);
		if (wpspl < 0) {
			wpspl = amba_readl(pinfo->regbase + SD_STA_OFFSET);
			wpspl &= SD_STA_WPS_PL;
		}
	}

	ambarella_sd_release_bus(mmc);

	ambsd_dbg(pslotinfo, "RO[%u].\n", wpspl);
	return !!wpspl;
}

static int ambarella_sd_get_cd(struct mmc_host *mmc)
{
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	u32 cdpin;

	ambarella_sd_request_bus(mmc);
	cdpin = ambarella_sd_check_cd(mmc);
	ambarella_sd_release_bus(mmc);

	ambsd_dbg(pslotinfo, "CD[%u].\n", cdpin);
	return cdpin ? 1 : 0;
}

static void ambarella_sd_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	if (enable)
		ambarella_sd_enable_int(mmc, SD_NISEN_CARD);
	else
		ambarella_sd_disable_int(mmc, SD_NISEN_CARD);
}

static int ambarella_sd_ssvs(struct mmc_host *mmc, struct mmc_ios *ios)
{
	int retval = 0;
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;

	ambarella_sd_request_bus(mmc);
	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330) {
		amba_writeb(pinfo->regbase + SD_PWR_OFFSET,
			(SD_PWR_ON | SD_PWR_3_3V));

		if (gpio_is_valid(pslotinfo->v18_gpio)) {
			gpio_set_value_cansleep(pslotinfo->v18_gpio,
						!pslotinfo->v18_gpio_active);
			msleep(10);
		}
	} else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180) {
		ambarella_sd_clear_clken(mmc);
		msleep(CONFIG_SD_AMBARELLA_VSW_PRE_SPEC);
		amba_writeb(pinfo->regbase + SD_PWR_OFFSET,
			(SD_PWR_ON | SD_PWR_1_8V));

		if (gpio_is_valid(pslotinfo->v18_gpio)) {
			gpio_set_value_cansleep(pslotinfo->v18_gpio,
						pslotinfo->v18_gpio_active);
			msleep(pinfo->switch_voltage_tmo);
		}
		ambarella_sd_set_clken(mmc);
	}
	ambarella_sd_release_bus(mmc);

	return retval;
}

static int ambarella_sd_card_busy(struct mmc_host *mmc)
{
	int retval = 0;
	struct ambarella_sd_mmc_info *pslotinfo = mmc_priv(mmc);
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	u32 sta_reg;

	ambarella_sd_request_bus(mmc);
	sta_reg = amba_readl(pinfo->regbase + SD_STA_OFFSET);
	ambsd_dbg(pslotinfo, "SD_STA_OFFSET = 0x%08x.\n", sta_reg);
	retval = !(sta_reg & 0x1F00000);
	ambarella_sd_release_bus(mmc);

	return retval;
}

static const struct mmc_host_ops ambarella_sd_host_ops = {
	.request = ambarella_sd_request,
	.set_ios = ambarella_sd_ios,
	.get_ro = ambarella_sd_get_ro,
	.get_cd = ambarella_sd_get_cd,
	.enable_sdio_irq = ambarella_sd_enable_sdio_irq,
	.start_signal_voltage_switch = ambarella_sd_ssvs,
	.card_busy = ambarella_sd_card_busy,
};

static int pre_notified[3] = {0};
static int sd_suspended = 0;

static int ambarella_sd_system_event(struct notifier_block *nb,
	unsigned long val, void *data)
{
	struct ambarella_sd_mmc_info *pslotinfo;
	struct ambarella_sd_controller_info *pinfo;

	pslotinfo = container_of(nb, struct ambarella_sd_mmc_info, system_event);
	pinfo = (struct ambarella_sd_controller_info *)pslotinfo->pinfo;

	switch (val) {
	case AMBA_EVENT_PRE_CPUFREQ:
		if (!sd_suspended) {
			pr_debug("%s[%u]: Pre Change\n", __func__, pslotinfo->slot_id);
			down(&pslotinfo->system_event_sem);
                        if ((u32) pinfo->regbase == SD0_BASE) {
                                pre_notified[0] = 1;
                        } else if ((u32) pinfo->regbase == SD1_BASE) {
                                pre_notified[1] = 1;
                        } else {
                                pre_notified[2] = 1;
                        }
		}
		break;

	case AMBA_EVENT_POST_CPUFREQ:
                if (((u32) pinfo->regbase == SD0_BASE) && pre_notified[0]) {
                        pr_debug("%s[%u]: Post Change\n", __func__, (u32) pinfo->regbase);
                        pre_notified[0] = 0;
                        ambarella_sd_set_clk(pslotinfo->mmc, &pinfo->controller_ios);
                        up(&pslotinfo->system_event_sem);
                } else if (((u32) pinfo->regbase == SD1_BASE) && pre_notified[1]) {
                        pr_debug("%s[%u]: Post Change\n", __func__, (u32) pinfo->regbase);
                        pre_notified[1] = 0;
                        ambarella_sd_set_clk(pslotinfo->mmc, &pinfo->controller_ios);
                        up(&pslotinfo->system_event_sem);
                } else if (pre_notified[2]) {
                        pr_debug("%s[%u]: Post Change\n", __func__, (u32) pinfo->regbase);
                        pre_notified[2] = 0;
                        ambarella_sd_set_clk(pslotinfo->mmc, &pinfo->controller_ios);
                        up(&pslotinfo->system_event_sem);
                }
		break;

	default:
		break;
	}

	return NOTIFY_OK;
}

/* ==========================================================================*/
static int ambarella_sd_init_slot(struct device_node *np, int id,
			struct ambarella_sd_controller_info *pinfo)
{
	struct device_node *save_np;
	struct ambarella_sd_mmc_info *pslotinfo = NULL;
	struct mmc_host *mmc;
	enum of_gpio_flags flags;
	u32 gpio_init_flag, hc_cap, hc_timeout_clk;
	int global_id, retval = 0;
#if defined(CONFIG_RPMSG_SD)
	struct rpdev_sdinfo *sdinfo;
#endif

	mmc = mmc_alloc_host(sizeof(*mmc), pinfo->dev);
	if (!mmc) {
		dev_err(pinfo->dev, "Failed to alloc mmc host %u!\n", id);
		return -ENOMEM;
	}
	pslotinfo = mmc_priv(mmc);
	pslotinfo->mmc = mmc;

	/* in order to use mmc_of_parse(), we reassign the parent
	 * of_node, this should be a workaroud. */
	save_np = mmc->parent->of_node;
	mmc->parent->of_node = np;
	mmc_of_parse(mmc);
	mmc->parent->of_node = save_np;

	/* Check official property and our own extra property */
	if (of_get_property(np, "non-removable", NULL)) {
		pslotinfo->fixed_wp = SD_PIN_FIXED;
		pslotinfo->fixed_cd = SD_PIN_FIXED;
	} else {
		if (of_property_read_u32(np, "amb,fixed-wp", &pslotinfo->fixed_wp) < 0)
			pslotinfo->fixed_wp = SD_PIN_BYPIN;
		if (of_property_read_u32(np, "amb,fixed-cd", &pslotinfo->fixed_cd) < 0)
			pslotinfo->fixed_cd = SD_PIN_BYPIN;
	}
	pslotinfo->no_1_8_v = !!of_find_property(np, "no-1-8-v", NULL);
	pslotinfo->caps_ddr = !!of_find_property(np, "amb,caps-ddr", NULL);
	pslotinfo->caps_adma = !!of_find_property(np, "amb,caps-adma", NULL);
	pslotinfo->force_gpio = !!of_find_property(np, "amb,force-gpio", NULL);
	if (pslotinfo->force_gpio) {
		pslotinfo->pinctrl = devm_pinctrl_get(pinfo->dev);
		if (IS_ERR(pslotinfo->pinctrl)) {
			retval = PTR_ERR(pslotinfo->pinctrl);
			dev_err(pinfo->dev, "Can't get pinctrl: %d\n", retval);
			goto init_slot_err1;
		}

		pslotinfo->state_work =
			pinctrl_lookup_state(pslotinfo->pinctrl, "work");
		if (IS_ERR(pslotinfo->state_work)) {
			retval = PTR_ERR(pslotinfo->state_work);
			dev_err(pinfo->dev, "Can't get pinctrl state: work\n");
			goto init_slot_err1;
		}

		pslotinfo->state_idle =
			pinctrl_lookup_state(pslotinfo->pinctrl, "idle");
		if (IS_ERR(pslotinfo->state_idle)) {
			retval = PTR_ERR(pslotinfo->state_idle);
			dev_err(pinfo->dev, "Can't get pinctrl state: idle\n");
			goto init_slot_err1;
		}
	}

	/* request gpio for external power control */
	pslotinfo->pwr_gpio = of_get_named_gpio_flags(np, "pwr-gpios", 0, &flags);
	pslotinfo->pwr_gpio_active = !!(flags & OF_GPIO_ACTIVE_LOW);

	if (gpio_is_valid(pslotinfo->pwr_gpio)) {
		if (pslotinfo->pwr_gpio_active)
			gpio_init_flag = GPIOF_OUT_INIT_LOW;
		else
			gpio_init_flag = GPIOF_OUT_INIT_HIGH;

		retval = devm_gpio_request_one(pinfo->dev, pslotinfo->pwr_gpio,
					gpio_init_flag, "sd ext power");
		if (retval < 0) {
			dev_err(pinfo->dev, "Failed to request pwr-gpios!\n");
			goto init_slot_err1;
		}
	}

	/* request gpio for 3.3v/1.8v switch */
	pslotinfo->v18_gpio = of_get_named_gpio_flags(np, "v18-gpios", 0, &flags);
	pslotinfo->v18_gpio_active = !!(flags & OF_GPIO_ACTIVE_LOW);

	if (gpio_is_valid(pslotinfo->v18_gpio)) {
		if (pslotinfo->v18_gpio_active)
			gpio_init_flag = GPIOF_OUT_INIT_LOW;
		else
			gpio_init_flag = GPIOF_OUT_INIT_HIGH;

		retval = devm_gpio_request_one(pinfo->dev, pslotinfo->v18_gpio,
				gpio_init_flag, "sd ext power");
		if (retval < 0) {
			dev_err(pinfo->dev, "Failed to request v18-gpios!\n");
			goto init_slot_err1;
		}
	}

	clk_set_rate(pinfo->clk, mmc->f_max);
	mmc->f_max = clk_get_rate(pinfo->clk);
	mmc->f_min = mmc->f_max >> 8;

	mmc->ops = &ambarella_sd_host_ops;

	init_waitqueue_head(&pslotinfo->wait);
	pslotinfo->state = AMBA_SD_STATE_ERR;
	pslotinfo->slot_id = id;
	pslotinfo->pinfo = pinfo;
	pinfo->pslotinfo[id] = pslotinfo;
	sema_init(&pslotinfo->system_event_sem, 1);

	ambarella_sd_request_bus(mmc);

	ambarella_sd_reset_all(mmc);

	hc_cap = amba_readl(pinfo->regbase + SD_CAP_OFFSET);

	dev_dbg(pinfo->dev,
		"SD Clock: base[%uMHz], min[%uHz], max[%uHz].\n",
		SD_CAP_BASE_FREQ(hc_cap), mmc->f_min, mmc->f_max);

	hc_timeout_clk = mmc->f_max / 1000;
	if (hc_timeout_clk == 0)
		hc_timeout_clk = 24000;

	mmc->max_discard_to = (1 << 27) / hc_timeout_clk;

	if (hc_cap & SD_CAP_MAX_2KB_BLK)
		mmc->max_blk_size = 2048;
	else if (hc_cap & SD_CAP_MAX_1KB_BLK)
		mmc->max_blk_size = 1024;
	else if (hc_cap & SD_CAP_MAX_512B_BLK)
		mmc->max_blk_size = 512;

	dev_dbg(pinfo->dev, "SD max_blk_size: %u.\n", mmc->max_blk_size);

	mmc->caps |= MMC_CAP_4_BIT_DATA | MMC_CAP_SDIO_IRQ |
			MMC_CAP_ERASE | MMC_CAP_BUS_WIDTH_TEST;

	if (mmc->f_max > 25000000) {
		mmc->caps |= MMC_CAP_SD_HIGHSPEED;
		mmc->caps |= MMC_CAP_MMC_HIGHSPEED;
	}

	if (!(hc_cap & SD_CAP_DMA)) {
		ambsd_err(pslotinfo, "HW do not support DMA!\n");
		retval = -ENODEV;
		goto init_slot_err1;
	}

	if (hc_cap & SD_CAP_ADMA_SUPPORT) {
		dev_dbg(pinfo->dev, "HW support ADMA!\n");
		pslotinfo->use_adma = pslotinfo->caps_adma ? 1 : 0;
	}

	dev_dbg(pinfo->dev, "HW%s support Suspend/Resume!\n",
			(hc_cap & SD_CAP_SUS_RES) ? "" : " do not");

	mmc->ocr_avail = 0;
	if (hc_cap & SD_CAP_VOL_3_3V)
		mmc->ocr_avail |= MMC_VDD_32_33 | MMC_VDD_33_34;

	if ((hc_cap & SD_CAP_VOL_1_8V) && !pslotinfo->no_1_8_v) {
		mmc->ocr_avail |= MMC_VDD_165_195;
		if (hc_cap & SD_CAP_HIGH_SPEED)
			mmc->caps |= MMC_CAP_1_8V_DDR;

		mmc->caps |= MMC_CAP_UHS_SDR12;
		if (mmc->f_max > 25000000) {
			mmc->caps |= MMC_CAP_UHS_SDR25;
			if (pslotinfo->caps_ddr)
				mmc->caps |= MMC_CAP_UHS_DDR50;
#if defined(CONFIG_RPMSG_SD) && !defined(CONFIG_PLAT_AMBARELLA_BOSS)
			disable_irq(pslotinfo->plat_info->gpio_cd.irq_line);
#endif
		}

		if (mmc->f_max > 50000000)
			mmc->caps |= MMC_CAP_UHS_SDR50;

		if (mmc->f_max > 100000000)
			mmc->caps |= MMC_CAP_UHS_SDR104;
	}

	if (mmc->ocr_avail == 0) {
		ambsd_err(pslotinfo, "HW report wrong voltages[0x%x]!\n", hc_cap);
		retval = -ENODEV;
		goto init_slot_err1;
	}

	if (!(hc_cap & SD_CAP_INTMODE)) {
		ambsd_err(pslotinfo, "HW do not support Interrupt mode!\n");
		retval = -ENODEV;
		goto init_slot_err1;
	}

	dev_dbg(pinfo->dev, "SD caps: 0x%x.\n", mmc->caps);
	dev_dbg(pinfo->dev, "SD ocr: 0x%x.\n", mmc->ocr_avail);

	mmc->max_blk_count = 0xFFFF;
	mmc->max_seg_size = pinfo->max_blk_sz;
	mmc->max_segs = mmc->max_seg_size / PAGE_SIZE;
	mmc->max_req_size =
		min(mmc->max_seg_size, mmc->max_blk_size * mmc->max_blk_count);

	pslotinfo->buf_vaddress = kmem_cache_alloc(pinfo->buf_cache, GFP_KERNEL);
	if (!pslotinfo->buf_vaddress) {
		ambsd_err(pslotinfo, "Can't alloc DMA memory");
		retval = -ENOMEM;
		goto init_slot_err1;
	}
	pslotinfo->buf_paddress = dma_map_single(pinfo->dev,
					pslotinfo->buf_vaddress,
					mmc->max_req_size,
					DMA_BIDIRECTIONAL);
	ambarella_sd_check_dma_boundary(pslotinfo->buf_paddress,
				mmc->max_req_size, pinfo->max_blk_sz);

	dev_notice(pinfo->dev, "Slot%u use bounce buffer[0x%p<->0x%08x]\n",
		pslotinfo->slot_id, pslotinfo->buf_vaddress,
		pslotinfo->buf_paddress);

	dev_notice(pinfo->dev, "Slot%u req_size=0x%08x, "
		"segs=%u, seg_size=0x%08x\n",
		pslotinfo->slot_id, mmc->max_req_size,
		mmc->max_segs, mmc->max_seg_size);

	dev_notice(pinfo->dev, "Slot%u use %sDMA\n", pslotinfo->slot_id,
		pslotinfo->use_adma ? "A" :"");

#if defined(CONFIG_RPMSG_SD)
	ambarella_sd_rpmsg_sdinfo_init(mmc);
	sdinfo = ambarella_sd_sdinfo_get(mmc);
	ambarella_sd_rpmsg_sdinfo_en(mmc, sdinfo->is_init);

	/* Set clock back to RTOS desired. */
	if (sdinfo->is_init) {
		clk_set_rate(pinfo->clk, sdinfo->clk);
	}
#endif

	mmc->pm_caps |= MMC_PM_KEEP_POWER | MMC_PM_WAKE_SDIO_IRQ;

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	boss_set_irq_owner(pinfo->irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
#if defined(CONFIG_RPMSG_SD)
	INIT_DELAYED_WORK(&pslotinfo->detect, ambarella_sd_cd_detect);
	disable_irq(pinfo->irq);
	enable_irq(pinfo->irq);
#endif

	ambarella_sd_release_bus(mmc);

	retval = mmc_add_host(pslotinfo->mmc);
	if (retval < 0) {
		ambsd_err(pslotinfo, "Can't add mmc host!\n");
		goto init_slot_err2;
	}

	pslotinfo->system_event.notifier_call = ambarella_sd_system_event;
	ambarella_register_event_notifier(&pslotinfo->system_event);

	retval = of_property_read_u32(np, "global-id", &global_id);
	if (retval < 0) {
		global_id = ((u32) pinfo->regbase == SD0_BASE) ? SD_HOST_0 :
		 ((u32) pinfo->regbase == SD1_BASE) ? SD_HOST_1 : SD_HOST_2;
	}

	G_mmc[global_id] = mmc;
	if (pslotinfo->fixed_cd != SD_PIN_BYPIN) {
		ambsd_dbg(pslotinfo, "Fixed CD\n");
		mmc_detect_change(pslotinfo->mmc, msecs_to_jiffies(1000));
	}

	return 0;

init_slot_err2:
	dma_unmap_single(pinfo->dev, pslotinfo->buf_paddress,
			pslotinfo->mmc->max_req_size, DMA_BIDIRECTIONAL);
	kmem_cache_free(pinfo->buf_cache, pslotinfo->buf_vaddress);

init_slot_err1:
	mmc_free_host(mmc);
	return retval;
}

static int ambarella_sd_free_slot(struct ambarella_sd_mmc_info *pslotinfo)
{
	struct ambarella_sd_controller_info *pinfo = pslotinfo->pinfo;
	int retval = 0;

	ambarella_unregister_event_notifier(&pslotinfo->system_event);

	mmc_remove_host(pslotinfo->mmc);

	if (pslotinfo->buf_paddress) {
		dma_unmap_single(pinfo->dev, pslotinfo->buf_paddress,
			pslotinfo->mmc->max_req_size, DMA_BIDIRECTIONAL);
		pslotinfo->buf_paddress = (dma_addr_t)NULL;
	}

	if (pslotinfo->buf_vaddress) {
		kmem_cache_free(pinfo->buf_cache, pslotinfo->buf_vaddress);
		pslotinfo->buf_vaddress = NULL;
	}

	mmc_free_host(pslotinfo->mmc);

	return retval;
}

static int ambarella_sd_of_parse(struct ambarella_sd_controller_info *pinfo)
{
	struct device_node *np = pinfo->dev->of_node;
	const __be32 *prop;
	const char *clk_name;
	int psize, tmo, switch_vol_tmo, retval = 0;

	retval = of_property_read_string(np, "amb,clk-name", &clk_name);
	if (retval < 0) {
		dev_err(pinfo->dev, "Get pll-name failed! %d\n", retval);
		goto pasre_err;
	}

	pinfo->clk = clk_get(NULL, clk_name);
	if (IS_ERR(pinfo->clk)) {
		dev_err(pinfo->dev, "Get PLL failed!\n");
		retval = PTR_ERR(pinfo->clk);
		goto pasre_err;
	}

	if (of_find_property(np, "amb,dma-addr-fix", NULL))
		pinfo->dma_fix = 0xc0000000;

	retval = of_property_read_u32(np, "amb,wait-tmo", &tmo);
	if (retval < 0)
		tmo = 0x10000;

	pinfo->default_wait_tmo = msecs_to_jiffies(tmo);

	if (pinfo->default_wait_tmo < CONFIG_SD_AMBARELLA_WAIT_TIMEOUT)
		pinfo->default_wait_tmo = CONFIG_SD_AMBARELLA_WAIT_TIMEOUT;

	retval = of_property_read_u32(np, "amb,switch-vol-tmo", &switch_vol_tmo);
	if (retval < 0)
		switch_vol_tmo = 100;

	pinfo->switch_voltage_tmo = switch_vol_tmo;

	retval = of_property_read_u32(np, "amb,max-blk-size", &pinfo->max_blk_sz);
	if (retval < 0)
		pinfo->max_blk_sz = 0x20000;

	/*Adjust SD Timing base on SD Phy*/
	pinfo->soft_phy = !!of_find_property(np, "amb,soft-phy", NULL);

	/* below are properties for phy timing */
	if (!pinfo->soft_phy && pinfo->timing_reg == NULL) {
		retval = 0;
		goto pasre_err;
	}

	/* amb,phy-timing must be provided when timing_reg is given */
	prop = of_get_property(np, "amb,phy-timing", &psize);
	if (!prop) {
		retval = -EINVAL;
		goto pasre_err;
	}

	psize /= sizeof(u32);
	BUG_ON(psize % 3);
	pinfo->phy_timing_num = psize / 3;
	pinfo->phy_timing = devm_kzalloc(pinfo->dev, psize, GFP_KERNEL);
	if (pinfo->phy_timing == NULL) {
		retval = -ENOMEM;
		goto pasre_err;
	}

	retval = of_property_read_u32_array(np, "amb,phy-timing",
			(u32 *)pinfo->phy_timing, psize);

	/* bit0 of mode must be set, and the phy setting in first row with
	 * bit0 set is the default one. */
	BUG_ON(!(pinfo->phy_timing[0].mode & 0x1));

pasre_err:
	return retval;
}

static int ambarella_sd_get_resource(struct platform_device *pdev,
			struct ambarella_sd_controller_info *pinfo)
{
	struct resource *mem;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (mem == NULL) {
		dev_err(&pdev->dev, "Get SD/MMC mem resource failed!\n");
		return -ENXIO;
	}

	pinfo->regbase = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
	if (pinfo->regbase == NULL) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return -ENOMEM;
	}

#if defined(CONFIG_RPMSG_SD)
	pinfo->id = (u32) pinfo->regbase;
#endif

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (mem == NULL) {
		dev_err(&pdev->dev, "Get FIO mem resource failed!\n");
		return -ENXIO;
	}

	pinfo->fio_reg = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
	if (pinfo->fio_reg == NULL) {
		dev_err(&pdev->dev, "devm_ioremap() failed\n");
		return -ENOMEM;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 2);
	if (mem == NULL) {
		pinfo->timing_reg = NULL;
	} else {
		pinfo->timing_reg = devm_ioremap(&pdev->dev,
					mem->start, resource_size(mem));
		if (pinfo->timing_reg == NULL) {
			dev_err(&pdev->dev, "devm_ioremap() failed\n");
			return -ENOMEM;
		}
	}

	pinfo->irq = platform_get_irq(pdev, 0);
	if (pinfo->irq < 0) {
		dev_err(&pdev->dev, "Get SD/MMC irq resource failed!\n");
		return -ENXIO;
	}

	return 0;
}

static int ambarella_sd_probe(struct platform_device *pdev)
{
	struct ambarella_sd_controller_info *pinfo;
	struct device_node *slot_np;
	int retval, i, slot_id = -1;

	pinfo = devm_kzalloc(&pdev->dev, sizeof(*pinfo), GFP_KERNEL);
	if (pinfo == NULL) {
		dev_err(&pdev->dev, "Out of memory!\n");
		return -ENOMEM;
	}
	pinfo->dev = &pdev->dev;

	retval = ambarella_sd_get_resource(pdev, pinfo);
	if (retval < 0)
		return retval;

	retval = ambarella_sd_of_parse(pinfo);
	if (retval < 0)
		return retval;

	pinfo->buf_cache = kmem_cache_create(dev_name(&pdev->dev),
				pinfo->max_blk_sz, pinfo->max_blk_sz, 0, NULL);
	if (!pinfo->buf_cache) {
		dev_err(&pdev->dev, "Can't alloc DMA Cache");
		return -ENOMEM;
	}

	pinfo->slot_num = 0;
	for_each_child_of_node(pdev->dev.of_node, slot_np) {
		retval = of_property_read_u32(slot_np, "reg", &slot_id);
		if (retval < 0 || slot_id >= SD_INSTANCES)
			goto ambarella_sd_probe_free_host;

		retval = ambarella_sd_init_slot(slot_np, slot_id, pinfo);
		if (retval < 0)
			goto ambarella_sd_probe_free_host;

		pinfo->slot_num++;
	}

	retval = devm_request_irq(&pdev->dev, pinfo->irq, ambarella_sd_irq,
				IRQF_SHARED | IRQF_TRIGGER_HIGH,
				dev_name(&pdev->dev), pinfo);
	if (retval < 0) {
		dev_err(&pdev->dev, "Can't Request IRQ%u!\n", pinfo->irq);
		goto ambarella_sd_probe_free_host;
	}

	platform_set_drvdata(pdev, pinfo);
	dev_info(&pdev->dev, "%u slots @ %luHz\n",
			pinfo->slot_num, clk_get_rate(pinfo->clk));

	return 0;

ambarella_sd_probe_free_host:
	for (i = pinfo->slot_num - 1; i >= 0; i--)
		ambarella_sd_free_slot(pinfo->pslotinfo[i]);

	if (pinfo->buf_cache) {
		kmem_cache_destroy(pinfo->buf_cache);
		pinfo->buf_cache = NULL;
	}

	return retval;
}

static int ambarella_sd_remove(struct platform_device *pdev)
{
	struct ambarella_sd_controller_info *pinfo;
	int retval = 0, i;

	pinfo = platform_get_drvdata(pdev);

	for (i = 0; i < pinfo->slot_num; i++)
		ambarella_sd_free_slot(pinfo->pslotinfo[i]);

	if (pinfo->buf_cache)
		kmem_cache_destroy(pinfo->buf_cache);

	dev_notice(&pdev->dev,
		"Remove Ambarella Media Processor SD/MMC Host Controller.\n");

	return retval;
}

#ifdef CONFIG_PM
extern int wowlan_resume_from_ram;
static u32 sd_nisen;
static u32 sd_eisen;
static u32 sd_nixen;
static u32 sd_eixen;

static int ambarella_sd_suspend(struct platform_device *pdev,
	pm_message_t state)
{
	int retval = 0;

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	struct ambarella_sd_controller_info *pinfo;
	struct ambarella_sd_mmc_info *pslotinfo;
	int i;

	sd_suspended = 1;

	pinfo = platform_get_drvdata(pdev);

	if (0 == wowlan_resume_from_ram) {
		for (i = 0; i < pinfo->slot_num; i++) {
			pslotinfo = pinfo->pslotinfo[i];
			if (pslotinfo->mmc) {
				pslotinfo->state = AMBA_SD_STATE_RESET;
				retval = mmc_suspend_host(pslotinfo->mmc);
				if (retval) {
					ambsd_err(pslotinfo,
					"mmc_suspend_host[%d] failed[%d]!\n",
					i, retval);
				}
				down(&pslotinfo->system_event_sem);
			}
		}

		disable_irq(pinfo->irq);
		for (i = 0; i < pinfo->slot_num; i++) {
			pslotinfo = pinfo->pslotinfo[i];
#if 0
			if (ambarella_is_valid_gpio_irq(&pslotinfo->plat_info->gpio_cd))
				disable_irq(pslotinfo->plat_info->gpio_cd.irq_line);
#endif
		}

		dev_dbg(&pdev->dev, "%s exit with %d @ %d\n",
			__func__, retval, state.event);
	} else {
		if (SD2_IRQ == (pinfo->irq)) {
			pslotinfo = pinfo->pslotinfo[0];

			pr_debug("%s[%u]: Pre Change\n", __func__, pslotinfo->slot_id);
			down(&pslotinfo->system_event_sem);

			/* for SR WOWLAN, disable sdio irq and backup sd registers */
			ambarella_sd_enable_sdio_irq(pslotinfo->mmc, 0);

			sd_nisen = amba_readw(pinfo->regbase + SD_NISEN_OFFSET);
			sd_eisen = amba_readw(pinfo->regbase + SD_EISEN_OFFSET);
			sd_nixen = amba_readw(pinfo->regbase + SD_NIXEN_OFFSET);
			sd_eixen = amba_readw(pinfo->regbase + SD_EIXEN_OFFSET);
		}
	}
#endif
	return retval;
}

static int ambarella_sd_resume(struct platform_device *pdev)
{
	int retval = 0;

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	struct ambarella_sd_controller_info *pinfo;
	struct ambarella_sd_mmc_info *pslotinfo;
	int i;

	pinfo = platform_get_drvdata(pdev);
        pslotinfo = pinfo->pslotinfo[0];

	if (0 == wowlan_resume_from_ram) {
		for (i = 0; i < pinfo->slot_num; i++) {
			pslotinfo = pinfo->pslotinfo[i];
                        up(&pslotinfo->system_event_sem);
                        ambarella_sd_request_bus(pslotinfo->mmc);
			clk_set_rate(pinfo->clk, pslotinfo->mmc->f_max);
			ambarella_sd_reset_all(pslotinfo->mmc);
                        ambarella_sd_release_bus(pslotinfo->mmc);
#if 0
			if (ambarella_is_valid_gpio_irq(&pslotinfo->plat_info->gpio_cd))
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
				boss_set_irq_owner(pslotinfo->plat_info->gpio_cd.irq_line,
						   BOSS_IRQ_OWNER_LINUX, 1);
#endif
				enable_irq(pslotinfo->plat_info->gpio_cd.irq_line);
#endif
		}

                ambarella_sd_request_bus(pslotinfo->mmc);
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
		boss_set_irq_owner(pinfo->irq, BOSS_IRQ_OWNER_LINUX, 1);
#endif
		enable_irq(pinfo->irq);
                ambarella_sd_release_bus(pslotinfo->mmc);

		for (i = 0; i < pinfo->slot_num; i++) {
			pslotinfo = pinfo->pslotinfo[i];
			if (pslotinfo->mmc) {
				retval = mmc_resume_host(pslotinfo->mmc);
				if (retval) {
					ambsd_err(pslotinfo,
					"mmc_resume_host[%d] failed[%d]!\n",
					i, retval);
				}
			}
		}

		dev_dbg(&pdev->dev, "%s exit with %d\n", __func__, retval);
	} else {
                if (SD2_IRQ == (pinfo->irq)) {
                        //struct irq_desc *desc;

                        /* for SR WOWLAN, restore vic, registers, clock, enable irq  */
                        //desc = irq_to_desc(pinfo->irq);
                        //desc->irq_data.chip->irq_set_type(&desc->irq_data, IRQF_TRIGGER_HIGH);

                        amba_writew(pinfo->regbase + SD_NISEN_OFFSET, sd_nisen);
                        amba_writew(pinfo->regbase + SD_EISEN_OFFSET, sd_eisen);
                        amba_writew(pinfo->regbase + SD_NIXEN_OFFSET, sd_nixen);
                        amba_writew(pinfo->regbase + SD_EIXEN_OFFSET, sd_eixen);

                        pslotinfo = pinfo->pslotinfo[0];
                        mdelay(10);
                        ambarella_sd_set_clk(pslotinfo->mmc, &pinfo->controller_ios);
			ambarella_sd_set_bus(pslotinfo->mmc, &pinfo->controller_ios);
                        mdelay(10);

                        /* prevent first cmd err */
                        //ambarella_sd_reset_all(pslotinfo->mmc);

                        pr_debug("%s[%u]: Post Change\n", __func__, pslotinfo->slot_id);
                        up(&pslotinfo->system_event_sem);

                        printk("ambarella_sd_enable_sdio_irq 1\n");
                        ambarella_sd_enable_sdio_irq(pslotinfo->mmc, 1);
                }
        }
#endif
	sd_suspended = 0;

	return retval;
}
#endif

static const struct of_device_id ambarella_mmc_dt_ids[] = {
	{ .compatible = "ambarella,sdmmc", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ambarella_mmc_dt_ids);

static struct platform_driver ambarella_sd_driver = {
	.probe		= ambarella_sd_probe,
	.remove		= ambarella_sd_remove,
#ifdef CONFIG_PM
	.suspend	= ambarella_sd_suspend,
	.resume		= ambarella_sd_resume,
#endif
	.driver		= {
		.name	= "ambarella-sd",
		.owner	= THIS_MODULE,
		.of_match_table = ambarella_mmc_dt_ids,
	},
};

static int mmc_fixed_cd_proc_read(struct seq_file *m, void *v)
{
	int mmci;
	int retlen = 0;
	struct mmc_host *mmc;
	struct ambarella_sd_mmc_info *pslotinfo;

	for(mmci = 0; mmci < SD_INSTANCES; mmci++) {
		if (G_mmc[mmci]) {
			mmc = G_mmc[mmci];
			pslotinfo = mmc_priv(mmc);
			retlen += seq_printf(m, "mmc%d fixed_cd=%d\n", mmci,
					pslotinfo->fixed_cd);
		}
	}

	return retlen;
}

static int mmc_fixed_cd_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, mmc_fixed_cd_proc_read, NULL);
}

static ssize_t mmc_fixed_cd_proc_write(struct file *file,
                                const char __user *buffer, size_t count, loff_t *data)
{
	char input[4];
	int mmci;
	struct mmc_host *mmc;
	struct ambarella_sd_mmc_info *pslotinfo;

	if (count != 4) {
		printk(KERN_ERR "0 0=remove mmc0; 0 1=insert mmc0; 0 2=mmc0 auto\n");
		return -EFAULT;
	}

	if (copy_from_user(input, buffer, 3)) {
		printk(KERN_ERR "%s: copy_from_user fail!\n", __func__);
		return -EFAULT;
	}

	mmci = input[0] - '0';
	mmc = G_mmc[mmci];
	if (!mmc) {
		printk(KERN_ERR "%s: err!\n", __func__);
		return -EFAULT;
	}

	pslotinfo = mmc_priv(mmc);

	pslotinfo->fixed_cd = input[2] - '0';
	mmc_detect_change(mmc, 10);

	return count;
}

static const struct file_operations proc_fops_mmc_fixed_cd = {
	.open = mmc_fixed_cd_proc_open,
	.read = seq_read,
	.write = mmc_fixed_cd_proc_write,
	.llseek	= seq_lseek,
	.release = single_release,
};

static int __init ambarella_sd_init(void)
{
	int retval = 0;

#if defined(CONFIG_RPMSG_SD)
	memset(G_rpdev_sdinfo, 0x0, sizeof(G_rpdev_sdinfo));
#endif

	retval = platform_driver_register(&ambarella_sd_driver);
	if (retval) {
		printk(KERN_ERR "%s: Register failed %d!\n",
			__func__, retval);
	}

	/* to implement the function of pre-3.10 /sys/module/ambarella_config/parameters/sd1_slot0_fixed_cd */
	proc_create("mmc_fixed_cd", S_IRUGO | S_IWUSR, get_ambarella_proc_dir(),
		&proc_fops_mmc_fixed_cd);

	proc_create("sdio_info", S_IRUGO | S_IWUSR, get_ambarella_proc_dir(),
		&proc_fops_sdio_info);

	return retval;
}

static void __exit ambarella_sd_exit(void)
{
	platform_driver_unregister(&ambarella_sd_driver);
}

fs_initcall_sync(ambarella_sd_init);
module_exit(ambarella_sd_exit);

MODULE_DESCRIPTION("Ambarella Media Processor SD/MMC Host Controller");
MODULE_AUTHOR("Anthony Ginger, <hfjiang@ambarella.com>");
MODULE_LICENSE("GPL");

