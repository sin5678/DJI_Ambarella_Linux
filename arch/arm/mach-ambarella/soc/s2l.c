/*
 * arch/arm/mach-ambarella/init-ixora.c
 *
 * Author: Anthony Ginger <hfjiang@ambarella.com>
 *
 * Copyright (C) 2004-2013, Ambarella, Inc.
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
#include <linux/of_platform.h>
#include <linux/irqchip.h>
#include <asm/mach/arch.h>
#include <asm/io.h>
#include <asm/cacheflush.h>

#include <mach/init.h>
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
#include <mach/io.h>
#include <mach/boss.h>
#endif

static void __init ambarella_ambalink_init_irq(void)
{
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
        /*
         * We should fix up the Linux vectors so that it knows how to properly
         * jump to Linux vectors.
         */
        unsigned long vectors = CONFIG_VECTORS_BASE;
        unsigned int linst;
        unsigned int off;
        register unsigned int x;

        /* Fix entries in backup and the one installed by early_tap_init() */
        for (off = 0x20; off < 0x40; off += 4) {
                linst = amba_readl((void *) vectors + off);

                /* Need to modify the address field by subtracting 0x20 */
                /* Just assume that the instructions installed in Linux */
                /* vector that need modification are either LDR or B */
                if ((linst & 0x0f000000) == 0x0a000000) {   /* B */
                        linst -= 0x8;
                }
                if ((linst & 0x0ff00000) == 0x05900000) {   /* LDR */
                        linst -= 0x20;
                }

                amba_writel((void *) vectors + off, linst);
        }

        /* Install addresses for our vector to BOSS */
        for (off = 0; off < 0x20; off += 4) {
                amba_writel((void *) vectors + off + 0x1020,
                            vectors + off + 0x20);
        }

        clean_dcache_area((void *) vectors + 0x1000, PAGE_SIZE);

        flush_icache_range(vectors, vectors + PAGE_SIZE);

        /* Set TTBR0 as cacheable (cacheable, inner WB not shareable, outer WB not shareable). */
        x = 0x0;
        asm volatile("mrc   p15, 0, %0, c2, c0, 0": "=r" (x));
        x |= 0x59;
        asm volatile("mcr   p15, 0, %0, c2, c0, 0": : "r" (x));
#endif

	irqchip_init();

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	boss_set_ready(1);
#endif
}


static const char * const s2l_dt_board_compat[] = {
	"ambarella,s2l",
	NULL,
};

DT_MACHINE_START(S2L_DT, "Ambarella S2L (Flattened Device Tree)")
	.restart_mode	= 's',
	.map_io		= ambarella_map_io,
	.init_early	= ambarella_init_early,
	.init_irq	= ambarella_ambalink_init_irq,
	.init_time	= ambarella_timer_init,
	.init_machine	= ambarella_init_machine,
	.restart	= ambarella_restart_machine,
	.dt_compat	= s2l_dt_board_compat,
MACHINE_END

