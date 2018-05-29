#include "ambafs.h"
#include <linux/pagemap.h>
#include <linux/pagemap.h>
#include <linux/pagevec.h>

struct readdir_db {
	struct ambafs_stat *stat;
	unsigned            nlink;
	unsigned            offset;
	struct ambafs_msg   msg;
};

/*
 * invalidate the page cache contents for an inode
 */
static void clear_cache_contents(struct address_space *mapping)
{
	AMBAFS_DMSG("%s \r\n", __func__);
	invalidate_mapping_pages(mapping, 0, -1);
}

/*
 * find a inode by its path
 */
static struct inode* ambafs_inode_lookup(struct dentry *dir, char *name)
{
	struct qstr qname;
	struct inode *inode = NULL;
	struct dentry *dentry;

	qname.name = name;
	qname.len  = strlen(name);
	qname.hash = full_name_hash(qname.name, qname.len);

	dentry = d_lookup(dir, &qname);
	if (dentry) {
		inode = dentry->d_inode;
		dput(dentry);
	}

	return inode;
}

/*
 * update the inode field
 */
void ambafs_update_inode(struct inode *inode, struct ambafs_stat *stat)
{
	struct timespec ftime;
	struct dentry *dentry = (struct dentry *)inode->i_private;

	AMBAFS_DMSG("%s: 1 inode->i_size = %lld,  stat->size = %lld, inode->i_mtime.tv_sec = %ld, stat->mtime = %ld, inode->i_opflags = 0x%x\r\n",
			__func__,  inode->i_size, stat->size, inode->i_mtime.tv_sec, stat->mtime, inode->i_opflags);
	if ((inode->i_size == stat->size && inode->i_mtime.tv_sec == stat->mtime) /* no modification */ ||
	    (inode->i_mtime.tv_sec > stat->mtime) /* linux modifcation */ ||
	    ((inode->i_opflags & AMBAFS_IOP_CREATE_FOR_WRITE) && (inode->i_size > 0)) /* linux modification */ ) {
		return;
	}

	AMBAFS_DMSG("%s: 2 inode->i_size = %lld,  stat->size = %lld, inode->i_mtime.tv_sec = %ld, stat->mtime = %ld\r\n",
			__func__,  inode->i_size, stat->size, inode->i_mtime.tv_sec, stat->mtime);

	if (inode->i_size != 0) {
		/* purge page cache for the inode */
		clear_cache_contents(inode->i_mapping);
	}

	if (dentry)
		dentry->d_time = jiffies;

	ftime.tv_sec = stat->atime;
	ftime.tv_nsec = 0;

	inode->i_atime = inode->i_mtime = inode->i_ctime = ftime;
	inode->i_size = stat->size;
}

/*
 *  create a new inode based on @stat coming from remote core
 */
struct inode* ambafs_new_inode(struct super_block *sb, struct ambafs_stat* stat)
{
	struct inode *inode;

	AMBAFS_DMSG("%s \r\n", __func__);
	inode = new_inode(sb);
	if (!inode)
		return NULL;

	inode->i_uid = inode->i_gid = 0;
	inode->i_blocks = 0;
	inode->i_ino = iunique(sb, AMBAFS_INO_MAX_RESERVED);
	inode->i_mode = 0744;
	inode->i_atime = inode->i_mtime = inode->i_ctime = (struct timespec) {0, 0};
	inode->i_size = 0;

	if (stat->type == AMBAFS_STAT_FILE) {
		inode->i_mode |= S_IFREG;
		inode->i_fop = &ambafs_file_ops;
		inode->i_op = &ambafs_file_inode_ops;
		inode->i_mapping->a_ops = &ambafs_aops;
		inode->i_mapping->backing_dev_info = &ambafs_bdi;
	} else {
		inode->i_mode |= S_IFDIR;
		inode->i_fop = &ambafs_dir_ops;
		inode->i_op = &ambafs_dir_inode_ops;
	}

	return inode;
}

/*
 * create a new inode and add the corresponding dentry
 */
static struct inode* ambafs_inode_create(struct dentry *dir, struct ambafs_stat* stat)
{
	struct inode *inode;
	struct dentry *dentry;


	AMBAFS_DMSG("%s \r\n", __func__);
	inode = ambafs_new_inode(dir->d_sb, stat);
	if (!inode)
		return NULL;

	dentry = d_alloc_name(dir, stat->name);
	if (!dentry) {
		iput(inode);
		return 0;
	}

	d_add(dentry, inode);
	inode->i_private = dentry;
	dput(dentry);
	return inode;
}

/*
 * put full path of @dir into @buf
 */
int ambafs_get_full_path(struct dentry *dir, char *buf, int len)
{
        char *src, *dst;
	char *root = (char*)dir->d_sb->s_fs_info;

	dst = buf;
	strcpy(dst, root);
	dst += strlen(dst);

	src = dentry_path_raw(dir, buf, len);
	len = strlen(src);
	if (len > 1) {
		// if this is not root dir, copy the string
		// need to use memmov since dst and src might overlap
		memmove(dst, src, len+1);
		dst += len;
	}

	return dst - buf;
}

/*
 * process return msg for readdir
 *   Here we parse each @stat and then fill it to user-space buffer by
 *   filldir. If the directory has tons of files and filldir fails,
 *   we save the context at the point of failure so we can resume the
 *   time we got called.
 */
static int fill_dir_from_msg(struct readdir_db *dir_db, struct dentry *dir,
			void *dirent, filldir_t filldir)
{
	struct inode *inode;
	int stat_idx = 0, stat_len;
	struct ambafs_msg  *msg  = &dir_db->msg;
	struct ambafs_stat *stat = dir_db->stat;

	AMBAFS_DMSG("%s \r\n", __func__);
	while (stat_idx < msg->flag) {
		if (stat->type == AMBAFS_STAT_DIR)
			dir_db->nlink++;

		if (!strcmp(stat->name, ".") || !strcmp(stat->name, ".."))
			goto next_stat;

		inode = ambafs_inode_lookup(dir, stat->name);
		if (!inode) {
			inode = ambafs_inode_create(dir, stat);
			if (!inode) {
				AMBAFS_EMSG("readdir fail for new node\n");
				BUG();
			}
		}
		ambafs_update_inode(inode, stat);
		inode->i_opflags |= AMBAFS_IOP_SKIP_GET_STAT;

		if (filldir(dirent, stat->name, strlen(stat->name),
			    dir_db->offset++,  inode->i_ino,
			    stat->type == AMBAFS_STAT_FILE ? DT_REG : DT_DIR)) {

			AMBAFS_DMSG("filldir paused at %s\n", stat->name);
			dir_db->stat = stat;
			msg->flag -= stat_idx;
			if (stat->type == AMBAFS_STAT_DIR)
				dir_db->nlink--;
			return 1;
		}

next_stat:
		stat_idx++;
		stat_len = offsetof(struct ambafs_stat, name);
		stat_len += strlen(stat->name) + 1;
		stat_len = (stat_len + 7) & ~7;
		stat = (struct ambafs_stat*)((char*)stat + stat_len);
	}

	msg->flag = 0;
	dir_db->stat = (struct ambafs_stat*)msg->parameter;
	return 0;
}

/*
 * Kick off a readdir operation
 *   We allocate a page to hold all info because filldir might fail.
 */
static int start_readdir(struct file *file)
{
	struct readdir_db  *dir_db;
	struct ambafs_msg  *msg;
	struct dentry      *dir = file->f_dentry;
	char *path;
	int ret;

	dir_db = (struct readdir_db*)get_zeroed_page(GFP_KERNEL);
	if (!dir_db)
		return -ENOMEM;
	file->f_pos = (loff_t)((unsigned)dir_db);

	msg = &dir_db->msg;
	path = (char*)msg->parameter;
	ambafs_get_full_path(dir, path, 2048);
	strcat(path, "/*");

	AMBAFS_DMSG("%s %s\r\n", __func__, path);
	msg->cmd = AMBAFS_CMD_LS_INIT;
	msg->flag = 16;
	ambafs_rpmsg_exec(msg, strlen(path) + 1);
        dir_db->stat = (struct ambafs_stat*)msg->parameter;

	// When msg->flag = 0, the error code is save in dir_db->stat->type.
	if (msg->flag == 0 && dir_db->stat->type < 0)
		ret = -EIO;
	else
		ret = msg->flag;

	return ret;
}

/*
 * send LS_INIT/LS_NEXT/LS_EXIT rpmsg to get the directory info
 *    f_pos == 0: readdir is started
 *    f_pos == LLONG_MAX: readdir is completely finished.
 *    otherwise, f_pos holds the page address
 */
static int ambafs_dir_readdir(struct file *file, void *dirent, filldir_t filldir)
{
	struct readdir_db  *dir_db;
	struct ambafs_msg  *msg;
	struct dentry      *dir = file->f_dentry;
	int ret;

	if (file->f_pos == LLONG_MAX)
		return 0;

	AMBAFS_DMSG("%s \r\n", __func__);
	if (file->f_pos == 0) {
		if ((ret = start_readdir(file)) <= 0) {
			if (ret < 0)
				printk(KERN_ERR "start_readdir nodev\n");
			goto ls_exit;
		}
	}


	dir_db = (struct readdir_db*)((unsigned)file->f_pos);
	msg = &(dir_db->msg);

	if (msg->flag)
		fill_dir_from_msg(dir_db, dir, dirent, filldir);

        while (1) {
		msg->flag = 16;
		msg->cmd = AMBAFS_CMD_LS_NEXT;
		ambafs_rpmsg_exec(msg, 4);
	        if (msg->flag == 0)
			break;

		if (fill_dir_from_msg(dir_db, dir, dirent, filldir))
			return 1;
	}
	set_nlink(file->f_dentry->d_inode, dir_db->nlink);

ls_exit:
	/* note that LS_EXIT doesn't expect a reply */
	AMBAFS_DMSG("readdir end\n");

	dir_db = (struct readdir_db*)((unsigned)file->f_pos);
	msg = &(dir_db->msg);
	msg->cmd = AMBAFS_CMD_LS_EXIT;
	ambafs_rpmsg_send(msg, 4, NULL, 0);
	free_page((unsigned long)file->f_pos);
	file->f_pos = LLONG_MAX;
	return 0;
}

const struct file_operations ambafs_dir_ops = {
	.read    = generic_read_dir,
	.readdir = ambafs_dir_readdir,
};

