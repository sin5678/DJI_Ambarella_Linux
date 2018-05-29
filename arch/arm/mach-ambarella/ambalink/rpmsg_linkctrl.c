/*
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 */

#include <linux/ioport.h>
#include <linux/proc_fs.h>
#include <asm/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/kthread.h>

#include <linux/remoteproc.h>
#include <mach/init.h>
#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>

/*----------------------------------------------------------------------------*/
typedef u32 UINT32;

typedef enum _AMBA_RPDEV_LINK_CTRL_CMD_e_ {
	LINK_CTRL_CMD_HIBER_PREPARE_FROM_LINUX = 0,
	LINK_CTRL_CMD_HIBER_ENTER_FROM_LINUX,
	LINK_CTRL_CMD_HIBER_EXIT_FROM_LINUX,
	LINK_CTRL_CMD_HIBER_ACK,
	LINK_CTRL_CMD_SUSPEND,
	LINK_CTRL_CMD_GPIO_LINUX_ONLY_LIST,
	LINK_CTRL_CMD_GET_MEM_INFO,
	LINK_CTRL_CMD_SET_RTOS_MEM
} AMBA_RPDEV_LINK_CTRL_CMD_e;

/*---------------------------------------------------------------------------*\
 * LinkCtrlSuspendLinux related data structure.
\*---------------------------------------------------------------------------*/
typedef enum _AMBA_RPDEV_LINK_CTRL_SUSPEND_TARGET_e_ {
    LINK_CTRL_CMD_SUSPEND_TO_DISK = 0,
    LINK_CTRL_CMD_SUSPEND_TO_DRAM
} AMBA_RPDEV_LINK_CTRL_SUSPEND_TARGET_e;

typedef struct _AMBA_RPDEV_LINK_CTRL_CMD_s_ {
	UINT32  Cmd;
	UINT32  Param1;
	UINT32  Param2;
} AMBA_RPDEV_LINK_CTRL_CMD_s;

typedef struct _LINK_CTRL_MEMINFO_CMD_s_ {
	UINT32	Cmd;
	UINT32	RtosStart;
	UINT32	RtosEnd;
	UINT32	RtosSystemStart;
	UINT32	RtosSystemEnd;
	UINT32	CachedHeapStart;
	UINT32	CachedHeapEnd;
	UINT32	NonCachedHeapStart;
	UINT32	NonCachedHeapEnd;
} LINK_CTRL_MEMINFO_CMD_s;

typedef enum _AMBA_RPDEV_LINK_CTRL_MEMTYPE_e_ {
	LINK_CTRL_MEMTYPE_HEAP = 0,
	LINK_CTRL_MEMTYPE_DSP
} AMBA_RPDEV_LINK_CTRL_MEMTYPE_e;

struct _AMBA_RPDEV_LINK_CTRL_MEMINFO_s_ {
        void *base_addr;
	void *phys_addr;
	UINT32 size;
	UINT32 padding;
	UINT32 padding1;
	UINT32 padding2;
	UINT32 padding3;
	UINT32 padding4;
} __attribute__((aligned(32), packed));
typedef struct _AMBA_RPDEV_LINK_CTRL_MEMINFO_s_ AMBA_RPDEV_LINK_CTRL_MEMINFO_t;

DECLARE_COMPLETION(linkctrl_comp);
struct rpmsg_channel *rpdev_linkctrl;
int hibernation_start = 0;
static AMBA_RPDEV_LINK_CTRL_MEMINFO_t rpdev_meminfo;

static struct resource amba_rtosmem = {
       .name   = "RTOS memory region",
       .start  = 0x0,
       .end    = 0x0,
       .flags  = IORESOURCE_MEM | IORESOURCE_BUSY,
};

/*
 * Standard memory resources
 */
static struct resource amba_heapmem[] = {
	{
		.name = "RTOS System",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_MEM | IORESOURCE_EXCLUSIVE | IORESOURCE_BUSY
	},
	{
		.name = "Cached heap",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_MEM
	},
	{
		.name = "Non-Cached heap",
		.start = 0,
		.end = 0,
		.flags = IORESOURCE_MEM
	},
};


/*----------------------------------------------------------------------------*/
static int rpmsg_linkctrl_ack(void *data)
{
	complete(&linkctrl_comp);

	return 0;
}

/*----------------------------------------------------------------------------*/
static int rpmsg_linkctrl_suspend(void *data)
{
	extern int amba_state_store(void *suspend_to);
	AMBA_RPDEV_LINK_CTRL_CMD_s *ctrl_cmd = (AMBA_RPDEV_LINK_CTRL_CMD_s *) data;
	struct task_struct *task;

	task = kthread_run(amba_state_store, (void *) ctrl_cmd->Param1,
	                   "linkctrl_suspend");
	if (IS_ERR(task))
		return PTR_ERR(task);

	return 0;
}

/*----------------------------------------------------------------------------*/
static int rpmsg_linkctrl_gpio_linux_only_list(void *data)
{
	extern void ambarella_gpio_create_linux_only_mask(u32 gpio);
	AMBA_RPDEV_LINK_CTRL_CMD_s *ctrl_cmd = (AMBA_RPDEV_LINK_CTRL_CMD_s *) data;
	u8 *p;
	int ret;
	u32 gpio, virt;

	virt = ambalink_phys_to_virt(ctrl_cmd->Param1);

	ambcache_inv_range((void *) virt, strlen((char*) virt));

	while((p = (u8 *) strsep((char **) &virt, ", "))) {
		ret = kstrtouint(p, 0, &gpio);
		if (ret < 0) {
			continue;
		}

		ambarella_gpio_create_linux_only_mask(gpio);
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
static int rpmsg_linkctrl_set_rtos_mem(void *data)
{
	int rval = 0;
	LINK_CTRL_MEMINFO_CMD_s *meminfo_cmd = (LINK_CTRL_MEMINFO_CMD_s *) data;

	amba_rtosmem.start = meminfo_cmd->RtosStart;
	amba_rtosmem.end = meminfo_cmd->RtosEnd;

	rval = request_resource(&iomem_resource, &amba_rtosmem);
	if (rval < 0 && rval != -EBUSY) {
		printk("Ambarella RTOS memory resource %pR cannot be added\n", &amba_rtosmem);
	}

	amba_heapmem[0].start	= meminfo_cmd->RtosSystemStart;
	amba_heapmem[0].end	= meminfo_cmd->RtosSystemEnd - 1;
	amba_heapmem[1].start	= meminfo_cmd->CachedHeapStart;
	amba_heapmem[1].end	= meminfo_cmd->CachedHeapEnd - 1;
	amba_heapmem[2].start	= meminfo_cmd->NonCachedHeapStart;
	amba_heapmem[2].end	= meminfo_cmd->NonCachedHeapEnd - 1;

	rval = request_resource(&amba_rtosmem, &amba_heapmem[0]);
	if (rval < 0 && rval != -EBUSY) {
		printk("Ambarella RTOS system memory resource %pR cannot be added\n",
			&amba_heapmem[0]);
	}

	rval = request_resource(&amba_rtosmem, &amba_heapmem[1]);
	if (rval < 0 && rval != -EBUSY) {
		printk("Ambarella RTOS cached heap memory resource %pR cannot be added\n",
			&amba_heapmem[1]);
	}

	rval = request_resource(&amba_rtosmem, &amba_heapmem[2]);
	if (rval < 0 && rval != -EBUSY) {
		printk("Ambarella RTOS non-cached heap memory resource %pR cannot be added\n",
			&amba_heapmem[2]);
	}

	return 0;
}

/*----------------------------------------------------------------------------*/
int rpmsg_linkctrl_cmd_get_mem_info(u8 type, void **base, void **phys, u32 *size)
{
	AMBA_RPDEV_LINK_CTRL_CMD_s ctrl_cmd;
	u32 phy_addr;

	//printk("%s: type=%d\n", __func__, type);

	phy_addr = ambalink_virt_to_phys((u32) &rpdev_meminfo);
	ambcache_flush_range(&rpdev_meminfo, sizeof(AMBA_RPDEV_LINK_CTRL_MEMINFO_t));

	memset(&ctrl_cmd, 0x0, sizeof(ctrl_cmd));
	ctrl_cmd.Cmd = LINK_CTRL_CMD_GET_MEM_INFO;
	ctrl_cmd.Param1 = (u32)type;
	ctrl_cmd.Param2 = phy_addr;

	rpmsg_send(rpdev_linkctrl, &ctrl_cmd, sizeof(ctrl_cmd));

	wait_for_completion(&linkctrl_comp);

	ambcache_inv_range((void *) ambalink_phys_to_virt(phy_addr), 32);

	*base = rpdev_meminfo.base_addr;
	*phys = rpdev_meminfo.phys_addr;
	*size = rpdev_meminfo.size;

	//printk("%s: type=%u, base=%p, phys=%p size=0x%08x\n", __func__ ,
	//	type, rpdev_meminfo.base_addr, rpdev_meminfo.phys_addr, rpdev_meminfo.size);

	return 0;
}
EXPORT_SYMBOL(rpmsg_linkctrl_cmd_get_mem_info);

int rpmsg_linkctrl_cmd_hiber_prepare(u32 info)
{
	AMBA_RPDEV_LINK_CTRL_CMD_s ctrl_cmd;

	printk("%s: 0x%08x\n", __func__ , info);

	memset(&ctrl_cmd, 0x0, sizeof(ctrl_cmd));
	ctrl_cmd.Cmd = LINK_CTRL_CMD_HIBER_PREPARE_FROM_LINUX;
	ctrl_cmd.Param1 = info;

	rpmsg_send(rpdev_linkctrl, &ctrl_cmd, sizeof(ctrl_cmd));

	wait_for_completion(&linkctrl_comp);

	ambcache_inv_range((void *) ambalink_phys_to_virt(info), 32);

	hibernation_start = 1;

	return 0;
}
EXPORT_SYMBOL(rpmsg_linkctrl_cmd_hiber_prepare);

int rpmsg_linkctrl_cmd_hiber_enter(int flag)
{
	AMBA_RPDEV_LINK_CTRL_CMD_s ctrl_cmd;

	printk("%s\n", __func__);

	memset(&ctrl_cmd, 0x0, sizeof(ctrl_cmd));
	ctrl_cmd.Cmd = LINK_CTRL_CMD_HIBER_ENTER_FROM_LINUX;
	ctrl_cmd.Param1 = flag;

	rpmsg_send(rpdev_linkctrl, &ctrl_cmd, sizeof(ctrl_cmd));

	return 0;
}
EXPORT_SYMBOL(rpmsg_linkctrl_cmd_hiber_enter);

int rpmsg_linkctrl_cmd_hiber_exit(int flag)
{
	AMBA_RPDEV_LINK_CTRL_CMD_s ctrl_cmd;

	wait_for_completion(&linkctrl_comp);

	printk("%s:\n", __func__);

	memset(&ctrl_cmd, 0x0, sizeof(ctrl_cmd));
	ctrl_cmd.Cmd = LINK_CTRL_CMD_HIBER_EXIT_FROM_LINUX;
	ctrl_cmd.Param1 = flag;

	rpmsg_send(rpdev_linkctrl, &ctrl_cmd, sizeof(ctrl_cmd));

	hibernation_start = 0;

	return 0;
}
EXPORT_SYMBOL(rpmsg_linkctrl_cmd_hiber_exit);

/*----------------------------------------------------------------------------*/
typedef int (*PROC_FUNC)(void *data);
static PROC_FUNC linkctrl_proc_list[] = {
	rpmsg_linkctrl_ack,
	rpmsg_linkctrl_suspend,
	rpmsg_linkctrl_gpio_linux_only_list,
	rpmsg_linkctrl_set_rtos_mem,
};

static void rpmsg_linkctrl_cb(struct rpmsg_channel *rpdev, void *data, int len,
                              void *priv, u32 src)
{
	AMBA_RPDEV_LINK_CTRL_CMD_s *msg = (AMBA_RPDEV_LINK_CTRL_CMD_s *) data;

#if 0
	printk("recv: cmd = [%d], data = [0x%08x]", msg->Cmd, msg->Param);
#endif

	switch (msg->Cmd) {
	case LINK_CTRL_CMD_HIBER_ACK:
		linkctrl_proc_list[0](data);
		break;
	case LINK_CTRL_CMD_SUSPEND:
		linkctrl_proc_list[1](data);
		break;
	case LINK_CTRL_CMD_GPIO_LINUX_ONLY_LIST:
		linkctrl_proc_list[2](data);
		break;
	case LINK_CTRL_CMD_SET_RTOS_MEM:
		linkctrl_proc_list[3](data);
		break;
	default:
		break;
	}
}

static int rpmsg_linkctrl_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
	struct rpmsg_ns_msg nsm;

	rpdev_linkctrl = rpdev;
	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;

	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_linkctrl_remove(struct rpmsg_channel *rpdev)
{
}

static struct rpmsg_device_id rpmsg_linkctrl_id_table[] = {
	{ .name = "AmbaRpdev_LinkCtrl", },
	{ },
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_linkctrl_id_table);

static struct rpmsg_driver rpmsg_linkctrl_driver = {
	.drv.name   = KBUILD_MODNAME,
	.drv.owner  = THIS_MODULE,
	.id_table   = rpmsg_linkctrl_id_table,
	.probe      = rpmsg_linkctrl_probe,
	.callback   = rpmsg_linkctrl_cb,
	.remove     = rpmsg_linkctrl_remove,
};

static int __init rpmsg_linkctrl_init(void)
{
	return register_rpmsg_driver(&rpmsg_linkctrl_driver);
}

static void __exit rpmsg_linkctrl_fini(void)
{
	unregister_rpmsg_driver(&rpmsg_linkctrl_driver);
}

module_init(rpmsg_linkctrl_init);
module_exit(rpmsg_linkctrl_fini);

MODULE_DESCRIPTION("RPMSG AmbaRpdev_LinkCtrl Server");
