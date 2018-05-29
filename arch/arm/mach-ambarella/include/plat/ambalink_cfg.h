/**
 * History:
 *    2013/10/29 - [Joey Li] created file
 *
 * Copyright (C) 2012-2014, Ambarella, Inc.
 *
 * All rights reserved. No Part of this file may be reproduced, stored
 * in a retrieval system, or transmitted, in any form, or by any means,
 * electronic, mechanical, photocopying, recording, or otherwise,
 * without the prior consent of Ambarella, Inc.
 */
#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK

#ifdef CONFIG_AMBALINK_SHMADDR

/*
 * User define
 */
#define RPMSG_NUM_BUFS                  (2048)
#define RPMSG_BUF_SIZE                  (2048)

/*
 * for RPMSG module
 */
#define RPMSG_TOTAL_BUF_SPACE           (RPMSG_NUM_BUFS * RPMSG_BUF_SIZE)
/* Alignment to 0x1000, the calculation details can be found in the document. */
#define VRING_SIZE                      ((((RPMSG_NUM_BUFS / 2) * 19 + (0x1000 - 1)) & ~(0x1000 - 1)) + \
                                        (((RPMSG_NUM_BUFS / 2) * 17 + (0x1000 - 1)) & ~(0x1000 - 1)))
#define RPC_PROFILE_SIZE                0x1000
#define RPC_RPMSG_PROFILE_SIZE          (RPC_PROFILE_SIZE + ((17 * RPMSG_NUM_BUFS + (0x1000 - 1)) & ~(0x1000 -1)))

#define VRING_C0_AND_C1_BUF             (CONFIG_AMBALINK_SHMADDR)
#define VRING_C0_TO_C1                  (VRING_C0_AND_C1_BUF + RPMSG_TOTAL_BUF_SPACE)
#define VRING_C1_TO_C0                  (VRING_C0_TO_C1 + VRING_SIZE)

#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_VIC)
#if (CHIP_REV == S3)
/* IRQ-0 is a special value in linux so skip it and use VIC_SOFT_IRQ2(3) instead. */
#define VRING_IRQ_C0_TO_C1_KICK         VIC_SOFT_IRQ2(4)
#else
#define VRING_IRQ_C0_TO_C1_KICK         VIC_SOFT_IRQ(0)
#endif
#define VRING_IRQ_C0_TO_C1_ACK          VIC_SOFT_IRQ(1)
#define VRING_IRQ_C1_TO_C0_KICK         VIC_SOFT_IRQ(2)
#define VRING_IRQ_C1_TO_C0_ACK          VIC_SOFT_IRQ(3)
#define MUTEX_IRQ_REMOTE                VIC_SOFT_IRQ(4)
#define MUTEX_IRQ_LOCAL                 VIC_SOFT_IRQ(5)
#define AMBALINK_AMP_SUSPEND_KICK       VIC_SOFT_IRQ(6)
#if (CHIP_REV == S3) || (CHIP_REV == S3L)
#define AMBALINK_VIC_REG(x)		VIC0_REG(x)
#elif (CHIP_REV == S2L) || (CHIP_REV == S2E)
#define AMBALINK_VIC_REG(x)		VIC2_REG(x)
#endif
#else
#define VRING_IRQ_C0_TO_C1_KICK         AXI_SOFT_IRQ(0)
#define VRING_IRQ_C0_TO_C1_ACK          AXI_SOFT_IRQ(1)
#define VRING_IRQ_C1_TO_C0_KICK         AXI_SOFT_IRQ(2)
#define VRING_IRQ_C1_TO_C0_ACK          AXI_SOFT_IRQ(3)
#define MUTEX_IRQ_REMOTE                AXI_SOFT_IRQ(4)
#define MUTEX_IRQ_LOCAL                 AXI_SOFT_IRQ(5)
#define AMBALINK_AMP_SUSPEND_KICK       AXI_SOFT_IRQ(6)
#endif

/*
 * for spinlock module
 */
#define AIPC_SLOCK_ADDR                 (VRING_C1_TO_C0 + VRING_SIZE)
#define AIPC_SLOCK_SIZE                 (0x1000)

/*
 * for mutex module
 */
#define AIPC_MUTEX_ADDR                 (AIPC_SLOCK_ADDR + AIPC_SLOCK_SIZE)
#define AIPC_MUTEX_SIZE                 0x1000

/*
 * for RPMSG suspend backup area
 */
#define RPMSG_SUSPEND_BACKUP_SIZE       0x20000

/*
 * for RPC and RPMSG profiling
 */
#define RPC_PROFILE_ADDR                (AIPC_MUTEX_ADDR + AIPC_MUTEX_SIZE + RPMSG_SUSPEND_BACKUP_SIZE)
#define RPMSG_PROFILE_ADDR              (RPC_PROFILE_ADDR + RPC_PROFILE_SIZE)
#if (CHIP_REV == S2)
#define PROFILE_TIMER                   TIMER6_STATUS_REG
#elif (CHIP_REV == S3)
#define PROFILE_TIMER                   TIMER17_STATUS_REG
#elif (CHIP_REV == S2L) || (CHIP_REV == S2E) || (CHIP_REV == S3L)
#define PROFILE_TIMER                   TIMER5_STATUS_REG
#endif

/*
 * general settings
 */
#define AMBALINK_CORE_LOCAL             0x2
#define ERG_SIZE                        8

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
#define ambalink_virt_to_phys(x)        ambarella_virt_to_phys(x)
#define ambalink_phys_to_virt(x)        ambarella_phys_to_virt(x)
#define ambalink_page_to_phys(x)        (page_to_phys(x) -  DEFAULT_MEM_START)
#define ambalink_phys_to_page(x)        phys_to_page((x) +  DEFAULT_MEM_START)

/* address translation in loadable kernel module. Note: Just follow the above formula without DEFAULT_MEM_START. */
#define ambalink_lkm_virt_to_phys(x)	(__pfn_to_phys(vmalloc_to_pfn((void *) (x))) + ((u32)(x) & (~PAGE_MASK)))
#else
#define ambalink_virt_to_phys(x)        (ambarella_virt_to_phys(x) -  DEFAULT_MEM_START)
#define ambalink_phys_to_virt(x)        ambarella_phys_to_virt((x) +  DEFAULT_MEM_START)
#define ambalink_page_to_phys(x)        (page_to_phys(x) -  DEFAULT_MEM_START)
#define ambalink_phys_to_page(x)        phys_to_page((x) +  DEFAULT_MEM_START)

/* address translation in loadable kernel module */
#define ambalink_lkm_virt_to_phys(x)	(__pfn_to_phys(vmalloc_to_pfn((void *) (x))) + ((u32)(x) & (~PAGE_MASK)) - DEFAULT_MEM_START)
#endif

#else
#error "CONFIG_AMBAKLINK_SHM_ADDR is not defined"
#endif

#else
#error "include ambalink_cfg.h while PLAT_AMBARELLA_AMBALINK is not defined"
#endif
