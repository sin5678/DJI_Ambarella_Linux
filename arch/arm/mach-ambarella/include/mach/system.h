/*
 * arch/arm/plat-ambarella/include/mach/system.h
 *
 * History:
 *	2006/12/27 - [Charles Chiou] created file
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

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

/* ==========================================================================*/
#include <plat/bapi.h>
#include <plat/rct.h>

#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
#include <plat/ambalink_cfg.h>
#include <mach/boss.h>
#endif

/* ==========================================================================*/
#ifndef __ASSEMBLER__

/* ==========================================================================*/
static inline void arch_idle(void)
{
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	if (boss && (BOSS_STATE_TURBO != boss->state) && !boss->force_schedule) {
		if (boss->state == BOSS_STATE_READY)
			local_irq_enable();
#if defined(CONFIG_ARM_GIC)
                amba_writel(AHB_SCRATCHPAD_REG(AHBSP_SWI_SET_OFFSET),
                            1 << (BOSS_VIRT_RIRQ_INT_VEC - AXI_SOFT_IRQ(0)));
#else
		amba_writel(AMBALINK_VIC_REG(VIC_SOFT_INT_INT_OFFSET),
			    BOSS_VIRT_RIRQ_INT_VEC % 32);
#endif  /* CONFIG_ARM_GIC */
	} else {
	        if (boss->force_schedule)
                        local_irq_enable();
		cpu_do_idle();
        }
#else   /* !CONFIG_PLAT_AMBARELLA_BOSS */
	cpu_do_idle();
#endif  /* CONFIG_PLAT_AMBARELLA_BOSS */
}

#if 0
static inline void arch_reset(char mode, const char *cmd)
{
#if defined(CONFIG_AMBARELLA_SUPPORT_BAPI)
	struct ambarella_bapi_reboot_info_s	reboot_info;

	reboot_info.magic = DEFAULT_BAPI_REBOOT_MAGIC;
	reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_NORMAL;
	if (cmd) {
		if(strcmp(cmd, "recovery") == 0) {
			reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_RECOVERY;
		} else if(strcmp(cmd, "fastboot") == 0) {
			reboot_info.mode = AMBARELLA_BAPI_CMD_REBOOT_FASTBOOT;
		}
	}
	ambarella_bapi_cmd(AMBARELLA_BAPI_CMD_SET_REBOOT_INFO, &reboot_info);
#endif

	__raw_writel(0x00, SOFT_RESET_REG);
	__raw_writel(0x01, SOFT_RESET_REG);
}
#endif
#endif /* __ASSEMBLER__ */
/* ==========================================================================*/

#endif

