/*
 * amba_dspmem.c
 *
 * History:
 *	2012/04/07 - [Keny Huang] created file
 *
 * Copyright (C) 2007-2012, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 *
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/uaccess.h>
#include <misc/amba_dspmem.h>
#include <plat/ambalink_cfg.h>

extern int rpmsg_linkctrl_cmd_get_mem_info(u8 type, void **base, void **phys, u32 *size);

static void *dsp_baseaddr = NULL;
static void *dsp_physaddr = NULL;
static unsigned int dsp_size = 0;

struct amba_dspmem_dev {
	struct miscdevice *misc_dev;
};

static struct amba_dspmem_dev amba_dmem_dev = {NULL};
static DEFINE_MUTEX(amba_dspmem_mutex);

static long amba_dspmem_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	long ret = 0;
	struct AMBA_DSPMEM_INFO_s minfo;

	//printk("%s\n",__func__);

	mutex_lock(&amba_dspmem_mutex);
	switch (cmd) {
	case AMBA_DSPMEM_GET_INFO:
		if(rpmsg_linkctrl_cmd_get_mem_info(1, &dsp_baseaddr, &dsp_physaddr, &dsp_size)<0){
			printk("rpmsg_linkctrl_cmd_get_mem_info() fail\n");
			ret = -EINVAL;
			break;
		}
		minfo.base = (unsigned int)dsp_baseaddr;
		minfo.phys = (unsigned int)dsp_physaddr;
		minfo.size = dsp_size;

		if(copy_to_user((void **)arg, &minfo, sizeof(struct AMBA_DSPMEM_INFO_s))){
			ret = -EFAULT;
		}
		break;
	default:
		printk("%s: unknown command 0x%08x", __func__, cmd);
		ret = -EINVAL;
		break;
	}
	mutex_unlock(&amba_dspmem_mutex);

	return ret;
}

#define pgprot_noncached(prot) \
       __pgprot_modify(prot, L_PTE_MT_MASK, L_PTE_MT_UNCACHED)

static pgprot_t phys_mem_access_prot(struct file *file, unsigned long pfn,
				     unsigned long size, pgprot_t vma_prot)
{
	/* Do not need to set as noncached for one CPU! */
#if !defined(CONFIG_AMBALINK_SINGLE_CORE)
	if (file->f_flags & O_DSYNC){
		printk("phys_mem_access_prot: set as noncached\n");
		return pgprot_noncached(vma_prot);
	}
#endif

	return vma_prot;
}

static int amba_dspmem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int rval;
	unsigned long size;
	u32 baseaddr;

	mutex_lock(&amba_dspmem_mutex);
	if(rpmsg_linkctrl_cmd_get_mem_info(1, &dsp_baseaddr, &dsp_physaddr, &dsp_size)<0){
		printk("rpmsg_linkctrl_cmd_get_mem_info() fail\n");
		rval = -EINVAL;
		goto Done;
	}

	size = vma->vm_end - vma->vm_start;
	if(size==dsp_size) {
		//printk("%s: dsp_baseaddr=%p, dsp_physaddr=%p, dsp_size=%x\n",
		//	__func__, dsp_baseaddr, dsp_physaddr, dsp_size);

		/* For MMAP, it needs to use physical address directly! */
		//baseaddr=(int)ambalink_phys_to_virt((u32)dsp_physaddr);
		baseaddr=(u32)dsp_physaddr;
	} else {
		printk("%s: wrong size(%x)! dsp_size=%x\n",__func__, (unsigned int)size, dsp_size);
		rval = -EINVAL;
		goto Done;
	}

	//if(filp->f_flags & O_SYNC){
	//	vma->vm_flags |= VM_IO;
	//}
	//vma->vm_flags |= VM_RESERVED;

	vma->vm_page_prot = phys_mem_access_prot(filp, vma->vm_pgoff,
						 size,
						 vma->vm_page_prot);

	vma->vm_pgoff = (baseaddr) >> PAGE_SHIFT;
	if ((rval = remap_pfn_range(vma,
			vma->vm_start,
			vma->vm_pgoff,
			size,
			vma->vm_page_prot)) < 0) {
		goto Done;
	}

	rval = 0;

Done:
	mutex_unlock(&amba_dspmem_mutex);
	return rval;
}

static int amba_dspmem_open(struct inode *inode, struct file *filp)
{
//printk("%s\n",__func__);
	return 0;
}

static int amba_dspmem_release(struct inode *inode, struct file *filp)
{
//printk("%s\n",__func__);
	return 0;
}

static struct file_operations amba_dspmem_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = amba_dspmem_ioctl,
	.mmap = amba_dspmem_mmap,
	.open = amba_dspmem_open,
	.release = amba_dspmem_release,
};

static struct miscdevice amba_dspmem_device = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "amba_dspmem",
	.fops = &amba_dspmem_fops,
};

struct platform_device amba_dspmem = {
	.name			= "amba_dspmem",
	.id			= 0,
};
EXPORT_SYMBOL(amba_dspmem);

static int amba_dspmem_probe(struct platform_device *pdev)
{
	int err = 0;


	if(amba_dmem_dev.misc_dev!=NULL){
		dev_err(&pdev->dev, "amba_dspmem already exists. Skip operation!\n");
		return 0;
	}

	platform_set_drvdata(pdev, &amba_dmem_dev);

	amba_dmem_dev.misc_dev = &amba_dspmem_device;

	err = misc_register(amba_dmem_dev.misc_dev);
	if (err){
		dev_err(&pdev->dev, "failed to misc_register amba_dspmem.\n");
		goto err_fail;
	}

	printk("Probe %s successfully\n",amba_dmem_dev.misc_dev->name);

	return 0;

err_fail:
	misc_deregister(amba_dmem_dev.misc_dev);
	amba_dmem_dev.misc_dev=NULL;
	return err;
}


static int amba_dspmem_remove(struct platform_device *pdev)
{
	misc_deregister(amba_dmem_dev.misc_dev);
	amba_dmem_dev.misc_dev=NULL;

	return 0;
}

#ifdef CONFIG_PM
static int amba_dspmem_suspend(struct platform_device *pdev, pm_message_t state)
{
	int errorCode = 0;

	dev_dbg(&pdev->dev, "%s exit with %d @ %d\n",
		__func__, errorCode, state.event);

	return errorCode;
}

static int amba_dspmem_resume(struct platform_device *pdev)
{
	int errorCode = 0;

	dev_dbg(&pdev->dev, "%s exit with %d\n", __func__, errorCode);

	return errorCode;
}
#endif

static struct platform_driver amba_dspmem_driver = {
	.probe = amba_dspmem_probe,
	.remove = amba_dspmem_remove,
#ifdef CONFIG_PM
	.suspend        = amba_dspmem_suspend,
	.resume		= amba_dspmem_resume,
#endif
	.driver = {
		.name	= "amba_dspmem",
	},
};

static int __init __amba_dspmem_init(void)
{
	return platform_driver_register(&amba_dspmem_driver);
}

static void __exit __amba_dspmem_exit(void)
{
	platform_driver_unregister(&amba_dspmem_driver);
}

module_init(__amba_dspmem_init);
module_exit(__amba_dspmem_exit);

MODULE_AUTHOR("Keny Huang <skhuang@ambarella.com>");
MODULE_DESCRIPTION("Ambarella dspmem driver");
MODULE_LICENSE("GPL");
