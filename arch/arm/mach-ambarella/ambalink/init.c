/*
 * arch/arm/plat-ambarella/generic/init.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
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
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/proc_fs.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/clk.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <asm/cacheflush.h>
#include <asm/system_info.h>
#include <asm/mach/map.h>
#include <mach/hardware.h>
#include <mach/init.h>
#include <plat/debug.h>
#include <plat/bapi.h>
#include <plat/clk.h>
#include <plat/ambcache.h>

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
#include <asm/cacheflush.h>

#include <linux/aipc/ipc_slock.h>
#include <linux/aipc/ipc_mutex.h>
#include <linux/proc_fs.h>

#include <mach/boss.h>
#include <plat/ambcache.h>
#include <plat/ambalink_cfg.h>

struct boss_s *boss = NULL;
EXPORT_SYMBOL(boss);
#endif

#ifdef CONFIG_RPROC_S2
extern struct platform_device ambarella_rproc_cortex_dev;
#endif
#ifdef CONFIG_AMBARELLA_HEAPMEM
extern struct platform_device amba_heapmem;
#endif
#ifdef CONFIG_AMBARELLA_DSPMEM
extern struct platform_device amba_dspmem;
#endif
/* ==========================================================================*/
u64 ambarella_dmamask = DMA_BIT_MASK(32);
EXPORT_SYMBOL(ambarella_dmamask);

u32 ambarella_debug_level = AMBA_DEBUG_NULL;
EXPORT_SYMBOL(ambarella_debug_level);

/* ==========================================================================*/
static struct platform_device *ambarella_devices[] __initdata = {
#ifdef CONFIG_RPROC_S2
	&ambarella_rproc_cortex_dev,
#endif
#ifdef CONFIG_AMBARELLA_HEAPMEM
	&amba_heapmem,
#endif
#ifdef CONFIG_AMBARELLA_DSPMEM
	&amba_dspmem,
#endif
};

enum {
        AMBARELLA_IO_DESC_AHB_ID = 0,
        AMBARELLA_IO_DESC_APB_ID,
        AMBARELLA_IO_DESC_PPM_ID,
        AMBARELLA_IO_DESC_PPM2_ID,
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_AXI)
        AMBARELLA_IO_DESC_AXI_ID,
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_DRAMC)
        AMBARELLA_IO_DESC_DRAMC_ID,
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_CRYPT)
        AMBARELLA_IO_DESC_CRYPT_ID,
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_AHB64)
        AMBARELLA_IO_DESC_AHB64_ID,
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_DBGBUS)
        AMBARELLA_IO_DESC_DBGBUS_ID,
#endif
        AMBARELLA_IO_DESC_DSP_ID,
};

struct ambarella_mem_map_desc {
	char        name[8];
	struct map_desc io_desc;
};

static struct ambarella_mem_map_desc ambarella_io_desc[] = {
	[AMBARELLA_IO_DESC_AHB_ID] = {
		.name       = "AHB",
		.io_desc    = {
			.virtual    = AHB_BASE,
			.pfn        = __phys_to_pfn(AHB_PHYS_BASE),
			.length     = AHB_SIZE,
#if defined(CONFIG_PLAT_AMBARELLA_ADD_REGISTER_LOCK)
			.type       = MT_DEVICE_NONSHARED,
#else
			.type       = MT_DEVICE,
#endif
		},
	},
	[AMBARELLA_IO_DESC_APB_ID] = {
		.name       = "APB",
		.io_desc    = {
			.virtual    = APB_BASE,
			.pfn        = __phys_to_pfn(APB_PHYS_BASE),
			.length     = APB_SIZE,
#if defined(CONFIG_PLAT_AMBARELLA_ADD_REGISTER_LOCK)
			.type       = MT_DEVICE_NONSHARED,
#else
			.type       = MT_DEVICE,
#endif
		},
	},
	[AMBARELLA_IO_DESC_PPM_ID] = {
		.name       = "PPM",    /* Private Physical Memory (shared memory) */
		.io_desc        = {
			.virtual    = CONFIG_AMBALINK_SHMADDR,
			.pfn        = __phys_to_pfn(CONFIG_AMBALINK_SHMADDR - NOLINUX_MEM_V_START),
#if (CONFIG_AMBARELLA_ZRELADDR < CONFIG_AMBALINK_SHMADDR)
			.length     = (CONFIG_AMBARELLA_ZRELADDR -
					(CONFIG_AMBALINK_SHMADDR - NOLINUX_MEM_V_START) -
					CONFIG_AMBARELLA_TEXTOFS),
#else
			.length     = (CONFIG_AMBARELLA_ZRELADDR - CONFIG_AMBALINK_SHMADDR - CONFIG_AMBARELLA_TEXTOFS),
#endif
#if defined(CONFIG_AMBARELLA_PPM_UNCACHED)
			.type       = MT_MEMORY_SHARED,
#else
			.type       = MT_MEMORY,
#endif
		},
	},
	[AMBARELLA_IO_DESC_PPM2_ID] = {
		.name       = "PPM2",   /* Private Physical Memory (RTOS memory) */
		.io_desc        = {
			.virtual    = 0,
			.pfn        = 0,
			.length     = 0,
#if defined(CONFIG_AMBARELLA_PPM_UNCACHED)
			.type       = MT_MEMORY_SHARED,
#else
			.type       = MT_MEMORY,
#endif
		},
	},
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_AXI)
	[AMBARELLA_IO_DESC_AXI_ID] = {
		.name       = "AXI",
		.io_desc    = {
			.virtual    = AXI_BASE,
			.pfn        = __phys_to_pfn(AXI_PHYS_BASE),
			.length     = AXI_SIZE,
			.type       = MT_DEVICE,
		},
	},
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_DRAMC)
	[AMBARELLA_IO_DESC_DRAMC_ID] = {
		.name       = "DRAMC",
		.io_desc    = {
			.virtual = DRAMC_BASE,
			.pfn    = __phys_to_pfn(DRAMC_PHYS_BASE),
			.length = DRAMC_SIZE,
#if defined(CONFIG_PLAT_AMBARELLA_ADD_REGISTER_LOCK)
			.type   = MT_DEVICE_NONSHARED,
#else
			.type   = MT_DEVICE,
#endif
		},
	},
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_CRYPT)
	[AMBARELLA_IO_DESC_CRYPT_ID] = {
		.name       = "CRYPT",
		.io_desc    = {
			.virtual = CRYPT_BASE,
			.pfn    = __phys_to_pfn(CRYPT_PHYS_BASE),
			.length = CRYPT_SIZE,
#if defined(CONFIG_PLAT_AMBARELLA_ADD_REGISTER_LOCK)
			.type   = MT_DEVICE_NONSHARED,
#else
			.type   = MT_DEVICE,
#endif
		},
	},
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_AHB64)
	[AMBARELLA_IO_DESC_AHB64_ID] = {
		.name       = "AHB64",
		.io_desc    = {
			.virtual = AHB64_BASE,
			.pfn    = __phys_to_pfn(AHB64_PHYS_BASE),
			.length = AHB64_SIZE,
			.type   = MT_DEVICE,
		},
	},
#endif
#if defined(CONFIG_PLAT_AMBARELLA_SUPPORT_MMAP_DBGBUS) && !defined(CONFIG_PLAT_AMBARELLA_BOSS)
	/* if running BOSS, it is already mapped in APB at RTOS side. */
	[AMBARELLA_IO_DESC_DBGBUS_ID] = {
		.name       = "DBGBUS",
		.io_desc    = {
			.virtual = DBGBUS_BASE,
			.pfn    = __phys_to_pfn(DBGBUS_PHYS_BASE),
			.length = DBGBUS_SIZE,
			.type   = MT_DEVICE,
		},
	},
#endif
	[AMBARELLA_IO_DESC_DSP_ID] = {
		.name       = "IAV",
		.io_desc    = {
			.virtual    = 0x00000000,
			.pfn        = 0x00000000,
			.length     = 0x00000000,
		},
	},
};


static int __init ambarella_dt_scan_iavmem(unsigned long node,
                                           const char *uname,  int depth, void *data)
{
	const char *type;
	__be32 *reg;
	unsigned long len;
	struct ambarella_mem_map_desc *iavmem_desc;

	type = of_get_flat_dt_prop(node, "device_type", NULL);
	if (type == NULL || strcmp(type, "iavmem") != 0)
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (WARN_ON(!reg || (len != 2 * sizeof(unsigned long))))
		return 0;

	iavmem_desc = &ambarella_io_desc[AMBARELLA_IO_DESC_DSP_ID];
	iavmem_desc->io_desc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	iavmem_desc->io_desc.length = be32_to_cpu(reg[1]);

	return 1;
}

int __init early_ambarella_dt_scan_ppm2(unsigned long node,
					const char *uname, int depth, void *data)
{
	const char *type;
	__be32 *reg;
	unsigned long len;
	struct ambarella_mem_map_desc *ppm2_desc;

	type = of_get_flat_dt_prop(node, "device_type", NULL);
	if (type == NULL || strcmp(type, "ppm2") != 0)
		return 0;

	reg = of_get_flat_dt_prop(node, "reg", &len);
	if (WARN_ON(!reg || (len != 2 * sizeof(unsigned long))))
		return 0;

#if !defined(CONFIG_PLAT_AMBARELLA_S3) || defined(CONFIG_PLAT_AMBARELLA_BOSS)
	ppm2_desc = &ambarella_io_desc[AMBARELLA_IO_DESC_PPM2_ID];
	ppm2_desc->io_desc.virtual = NOLINUX_MEM_V_START + be32_to_cpu(reg[0]);
	ppm2_desc->io_desc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	ppm2_desc->io_desc.length = be32_to_cpu(reg[1]);
#else
	/* In H1, we just map the memory we need becaue address space is not enough. */
	ppm2_desc = &ambarella_io_desc[AMBARELLA_IO_DESC_PPM2_ID];
	ppm2_desc->io_desc.virtual =
		ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.virtual - be32_to_cpu(reg[1]);
	ppm2_desc->io_desc.pfn = __phys_to_pfn(be32_to_cpu(reg[0]));
	ppm2_desc->io_desc.length = be32_to_cpu(reg[1]);
#endif

	return 1;
}

void __init ambarella_map_io(void)
{
	u32 i, iop, ios, iov;

	for (i = 0; i < AMBARELLA_IO_DESC_DSP_ID; i++) {
		iop = __pfn_to_phys(ambarella_io_desc[i].io_desc.pfn);
		ios = ambarella_io_desc[i].io_desc.length;
		iov = ambarella_io_desc[i].io_desc.virtual;
		if (ios > 0) {
			iotable_init(&(ambarella_io_desc[i].io_desc), 1);
			pr_info("Ambarella: %8s = 0x%08x[0x%08x],0x%08x %d\n",
			        ambarella_io_desc[i].name, iop, iov, ios,
			        ambarella_io_desc[i].io_desc.type);
		}
	}

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	if (boss != NULL) {
		u32 virt, phys;

		virt = boss->smem_addr;
		phys = ambarella_virt_to_phys(virt);

		pr_notice ("aipc: smem: 0x%08x [0x%08x], 0x%08x\n", virt, phys,
			    boss->smem_size);
	}
#endif

	/* scan and hold the memory information for IAV */
	of_scan_flat_dt(ambarella_dt_scan_iavmem, NULL);
}

int arch_pfn_is_nosave(unsigned long pfn)
{
#if 0
	int                 i;
#endif
	unsigned long               nosave_begin_pfn;
	unsigned long               nosave_end_pfn;

	nosave_begin_pfn =
	        ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.pfn;
	nosave_end_pfn = PFN_PHYS(nosave_begin_pfn) +
	                 ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.length;
	nosave_end_pfn = PFN_UP(nosave_end_pfn);

	if ((pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn))
		return 1;
#if 0
	for (i = 0; i < ambarella_reserve_mem_info.counter; i++) {
		nosave_begin_pfn = __phys_to_pfn(
		                           ambarella_reserve_mem_info.desc[i].physaddr);
		nosave_end_pfn = __phys_to_pfn(
		                         ambarella_reserve_mem_info.desc[i].physaddr +
		                         ambarella_reserve_mem_info.desc[i].size);
		if ((pfn >= nosave_begin_pfn) && (pfn < nosave_end_pfn))
			return 1;
	}
#endif
	return 0;
}
/* ==========================================================================*/
static struct proc_dir_entry *ambarella_proc_dir = NULL;

int __init ambarella_create_proc_dir(void)
{
	int ret_val = 0;

	ambarella_proc_dir = proc_mkdir("ambarella", NULL);
	if (!ambarella_proc_dir)
		ret_val = -ENOMEM;

	return ret_val;
}

struct proc_dir_entry *get_ambarella_proc_dir(void)
{
	return ambarella_proc_dir;
}
EXPORT_SYMBOL(get_ambarella_proc_dir);


/* ==========================================================================*/
void __init ambarella_init_machine(void)
{
	int i, ret_val = 0;

#if defined(CONFIG_PLAT_AMBARELLA_LOWER_ARM_PLL)
	amba_rct_writel(SCALER_ARM_ASYNC_REG, 0xF);
#endif

	ret_val = ambarella_create_proc_dir();
	BUG_ON(ret_val != 0);

	ret_val = ambarella_clk_init();
	BUG_ON(ret_val != 0);

	ret_val = ambarella_init_fio();
	BUG_ON(ret_val != 0);

	ret_val = ambarella_init_pm();
	BUG_ON(ret_val != 0);

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
	ret_val = ambarella_init_fb();
	BUG_ON(ret_val != 0);

	ret_val = ambarella_init_audio();
	BUG_ON(ret_val != 0);
#endif

#ifdef CONFIG_OUTER_CACHE
	ambcache_l2_enable();
#endif

	platform_add_devices(ambarella_devices, ARRAY_SIZE(ambarella_devices));
	for (i = 0; i < ARRAY_SIZE(ambarella_devices); i++) {
		device_set_wakeup_capable(&ambarella_devices[i]->dev, 1);
		device_set_wakeup_enable(&ambarella_devices[i]->dev, 0);
	}

	of_platform_populate(NULL, of_default_bus_match_table, NULL, NULL);
}

void ambarella_restart_machine(char mode, const char *cmd)
{
#if defined(CONFIG_AMBARELLA_SUPPORT_BAPI)
	struct ambarella_bapi_reboot_info_s reboot_info;

	reboot_info.magic = DEFAULT_BAPI_REBOOT_MAGIC;
	reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_NORMAL;
	if (cmd) {
		if (strcmp(cmd, "recovery") == 0) {
			reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_RECOVERY;
		} else if (strcmp(cmd, "fastboot") == 0) {
			reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_FASTBOOT;
		}
	}
	ambarella_bapi_cmd(AMBARELLA_BAPI_CMD_SET_REBOOT_INFO, &reboot_info);
#endif

	local_irq_disable();
	local_fiq_disable();
	flush_cache_all();

#if defined(CONFIG_PLAT_AMBARELLA_S2) || defined(CONFIG_PLAT_AMBARELLA_S2)
    /* Disconnect USB before reboot. */
    amba_rct_setbitsl(ANA_PWR_REG, 0x2);
    amba_setbitsl(USB_DEV_CTRL_REG, USB_DEV_SOFT_DISCON);
    amba_rct_clrbitsl(ANA_PWR_REG, 0x6);
#endif

	amba_rct_writel(SOFT_OR_DLL_RESET_REG, 0x2);
	amba_rct_writel(SOFT_OR_DLL_RESET_REG, 0x3);
}

/* ==========================================================================*/

static int __init parse_system_revision(char *p)
{
	system_rev = simple_strtoul(p, NULL, 0);
	return 0;
}
early_param("system_rev", parse_system_revision);

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
static int __init early_boss(char *p)
{
       unsigned long vaddr, paddr;
       char *endp;

       vaddr = memparse(p, &endp);
       paddr = ambarella_virt_to_phys(vaddr);

       printk (KERN_NOTICE "boss: %08lx [%08lx]\n", vaddr, paddr);
#if 0
       printk (KERN_NOTICE "boss: printk: %08x [%08x], %08x [%08x], %08x [%08x]\n",
               (u32) boss_log_buf_ptr, ipc_virt_to_phys (boss_log_buf_ptr),
               (u32) boss_log_buf_len_ptr, ipc_virt_to_phys (boss_log_buf_len_ptr),
               (u32) boss_log_buf_last_ptr, ipc_virt_to_phys (boss_log_buf_last_ptr));
#endif
       boss = (struct boss_s *) vaddr;

       aipc_spin_lock_setup(AIPC_SLOCK_ADDR);

       return 0;
}
early_param("boss", early_boss);
#endif

#if defined(CONFIG_PLAT_AMBARELLA_BOSS) || defined(CONFIG_PLAT_AMBARELLA_AMBALINK)
char wifi_mac[20];
EXPORT_SYMBOL(wifi_mac);
static int __init early_wifi_mac(char *p)
{
	strncpy(wifi_mac, p, strlen("00:00:00:00:00:00"));
	wifi_mac[strlen("00:00:00:00:00:00")] = '\0';

	return 0;
}
early_param("wifi_mac", early_wifi_mac);

char bt_mac[20];
EXPORT_SYMBOL(bt_mac);
static int __init early_bt_mac(char *p)
{
	strncpy(bt_mac, p, strlen("00:00:00:00:00:00"));
	bt_mac[strlen("00:00:00:00:00:00")] = '\0';

	return 0;
}
early_param("bt_mac", early_bt_mac);

static int ambarella_atags_proc_read(struct seq_file *m, void *v)
{
	int retlen = 0;

	/* keep the the same name as old platform */
	retlen += seq_printf(m, "wifi_mac: %s\n", wifi_mac);
	retlen += seq_printf(m, "wifi1_mac: %s\n", bt_mac);
	return retlen;
}

static int ambarella_atags_proc_open(struct inode *inode, struct file *file)
{
	return single_open(file, ambarella_atags_proc_read, NULL);
}

static const struct file_operations atags_ambarella_proc_fops = {
	.open = ambarella_atags_proc_open,
	.read = seq_read,
	.llseek	= seq_lseek,
	.release = single_release,
};

static int __init atags_ambarella_proc_init(void)
{
	/* keep the name board_info to old platform */
	proc_create("board_info", S_IRUGO, get_ambarella_proc_dir(),
		&atags_ambarella_proc_fops);

	return 0;
}
late_initcall(atags_ambarella_proc_init);
#endif

u32 ambarella_phys_to_virt(u32 paddr)
{
	int                 i;
	u32                 phystart;
	u32                 phylength;
	u32                 phyoffset;
	u32                 vstart;

	for (i = 0; i < ARRAY_SIZE(ambarella_io_desc); i++) {
		phystart = __pfn_to_phys(ambarella_io_desc[i].io_desc.pfn);
		phylength = ambarella_io_desc[i].io_desc.length;
		vstart = ambarella_io_desc[i].io_desc.virtual;
		if ((paddr >= phystart) && (paddr < (phystart + phylength))) {
			phyoffset = paddr - phystart;
			return (vstart + phyoffset);
		}
	}

	return __amb_raw_phys_to_virt(paddr);
}
EXPORT_SYMBOL(ambarella_phys_to_virt);

u32 ambarella_virt_to_phys(u32 vaddr)
{
	int                 i;
	u32                 phystart;
	u32                 vlength;
	u32                 voffset;
	u32                 vstart;

	for (i = 0; i < ARRAY_SIZE(ambarella_io_desc); i++) {
		phystart = __pfn_to_phys(ambarella_io_desc[i].io_desc.pfn);
		vlength = ambarella_io_desc[i].io_desc.length;
		vstart = ambarella_io_desc[i].io_desc.virtual;
		if ((vaddr >= vstart) && (vaddr < (vstart + vlength))) {
			voffset = vaddr - vstart;
			return (phystart + voffset);
		}
	}

	return __amb_raw_virt_to_phys(vaddr);
}
EXPORT_SYMBOL(ambarella_virt_to_phys);

u32 get_ambarella_iavmem_phys(void)
{
	return __pfn_to_phys(
	               ambarella_io_desc[AMBARELLA_IO_DESC_DSP_ID].io_desc.pfn);
}
EXPORT_SYMBOL(get_ambarella_iavmem_phys);

u32 get_ambarella_iavmem_size(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_DSP_ID].io_desc.length;
}
EXPORT_SYMBOL(get_ambarella_iavmem_size);

u32 get_ambarella_ppm_phys(void)
{
	return __pfn_to_phys(
	               ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.pfn);
}
EXPORT_SYMBOL(get_ambarella_ppm_phys);

u32 get_ambarella_ppm_virt(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.virtual;
}
EXPORT_SYMBOL(get_ambarella_ppm_virt);

u32 get_ambarella_ppm_size(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_PPM_ID].io_desc.length;
}
EXPORT_SYMBOL(get_ambarella_ppm_size);

u32 get_ambarella_ahb_phys(void)
{
	return __pfn_to_phys(
	               ambarella_io_desc[AMBARELLA_IO_DESC_AHB_ID].io_desc.pfn);
}
EXPORT_SYMBOL(get_ambarella_ahb_phys);

u32 get_ambarella_ahb_virt(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_AHB_ID].io_desc.virtual;
}
EXPORT_SYMBOL(get_ambarella_ahb_virt);

u32 get_ambarella_ahb_size(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_AHB_ID].io_desc.length;
}
EXPORT_SYMBOL(get_ambarella_ahb_size);

u32 get_ambarella_apb_phys(void)
{
	return __pfn_to_phys(
	               ambarella_io_desc[AMBARELLA_IO_DESC_APB_ID].io_desc.pfn);
}
EXPORT_SYMBOL(get_ambarella_apb_phys);

u32 get_ambarella_apb_virt(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_APB_ID].io_desc.virtual;
}
EXPORT_SYMBOL(get_ambarella_apb_virt);

u32 get_ambarella_apb_size(void)
{
	return ambarella_io_desc[AMBARELLA_IO_DESC_APB_ID].io_desc.length;
}
EXPORT_SYMBOL(get_ambarella_apb_size);

u32 ambarella_get_poc(void)
{
	return amba_rct_readl(SYS_CONFIG_REG);
}
EXPORT_SYMBOL(ambarella_get_poc);


