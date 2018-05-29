#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/pagemap.h>
#include <linux/writeback.h>
#include <linux/spinlock.h>
#include <asm/page.h>
#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>
#include "ambafs.h"
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
#include <mach/hardware.h>
#include <mach/boss.h>
#include <plat/rct.h>
#endif

int *qstat_buf;

/*
 * helper function to trigger/wait a remote command excution
 */
static void exec_cmd(struct inode *inode, struct dentry *dentry,
		void *buf, int size, int cmd)
{
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)buf;
	struct dentry *dir = (struct dentry *)inode->i_private;
	char *path = (char*)msg->parameter;
	int len;

	//dir = d_find_any_alias(inode);
	len = ambafs_get_full_path(dir, path, (char*)buf + size - path);
	path[len] = '/';
	strcpy(path+len+1, dentry->d_name.name);
	//dput(dir);

	msg->cmd = cmd;
	ambafs_rpmsg_exec(msg, strlen(path) + 1);
}

/*
 * couple a dentry with inode
 */
static void ambafs_attach_inode(struct dentry *dentry, struct inode *inode)
{
	inode->i_private = dentry;
	d_instantiate(dentry, inode);
	if (d_unhashed(dentry))
		d_rehash(dentry);
}

/*
 * lookup for a specific @dentry under dir @inode
 */
static struct dentry *ambafs_lookup(struct inode *inode,
	            struct dentry *dentry, unsigned int flags)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)buf;
	struct ambafs_stat *stat = (struct ambafs_stat*)msg->parameter;

	AMBAFS_DMSG("%s:  %s \r\n", __func__, dentry->d_name.name);

	exec_cmd(inode, dentry, msg, sizeof(buf), AMBAFS_CMD_STAT);
	if (msg->flag) {
		inode = ambafs_new_inode(dentry->d_sb, stat);
		if (inode) {
			struct dentry *new = d_splice_alias(inode, dentry);
			inode->i_private = dentry;
			ambafs_update_inode(inode, stat);
			return new;
		}
	}

	return NULL;
}

/*
 * create a new file @dentry under dir @inode
 */
static int ambafs_create(struct inode *inode, struct dentry *dentry, umode_t mode,
				bool excl)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg *)buf;
	struct ambafs_stat *stat = (struct ambafs_stat*)msg->parameter;

	AMBAFS_DMSG("%s:  %s \r\n", __func__, dentry->d_name.name);

	exec_cmd(inode, dentry, msg, sizeof(buf), AMBAFS_CMD_CREATE);
	if (stat->type == AMBAFS_STAT_FILE) {
		struct inode *child = ambafs_new_inode(dentry->d_sb, stat);
		if (child) {
			ambafs_attach_inode(dentry, child);
			ambafs_update_inode(child, stat);
			return 0;
		}
	}

	return -ENODEV;
}

/*
 * remove the file @dentry under dir @inode
 */
static int ambafs_unlink(struct inode *inode, struct dentry *dentry)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg *)buf;

	AMBAFS_DMSG("%s:  %s \r\n", __func__, dentry->d_name.name);
	exec_cmd(inode, dentry, msg, sizeof(buf), AMBAFS_CMD_DELETE);
	if (msg->flag == 0) {
		drop_nlink(dentry->d_inode);
		return 0;
	}
	return -EBUSY;
}

/*
 * make a new dir @dentry under dir @inode
 */
static int ambafs_mkdir(struct inode *inode,
			struct dentry *dentry, umode_t mode)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg *)buf;
	struct ambafs_stat *stat = (struct ambafs_stat*)msg->parameter;

	AMBAFS_DMSG("%s:  %s \r\n", __func__, dentry->d_name.name);

	exec_cmd(inode, dentry, msg, sizeof(buf), AMBAFS_CMD_MKDIR);
	if (stat->type == AMBAFS_STAT_DIR) {
		struct inode *child = ambafs_new_inode(dentry->d_sb, stat);
		if (child) {
			ambafs_attach_inode(dentry, child);
			ambafs_update_inode(child, stat);
			inc_nlink(child);
			inc_nlink(inode);
			return 0;
		}
	}

	return -ENODEV;
}

/*
 * remove the dir @dentry under dir @inode
 */
static int ambafs_rmdir(struct inode *inode, struct dentry *dentry)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg *)buf;

	AMBAFS_DMSG("%s: %s\n", __func__, dentry->d_name.name);

	exec_cmd(inode, dentry, msg, sizeof(buf), AMBAFS_CMD_RMDIR);
	if (msg->flag == 0) {
		clear_nlink(dentry->d_inode);
		drop_nlink(inode);
		return 0;
	}
	return -EBUSY;
}

/*
 * move a file or directory
 */
static int ambafs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	int buf[128];
	struct ambafs_msg  *msg  = (struct ambafs_msg  *)buf;
	struct ambafs_stat *stat = (struct ambafs_stat*)msg->parameter;
	char *path, *end = (char*)buf + sizeof(buf);
	int len;

	AMBAFS_DMSG("ambafs_rename %s-->%s\n",
		old_dentry->d_name.name, new_dentry->d_name.name);

	/* get new path */
	path = (char*)msg->parameter;
	len = ambafs_get_full_path(new_dentry, path, end - path);

        /* get old path */
	path += strlen(path) + 1;
	len = ambafs_get_full_path(old_dentry, path, end - path);
	len = strchr(path, 0) + 1 - (char*)msg->parameter;

	msg->cmd = AMBAFS_CMD_RENAME;
	path = (char*)msg->parameter;
	ambafs_rpmsg_exec(msg, len);

	if (msg->flag != 0) {
		int is_dir = (stat->type == AMBAFS_STAT_DIR) ? 1 : 0;
		if (new_dentry->d_inode) {
			drop_nlink(new_dentry->d_inode);
			if (is_dir) {
				drop_nlink(old_dir);
				drop_nlink(new_dentry->d_inode);
			}
		} else if (is_dir) {
			drop_nlink(old_dir);
			inc_nlink(new_dir);
		}
		return 0;
	}

	return -EBUSY;
}

const struct inode_operations ambafs_dir_inode_ops = {
	.lookup = ambafs_lookup,
	.create = ambafs_create,
	.unlink = ambafs_unlink,
	.mkdir  = ambafs_mkdir,
	.rmdir  = ambafs_rmdir,
	.rename = ambafs_rename,
};

/*
 * ambafs quick stat.
 */
struct ambafs_qstat* ambafs_get_qstat(struct dentry *dentry, struct inode *inode, void *buf, int size)
{
	struct ambafs_msg *msg = (struct ambafs_msg*)buf;
	struct dentry *dir;
	struct ambafs_qstat *stat = (struct ambafs_qstat*)msg->parameter;
	char *path = (char*)&(msg->parameter[1]);
	int len, i;

	if (dentry) {
		dir = dentry;
	} else if (inode && inode->i_private) {
		dir = (struct dentry*) inode->i_private;
	} else {
		stat->type = AMBAFS_STAT_NULL;
		goto exit;
	}

	len = ambafs_get_full_path(dir, path, (char*)buf + size - path);

        msg->parameter[0] = (int) ambalink_virt_to_phys((u32)stat);

	AMBAFS_DMSG("%s: path = %s, quick_stat result phy address = 0x%x \r\n", __func__, path, msg->parameter[0]);

	msg->cmd = AMBAFS_CMD_QUICK_STAT;
	ambafs_rpmsg_send(msg, len + 1 + 4, NULL, NULL);

	for (i = 0; i < 65536; i++) {
		ambcache_inv_range((void *)&stat->magic, 32);
		if (stat->magic == AMBAFS_QSTAT_MAGIC) {
			stat->magic = 0x0;
			ambcache_inv_range((void *)&stat->statp, 32);
			ambcache_clean_range((void *)&stat->magic, 32);
			break;
		}
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
		/* Send RIRQ to let RTOS do fstat quickly. */
#if defined(CONFIG_ARM_GIC)
		amba_writel(AHB_SCRATCHPAD_REG(AHBSP_SWI_SET_OFFSET),
			    0x1 << (BOSS_VIRT_RIRQ_INT_VEC - AXI_SOFT_IRQ(0)));
#else
		amba_writel(AMBALINK_VIC_REG(VIC_SOFT_INT_INT_OFFSET),
			    BOSS_VIRT_RIRQ_INT_VEC % 32);
#endif
#endif
	}

	if (i == 65536)
		stat->type = AMBAFS_STAT_NULL;

exit:
	return stat;
}

static int ambafs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct inode *inode = dentry->d_inode;
	int valid = 1;

	if (inode && S_ISDIR(inode->i_mode)) {
		void *align_buf;
		struct ambafs_qstat *astat;

		align_buf = (void *)((((u32) qstat_buf) & (~0x1f)) + 0x20);
		//AMBAFS_DMSG("%s: buf virt = 0x%x, buf phy = 0x%x\r\n", __func__, (int) align_buf, (int) __pfn_to_phys(vmalloc_to_pfn((void *) align_buf)));
		astat = ambafs_get_qstat(NULL, dentry->d_inode, align_buf, QSTAT_BUFF_SIZE);
		if (astat->type == AMBAFS_STAT_NULL) {
			valid = 0;
		}
	}

	AMBAFS_DMSG("%s: flags = 0x%x, valid = %d\r\n", __func__, flags, valid);

	return valid;
}

const struct dentry_operations ambafs_dentry_ops = {
	.d_revalidate	= ambafs_d_revalidate,
};

/*
 * get inode stat
 */
struct ambafs_stat* ambafs_get_stat(struct dentry *dentry, struct inode *inode, void *buf, int size)
{
	struct ambafs_msg *msg = (struct ambafs_msg*)buf;
	struct dentry *dir;
	struct ambafs_stat *stat = (struct ambafs_stat*)msg->parameter;
	char *path = (char*)msg->parameter;
	int len;

	if (dentry) {
		dir = dentry;
	} else if (inode && inode->i_private) {
		dir = (struct dentry*) inode->i_private;
	} else {
		stat->type = AMBAFS_STAT_NULL;
		goto exit;
	}

	len = ambafs_get_full_path(dir, path, (char*)buf + size - path);

	AMBAFS_DMSG("%s:  %s \r\n", __func__, path);

	msg->cmd = AMBAFS_CMD_STAT;
	ambafs_rpmsg_exec(msg, len+1);
	if (!msg->flag)
		stat->type = AMBAFS_STAT_NULL;

exit:
	return stat;
}

/*
 * get file attriubte, for cache consistency
 */
static int ambafs_getattr(struct vfsmount *mnt, struct dentry *dentry,
		struct kstat *stat)
{
	AMBAFS_DMSG("%s:  \r\n", __func__);

	if (dentry->d_inode && (dentry->d_inode->i_opflags & AMBAFS_IOP_SKIP_GET_STAT)) {
		dentry->d_inode->i_opflags &= ~AMBAFS_IOP_SKIP_GET_STAT;
	} else {
		int buf[128];
		struct ambafs_stat *astat;

		astat = ambafs_get_stat(NULL, dentry->d_inode, buf, sizeof(buf));
		if (astat->type == AMBAFS_STAT_NULL) {
			return -ENOENT;
		}

		if (astat->type == AMBAFS_STAT_FILE) {
			ambafs_update_inode(dentry->d_inode, astat);
		}
	}
	return simple_getattr(mnt, dentry, stat);
}


const struct inode_operations ambafs_file_inode_ops = {
	.getattr = ambafs_getattr,
};
