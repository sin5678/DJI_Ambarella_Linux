/*
 * arch/arm/plat-ambarella/misc/aipc_mutex.c
 *
 * Authors:
 *  Joey Li <jli@ambarella.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * Software distributed under the License is distributed on an "AS
 * IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
 * implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * Copyright (C) 2013-2015, Ambarella Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <linux/aipc/ipc_mutex.h>
#include <mach/init.h>
#include <mach/hardware.h>
#include <mach/io.h>
#include <plat/rct.h>
#include <plat/ambalink_cfg.h>

extern void __aipc_spin_unlock_irqrestore(unsigned long *lock, unsigned long flags);
extern void __aipc_spin_lock_irqsave(unsigned long *lock, unsigned long *flags);

#define OWNER_IS_LOCAL(x)   ((x)->owner == AMBALINK_CORE_LOCAL)
#define OWNER_IS_EMPTY(x)   ((x)->owner == 0)
#define OWNER_IS_REMOTE(x)  (!OWNER_IS_LOCAL(x) && !OWNER_IS_EMPTY(x))

#define CORTEX_CORE
typedef struct {
	unsigned long slock;        // the bare-metal spinlock
	unsigned char owner;        // entity that currently owns the mutex
	unsigned char wait_list;    // entities that are waiting for the mutex
	char          padding[2];
} amutex_share_t;

typedef struct {
	struct mutex      mutex;
	struct completion completion;
	unsigned int      count;
} amutex_local_t;

typedef struct {
	amutex_local_t local[AMBA_IPC_NUM_MUTEX];
	amutex_share_t *share;
} amutex_db;

static amutex_db lock_set;
static int ipc_mutex_inited = 0;

static int procfs_mutex_show(struct seq_file *m, void *v)
{
	int len;

	len = seq_printf(m,
	                 "\n"
	                 "usage: echo id [op] > /proc/ambarella/mutex\n"
	                 "    \"echo n +\" to lock mutex n\n"
	                 "    \"echo n -\" to unlock mutex n\n"
	                 "\n");

	return len;
}

static ssize_t procfs_mutex_write(struct file *file,
                              const char __user *buffer, size_t count, loff_t *data)
{
	unsigned char str[128], op;
	int  id = 0;

	//memset(str, 0, sizeof(str));
	if (copy_from_user(str, buffer, count)) {
		printk(KERN_ERR "copy_from_user failed, aborting\n");
		return -EFAULT;
	}
	sscanf(str, "%d %c", &id, &op);

	switch (op) {
	case '+':
		printk("try to lock mutex %d, %p\n", id, current);
		aipc_mutex_lock(id);
		printk("done\n");
		break;
	case '-':
		printk("unlock mutex %d, %p\n", id, current);
		aipc_mutex_unlock(id);
		break;
	default:
		printk("unknow operations, "
		       "try \"cat /proc/ambarella/mutex\" for details\n");
	}

	return count;
}

static int procfs_mutex_open(struct inode *inode, struct file *file)
{
	return single_open(file, procfs_mutex_show, PDE_DATA(inode));
}

static const struct file_operations proc_ipc_mutex_fops = {
	.open = procfs_mutex_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.write = procfs_mutex_write,
	.release = single_release,
};

static void init_procfs(void)
{
	struct proc_dir_entry *aipc_dev;

	aipc_dev = proc_create_data("mutex", S_IRUGO | S_IWUSR,
	                            get_ambarella_proc_dir(),
	                            &proc_ipc_mutex_fops, NULL);
}

static irqreturn_t ipc_mutex_isr(int irq, void *dev_id)
{
	int i;

	//printk("isr %d", irq);

	// clear the irq
#if defined(CONFIG_ARM_GIC)
	amba_writel(AHB_SCRATCHPAD_REG(0x14), 0x1 << (irq - AXI_SOFT_IRQ(0)));
#else
        amba_writel(AMBALINK_VIC_REG(VIC_SOFT_INT_CLR_INT_OFFSET), irq % 32);
#endif

	// wake up
	for (i = 0; i < AMBA_IPC_NUM_MUTEX; i++) {
		if (lock_set.share[i].wait_list & AMBALINK_CORE_LOCAL) {
			complete_all(&lock_set.local[i].completion);
		}
	}
	return IRQ_HANDLED;
}

static int aipc_mutex_init(void)
{
	int i, ret;

	// init shared part of aipc mutex memory
	lock_set.share = (amutex_share_t*) AIPC_MUTEX_ADDR;
	//memset(lock_set.share, 0, sizeof(amutex_share_t)*AMBA_IPC_NUM_MUTEX);

	// init local part of aipc mutex memory
	for (i = 0; i < AMBA_IPC_NUM_MUTEX; i++) {
		lock_set.local[i].count = 0;
		mutex_init(&lock_set.local[i].mutex);
		init_completion(&lock_set.local[i].completion);
	}

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	boss_set_irq_owner(MUTEX_IRQ_LOCAL, BOSS_IRQ_OWNER_LINUX, 1);
#endif

	// IRQ handling
	ret = request_irq(MUTEX_IRQ_LOCAL, ipc_mutex_isr,
	                  IRQF_SHARED | IRQF_TRIGGER_HIGH, "aipc_mutex", &lock_set);
	if (ret) {
		printk(KERN_ERR "aipc_mutex_init err %d while requesting irq %d\n",
		       ret, MUTEX_IRQ_LOCAL);
		return 1;
	}

	init_procfs();

	ipc_mutex_inited = 1;

	printk("%s done\n", __func__);

	return 0;
}

static void aipc_mutex_exit(void)
{
}

void aipc_mutex_lock(int id)
{
	unsigned long flags;
	amutex_share_t *share;
	amutex_local_t *local;

	if (!ipc_mutex_inited) {
		/* FIXME: time_init will call clk_get_rate.
		 * But aipc_mutex_init is not called yet. */
		return;
	}

	if (id < 0 || id >= AMBA_IPC_NUM_MUTEX) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	share = &lock_set.share[id];
	local = &lock_set.local[id];

	// check repeatedly until we become the owner
#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
	flags = arm_irq_save();
#else
	__aipc_spin_lock_irqsave(&share->slock, &flags);
#endif
	{
		while (OWNER_IS_REMOTE(share)) {
			share->wait_list |= AMBALINK_CORE_LOCAL;
#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
			arm_irq_restore(flags);
#else
			__aipc_spin_unlock_irqrestore(&share->slock, flags);
#endif

			// wait for remote owner to finish
			/*
			 * use timeout api since the task priority lower than boss's
			 * may acquire the lock before linux.
			 * linux sleep and send rirq to rtos can let the lower
			 * priority task have chance to execute and release the lock.
			 **/
			wait_for_completion_timeout(&local->completion, 1);
			msleep(1);

			// lock and check again
#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
                       flags = arm_irq_save();
#else
			__aipc_spin_lock_irqsave(&share->slock, &flags);
#endif
		}
		// set ourself as owner (note that we might be owner already)
		share->owner = AMBALINK_CORE_LOCAL;
		share->wait_list &= ~AMBALINK_CORE_LOCAL;
		local->count++;
	}
#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
       arm_irq_restore(flags);
#else
	__aipc_spin_unlock_irqrestore(&share->slock, flags);
#endif

	// lock the local mutex
	mutex_lock(&local->mutex);
}

void aipc_mutex_unlock(int id)
{
	unsigned long flags;
	amutex_share_t *share;
	amutex_local_t *local;

	if (!ipc_mutex_inited) {
		/* FIXME: time_init will call clk_get_rate.
		 * But aipc_mutex_init is not called yet. */
		return;
	}

	if (id < 0 || id >= AMBA_IPC_NUM_MUTEX) {
		printk(KERN_ERR "%s: invalid id %d\n", __FUNCTION__, id);
		return;
	}
	share = &lock_set.share[id];
	local = &lock_set.local[id];

	if (!OWNER_IS_LOCAL(share)) {
		// we are not the owner, there must be something wrong
		printk(KERN_ERR "aipc_mutex_unlock(%d) non-owner error\n", id);
		BUG();
		return;
	}

	// unlock local mutex
	mutex_unlock(&local->mutex);

#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
       flags = arm_irq_save();
#else
	__aipc_spin_lock_irqsave(&share->slock, &flags);
#endif
	if (--local->count == 0) {
		// We are done with the mutex,
		// now let other waiting core(s) to grab it
		share->owner = 0;
		if (share->wait_list) {
			// notify remote waiting core(s)
#if defined(CONFIG_ARM_GIC)
			amba_writel(AHB_SCRATCHPAD_REG(0x10),
				    0x1 << (MUTEX_IRQ_REMOTE - AXI_SOFT_IRQ(0)));
#else
                        amba_writel(AMBALINK_VIC_REG(VIC_SOFT_INT_INT_OFFSET),
				    MUTEX_IRQ_REMOTE % 32);
#endif
			//printk("wakeup tx\n");
		}
	}
#if defined(CONFIG_PLAT_AMBARELLA_BOSS) && defined(CONFIG_AMBALINK_SINGLE_CORE)
       arm_irq_restore(flags);
#else
	__aipc_spin_unlock_irqrestore(&share->slock, flags);
#endif
}

subsys_initcall(aipc_mutex_init);
module_exit(aipc_mutex_exit);
MODULE_DESCRIPTION("Ambarella IPC mutex");
