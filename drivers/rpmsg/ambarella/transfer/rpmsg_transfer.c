/*----------------------------------------------------------------------------*\
 *  @FileName       :: image_transfer.c
 *
 *  @Copyright      :: Copyright (C) 2013 DJI Innovations. All rights reserved.
 *
 *                     No part of this file may be reproduced, stored in a
 *                     retrieval system, or transmitted, in any form, or by any
 *                     means, electronic, mechanical, photocopying, recording,
 *                     or otherwise, without the prior consent of DJI Innovations.
 *
 *  @Description    :: DJI rpmsg driver.
 *
 *  @History        ::
 *    Date       Name            Comments
\*----------------------------------------------------------------------------*/
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/rpmsg.h>
#include <linux/err.h>
#include <linux/vmalloc.h>  // vmalloc() vfree() etc.
#include <linux/fs.h>  //struct inode/file
#include <asm/uaccess.h>  // get_fs() set_fs()
#include <linux/netlink.h>
#include <net/sock.h>
#include <net/netlink.h>
#include <plat/ambcache.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <mach/hardware.h>
#include <plat/ambalink_cfg.h>

#include "rpmsg_transfer.h"

static UINT32  gTransDebugFlg = 0;
#define RPMSG_DEBUG_PRINT       0x00000001
#define RPMSG_DEBUG_SAVE_FILE   0x00000002

#define RPMSG_ERR(fmt, arg...)   printk(KERN_ERR "%s[L%u]: "fmt"\n", __func__, __LINE__, ##arg)
#define RPMSG_INFO(fmt, arg...)  \
    do\
    {\
        if (gTransDebugFlg & RPMSG_DEBUG_PRINT)\
        {\
            printk(KERN_ERR fmt"\n", ##arg);\
        }\
    }while(0)


#define RPMSG_ASSERT_WITH_VAL(_expect_cond, _ret_val, _err_info, _arg...)\
    do\
    {\
        if (!(_expect_cond))\
        {\
            RPMSG_ERR(_err_info, ##_arg);\
            return (_ret_val);\
        }\
    }while(0)

#define RPMSG_ASSERT_WITHOUT_VAL(_expect_cond, _err_info, _arg...)\
    do\
    {\
        if (!(_expect_cond))\
        {\
            RPMSG_ERR(_err_info, ##_arg);\
            return;\
        }\
    }while(0)

static struct file *fpUpdate = NULL;
static struct file *fpVideo = NULL;
//static  loff_t pos = 0;
#undef DBG_RPMSG_TRANS
#define DBG_RPMSG_TRANS

void C0toC1_proc_usr(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);
void C0toC1_proc_drv(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);
void C0toC1_proc_test(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);

void C1toC0_proc_comm(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);

typedef void (*RPMSG_RXPROC)(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);
typedef void (*RPMSG_NETLINK_RXPROC)(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv);

typedef struct
{
    char name[16];
    UINT32  ChnId;
    struct rpmsg_channel *rpdev;
    RPMSG_RXPROC          l1rxproc;
    RPMSG_NETLINK_RXPROC  l2rxproc;
    RPMSG_TRANS_STAT_STRU l1stat;
    RPMSG_TRANS_STAT_STRU l2stat;
} RPMSG_CHN_STRU;

RPMSG_CHN_STRU gstRpmsgChn[] =
{
    {RPMSG_CHNNAME0, 0, NULL, C0toC1_proc_test,   C1toC0_proc_comm,  {0}, {0}},
    {RPMSG_CHNNAME1, 1, NULL, C0toC1_proc_usr,    C1toC0_proc_comm,  {0}, {0}},
    {RPMSG_CHNNAME2, 2, NULL, C0toC1_proc_drv,    NULL,            {0}, {0}},
};

#ifdef HAS_USB_68013
extern int usb_cy7c68013a_write_s(UINT32 addr, UINT32 len);
#endif

//struct proc_dir_entry *pProcDjiDir = NULL;
struct proc_dir_entry *pProcTransDir = NULL;
struct proc_dir_entry *pProcTransStat = NULL;
struct proc_dir_entry *pProcTransDebug = NULL;


module_param(gTransDebugFlg, uint, 0644);
MODULE_PARM_DESC(gTransDebugFlg, "Trans module debug flag control");


#define MCCTRL_TEST_MSG_MEM_INFO    1
struct _RPMSG_MEMINFO_s_ {
    UINT32  base_addr;
    UINT32  phys_addr;
    UINT32  size;
    UINT32  padding[5];
} __attribute__((aligned(32), packed));
typedef struct _RPMSG_MEMINFO_s_ RPMSG_MEMINFO_t;
RPMSG_MEMINFO_t gRpmsgMemInfo;
DECLARE_COMPLETION(gRpmsgCtrl_comp);

static struct file * fop_open(char *filepath, int flg)
{
    struct file *fp;
    mm_segment_t  fs;
    fs = get_fs();
    set_fs(KERNEL_DS);
    fp = filp_open(filepath, flg, 0666);
    if (IS_ERR(fp))
    {
        printk("open file(%s) error/n", filepath);
        return NULL;
    }
    set_fs(fs);
    return fp;
}

static void fop_close(struct file **fp)
{
     mm_segment_t  fs;
     fs = get_fs();
     set_fs(KERNEL_DS);
     filp_close(*fp, NULL);
     *fp = NULL;
     set_fs(fs);
}

void C1toC0_proc_comm(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv)
{
    RPMSG_CHN_STRU *pstChn = (RPMSG_CHN_STRU *)pPriv;
    //printk("%s send c1->c0 msg\n", pstChn->name);
    pstChn->l1stat.tx_total++;
    if (rpmsg_send(pstChn->rpdev, pMsgData, MsgLen))
    {
        pstChn->l1stat.tx_fail++;
    }
    return;
}

static struct sock *pSock_Rpmsg_Netlink = NULL;
/********************************************************************************************
  netlink send from kernel to user space socket
********************************************************************************************/
static UINT32 rpmsg_Pid2Chn(UINT32 Pid)
{
    UINT32 ChnId;
    switch (DJI_IPC_GET_PID_D1(Pid))
    {
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_MCCTRL):
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_IMAGE):
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_COMMAND):
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_DOWNLOAD):
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_RSH):
            ChnId = 1;
            break;
        case DJI_IPC_GET_PID_D1(DJI_IPC_C1_PID_TEST):
            ChnId = 0;
            break;
        default:
            RPMSG_ERR("no pid(0x%04x)", Pid);
            return 0xffffffff;
    }
    return ChnId;
}

static int rpmsg_netlink_K2U(UINT32 Pid, UINT8 *pDataBuf, UINT32 DataLen)
{
    struct sk_buff *skb;
    struct nlmsghdr *nlh;
    UINT32 len;
    UINT32 ChnId;
    int  ret;

    RPMSG_ASSERT_WITH_VAL((pSock_Rpmsg_Netlink), -1, "netlink not ready");
    RPMSG_ASSERT_WITH_VAL((pDataBuf), -1, "buf is null");
    RPMSG_ASSERT_WITH_VAL((DataLen != 0), -1, "len is zero");
    RPMSG_ASSERT_WITH_VAL((DataLen <= RPMSG_NETLINK_MAXPKT), -1, "len(%u) must less than %u", DataLen, RPMSG_NETLINK_MAXPKT);

    len = NLMSG_SPACE(DataLen);
    skb = alloc_skb(len, GFP_KERNEL);
    if(!skb)
    {
        RPMSG_ERR("alloc_skb error");
        return -1;
    }

    nlh = nlmsg_put(skb, 0, 0, 0, DataLen, 0);

    NETLINK_CB(skb).portid = 0;
    NETLINK_CB(skb).dst_group = 0;

    //copy data
    memcpy(NLMSG_DATA(nlh), pDataBuf, DataLen);

    ChnId = rpmsg_Pid2Chn(Pid);
    if (ChnId == 0xffffffff)
    {
        RPMSG_ERR("no C1 pid(0x%04x)", Pid);
        return -1;
    }

    gstRpmsgChn[ChnId].l2stat.tx_total++;
    ret = netlink_unicast(pSock_Rpmsg_Netlink, skb, Pid, MSG_DONTWAIT);
    gstRpmsgChn[ChnId].l2stat.tx_fail += (ret < 0) ? 1 : 0;
    return ret;
}


/********************************************************************************************
  netlink receive from user space socket to kernel
********************************************************************************************/
static void rpmsg_netlink_U2K(struct sk_buff *__skb)
{
    struct nlmsghdr *nlh;
    UINT8  *pMsgData;
	UINT32 MsgLen;
    UINT32 ChnId;

    nlh = (struct nlmsghdr*)__skb->data;
    pMsgData = (UINT8 *)NLMSG_DATA(nlh);
	MsgLen = NLMSG_PAYLOAD(nlh, 0);

    ChnId = rpmsg_Pid2Chn(nlh->nlmsg_pid);
    if (ChnId == 0xffffffff)
    {
        RPMSG_ERR("no C0 pid(0x%08x)", nlh->nlmsg_pid);
        return;
    }

    if (ChnId < RPMSG_CHNNUM)
    {
        gstRpmsgChn[ChnId].l2stat.rx_total++;
        if (gstRpmsgChn[ChnId].l2rxproc)
        {
            gstRpmsgChn[ChnId].l2rxproc(pMsgData, MsgLen, &gstRpmsgChn[ChnId]);
        }
    }
    return;
}

static int rpmsg_netlink_init(void)
{
    struct netlink_kernel_cfg cfg =
    {
        .groups   = 0,
        .input    = rpmsg_netlink_U2K,
    };

    pSock_Rpmsg_Netlink = netlink_kernel_create(&init_net, NETLINK_RPMSG_DJI, &cfg);
    if (!pSock_Rpmsg_Netlink)
    {
        RPMSG_ERR("netlink_kernel_create failed");
        return -EFAULT;
    }

    return 0;
}

static void rpmsg_netlink_exit(void)
{
    if (pSock_Rpmsg_Netlink)
    {
        sock_release(pSock_Rpmsg_Netlink->sk_socket);
        pSock_Rpmsg_Netlink = NULL;
    }
    return;
}

void C0toC1_proc_usr(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv)
{
    int ret;
    IPC_MSG_COMM_STRU *pstMsg = (IPC_MSG_COMM_STRU *)pMsgData;
    RPMSG_ASSERT_WITHOUT_VAL((pstMsg->Magic == IPC_MSG_COMM_MAGIC), "sock chn recv damaged message");
    ret = rpmsg_netlink_K2U(pstMsg->ReceiverId, pMsgData, MsgLen);
    return;
}

void C0toC1_proc_drv(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv)
{
    return;
}

void C0toC1_proc_test(UINT8 *pMsgData, UINT32 MsgLen, void *pPriv)
{
    return;
}


static int proc_isdir_exist(char *dirpath)
{
    struct file *fp;
    fp = filp_open(dirpath, O_RDONLY | O_EXCL, 0755);
    if (IS_ERR(fp))
    {
        return 0;
    }
    else
    {
        filp_close(fp, NULL);
        return 1;
    }
}

static int rpmsg_proc_debug_write(struct file *filp, const char __user *buf, size_t count, loff_t *data)
{
    UINT32 flg;
    UINT32 str_len = 31;
    char str[32];

    count = (count < str_len) ? count : str_len;
    str_len = (count < sizeof(buf)) ? count : sizeof(str);

    if (copy_from_user(str, buf, str_len))
    {
    	RPMSG_ERR("copy_from_user fail!\n");
		return -EFAULT;
	}
    flg = simple_strtoul(str, NULL, 0);
	gTransDebugFlg = flg;
    printk("Set Debug Flag to 0x%08x\n", gTransDebugFlg);

	return count;
}

static int rpmsg_proc_debug_read(struct file *filp, char __user *buf, size_t count, loff_t *offp)
{
    int len = 0;
    char *buftmp;

    buftmp = kzalloc(128, GFP_KERNEL);
	if (!buftmp)
	{
		return 0;
	}

    len = 0;
    len += scnprintf(buftmp+len, count-len, "Debug: 0x%08x\n", gTransDebugFlg);
    len += scnprintf(buftmp+len, count-len, "  Print     Flag: %s\n", (gTransDebugFlg & RPMSG_DEBUG_PRINT)     ? "on" : "off");
    len += scnprintf(buftmp+len, count-len, "  Save File Flag: %s\n", (gTransDebugFlg & RPMSG_DEBUG_SAVE_FILE) ? "on" : "off");

    len = simple_read_from_buffer(buf, count, offp, buftmp, len);

	kfree(buftmp);

    return len;
}

static int rpmsg_proc_stat_read(struct file *filp, char __user *buf, size_t count, loff_t *offp)
{
    int len = 0;
    int i;
    char *buftmp;

    buftmp = kzalloc(1024, GFP_KERNEL);
	if (!buftmp)
	{
		return 0;
	}

    len = 0;
    len += scnprintf(buftmp+len, count-len, "================ Port Info ================\n");
    for (i = 0; i < sizeof(gstRpmsgChn)/sizeof(RPMSG_CHN_STRU); i++)
    {
        if (!gstRpmsgChn[i].rpdev)
        {
            continue;
        }
        len += scnprintf(buftmp+len, count-len, "=== port %u (%s, %p, %p)\n",
            i, gstRpmsgChn[i].name, gstRpmsgChn[i].l1rxproc, gstRpmsgChn[i].l2rxproc);
    }
    len += scnprintf(buftmp+len, count-len, "   ================ L1 =============== | ================ L2 ===============\n");
    len += scnprintf(buftmp+len, count-len, "id tx_total  tx_fail rx_total  rx_fail | tx_total  tx_fail rx_total  rx_fail\n");
    for (i = 0; i < sizeof(gstRpmsgChn)/sizeof(RPMSG_CHN_STRU); i++)
    {
        if (!gstRpmsgChn[i].rpdev)
        {
            continue;
        }
        len += scnprintf(buftmp+len, count-len,
                        "%2u %8u %8u %8u %8u | %8u %8u %8u %8u\n",
                        gstRpmsgChn[i].ChnId,
                        gstRpmsgChn[i].l1stat.tx_total, gstRpmsgChn[i].l1stat.tx_fail,
                        gstRpmsgChn[i].l1stat.rx_total, gstRpmsgChn[i].l1stat.rx_fail,
                        gstRpmsgChn[i].l2stat.tx_total, gstRpmsgChn[i].l2stat.tx_fail,
                        gstRpmsgChn[i].l2stat.rx_total, gstRpmsgChn[i].l2stat.rx_fail);
    }
    if (len > 1024)
    {
        len = 1024;
    }

    len = simple_read_from_buffer(buf, count, offp, buftmp, len);

	kfree(buftmp);

    return len;
}

static const struct file_operations rpmsg_proc_stat_fops = {
	.read = rpmsg_proc_stat_read,
};

static const struct file_operations rpmsg_proc_debug_fops = {
	.read = rpmsg_proc_debug_read,
	.write = rpmsg_proc_debug_write,
};

static int init_proc(void)
{
    int ret = 0;

    if (!proc_isdir_exist("/proc/dji"))
    {
        struct proc_dir_entry *pProcDjiDir = NULL;
        pProcDjiDir = proc_mkdir("dji", NULL);
        if (pProcDjiDir == NULL)
        {
            RPMSG_ERR("creating ProcFs Dir dji fail\n");
            ret = -1;
            goto ERR_TAG0;
        }
    }

    pProcTransDir = proc_mkdir("dji/trans", NULL);
    if (pProcTransDir == NULL)
    {
        RPMSG_ERR("creating ProcFs Dir trans fail\n");
        ret = -1;
        goto ERR_TAG0;
    }

    if (!proc_create("stat", S_IRUGO, pProcTransDir, &rpmsg_proc_stat_fops))
    {
        RPMSG_ERR("creating ProcFs file stat fail\n");
        ret = -1;
        goto ERR_TAG1;
    }

    if (!proc_create("debug", S_IRUGO | S_IWUSR, pProcTransDir, &rpmsg_proc_debug_fops))
    {
        RPMSG_ERR("creating ProcFs file debug fail\n");
        ret = -1;
        goto ERR_TAG1;
    }

    return 0;

ERR_TAG1:
    remove_proc_entry("dji/trans", NULL);
ERR_TAG0:
    return ret;
}

static void remove_proc(void)
{
    remove_proc_entry("stat", pProcTransDir);
    remove_proc_entry("debug", pProcTransDir);
    remove_proc_entry("dji/trans", NULL);
}

static int trans_init(void)
{
    if (gTransDebugFlg & RPMSG_DEBUG_SAVE_FILE)
    {
        fpUpdate = fop_open("/tmp/update_data", O_WRONLY | O_CREAT | O_TRUNC);
        if (fpUpdate == NULL)
        {
            RPMSG_ERR("Open update_data fail\n");
        }
        fpVideo = fop_open("/tmp/video_data", O_WRONLY | O_CREAT | O_TRUNC);
        if (fpVideo == NULL)
        {
            RPMSG_ERR("Open video_data fail\n");
        }
    }
    return 0;
}

static void trans_exit(void)
{
    if (fpUpdate != NULL)
    {
        fop_close(&fpUpdate);
        fpUpdate = NULL;
    }
    if (fpVideo != NULL)
    {
        fop_close(&fpVideo);
        fpVideo = NULL;
    }
}

static void rpmsg_trans_callback(struct rpmsg_channel *rpdev, void *data, int len, void *priv, u32 src)
{
    UINT32 ChnId = (UINT32)priv;

    if (ChnId < RPMSG_CHNNUM)
    {
        gstRpmsgChn[ChnId].l1stat.rx_total++;
        if (gstRpmsgChn[ChnId].l1rxproc)
        {
            gstRpmsgChn[ChnId].l1rxproc(data, len, &gstRpmsgChn[ChnId]);
        }
    }
}

static int rpmsg_trans_probe(struct rpmsg_channel *rpdev)
{
	int ret = 0;
    UINT32 ChnId;
	struct rpmsg_ns_msg nsm;

	RPMSG_INFO("%s: %s probed\n", __func__, rpdev->id.name);

    for (ChnId = 0; ChnId < sizeof(gstRpmsgChn)/sizeof(RPMSG_CHN_STRU); ChnId++)
    {
        if (!strcmp(rpdev->id.name, gstRpmsgChn[ChnId].name))
        {
            break;
        }
    }
    rpdev->ept->priv = (void *)ChnId;
    gstRpmsgChn[ChnId].rpdev = rpdev;

	nsm.addr = rpdev->dst;
	memcpy(nsm.name, rpdev->id.name, RPMSG_NAME_SIZE);
	nsm.flags = 0;
	rpmsg_send(rpdev, &nsm, sizeof(nsm));

	return ret;
}

static void rpmsg_trans_remove(struct rpmsg_channel *rpdev)
{
    RPMSG_INFO("%s: remove trans driver %s\n", __func__, gstRpmsgChn[(UINT32)rpdev->ept->priv].name);
}

static struct rpmsg_device_id rpmsg_trans_id_table[] = {
	{ .name	= RPMSG_CHNNAME0,},
	{ .name	= RPMSG_CHNNAME1,},
	{ .name	= RPMSG_CHNNAME2,},
};
MODULE_DEVICE_TABLE(rpmsg, rpmsg_trans_id_table);

static struct rpmsg_driver rpmsg_trans_driver = {
	.drv.name	= KBUILD_MODNAME,
	.drv.owner	= THIS_MODULE,
	.id_table	= rpmsg_trans_id_table,
	.probe		= rpmsg_trans_probe,
	.callback	= rpmsg_trans_callback,
	.remove		= rpmsg_trans_remove,
};

static int __init rpmsg_trans_init(void)
{
    int ret = 0;
    UINT32 b, a, s;

    printk("Debug Flag:  0x%08x\n", gTransDebugFlg);

    ret = trans_init();
    if (ret != 0)
    {
        RPMSG_ERR("init_transfer() failed!\n");
        goto ERR_TAG1;
    }

    (void)init_proc();

    ret = register_rpmsg_driver(&rpmsg_trans_driver);
    if (ret != 0)
    {
        RPMSG_ERR("register_rpmsg_driver failed!");
        goto ERR_TAG2;
    }

    ret = rpmsg_netlink_init();
    if (ret != 0)
    {
        RPMSG_ERR("rpmsg_netlink_init failed!");
        goto ERR_TAG2;
    }

    return 0;

ERR_TAG2:
    unregister_rpmsg_driver(&rpmsg_trans_driver);
ERR_TAG1:
    return ret;
}


static void __exit rpmsg_trans_exit(void)
{
    rpmsg_netlink_exit();
    unregister_rpmsg_driver(&rpmsg_trans_driver);
    remove_proc();
    trans_exit();
}


module_init(rpmsg_trans_init);
module_exit(rpmsg_trans_exit);

MODULE_DESCRIPTION("rpmsg tranfer module");
MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("Dual BSD/GPL");



