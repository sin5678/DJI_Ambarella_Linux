/*
 * drivers/irqchip/irq-ambarella.c
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
#include <linux/init.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/interrupt.h>
#include <linux/syscore_ops.h>
#include <asm/exception.h>
#include "irqchip.h"

#include <plat/rct.h>

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
#include <plat/ambalink_cfg.h>
#endif
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
#include <mach/boss.h>
#endif

#define HWIRQ_TO_BANK(hwirq)	((hwirq) >> 5)
#define HWIRQ_TO_OFFSET(hwirq)	((hwirq) & 0x1f)

struct ambvic_chip_data {
	void __iomem *reg_base[VIC_INSTANCES];
	void __iomem *ipi_reg_base;
	struct irq_domain *domain;
};

#ifdef CONFIG_SMP

static DEFINE_RAW_SPINLOCK(irq_controller_lock);

#define IPI0_IRQ_MASK		((1 << (IPI01_IRQ % 32)) | \
				 (1 << (IPI02_IRQ % 32)) | \
				 (1 << (IPI03_IRQ % 32)) | \
				 (1 << (IPI04_IRQ % 32)) | \
				 (1 << (IPI05_IRQ % 32)) | \
				 (1 << (IPI06_IRQ % 32)))
#define IPI1_IRQ_MASK		((1 << (IPI11_IRQ % 32)) | \
				 (1 << (IPI12_IRQ % 32)) | \
				 (1 << (IPI13_IRQ % 32)) | \
				 (1 << (IPI14_IRQ % 32)) | \
				 (1 << (IPI15_IRQ % 32)) | \
				 (1 << (IPI16_IRQ % 32)))
#define IPI_IRQ_MASK		(IPI0_IRQ_MASK | IPI1_IRQ_MASK)

#define AMBVIC_IRQ_IS_IPI0(irq)	(((irq) >= IPI01_IRQ && (irq) <= IPI06_IRQ))
#define AMBVIC_IRQ_IS_IPI1(irq)	(((irq) >= IPI11_IRQ && (irq) <= IPI16_IRQ))
#define AMBVIC_IRQ_IS_IPI(irq)	(AMBVIC_IRQ_IS_IPI0(irq) || AMBVIC_IRQ_IS_IPI1(irq))

#endif

static struct ambvic_chip_data ambvic_data __read_mostly;

#if defined(CONFIG_PLAT_AMBARELLA_AMBALINK)
u32 vic_linux_only[4] = {0};
#endif

/* ==========================================================================*/
#if (VIC_SUPPORT_CPU_OFFLOAD >= 1)

static void ambvic_ack_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

#if (VIC_SUPPORT_CPU_OFFLOAD == 1)
	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);
#else
	amba_writel(reg_base + VIC_INT_EDGE_CLR_OFFSET, offset);
#endif
}

static void ambvic_mask_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	if (boss_get_irq_owner(data->hwirq) != BOSS_IRQ_OWNER_LINUX)
		return;
#endif

	amba_writel(reg_base + VIC_INT_EN_CLR_INT_OFFSET, offset);

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	/* Disable in BOSS. */
	boss_disable_irq(data->hwirq);
#endif
}

static void ambvic_unmask_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);
#if defined(CONFIG_PLAT_AMBARELLA_AMBALINK)
	u32 mask = 1 << HWIRQ_TO_OFFSET(data->hwirq);
	u32 val0;
#endif

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	/* If DMA IRQ owner is pre-set to RTOS at ambdma_dma_irq_handler,
	   we cannot skip irq enable */
#define UART_DMA_IRQ 0xf
	if ((boss_get_irq_owner(data->hwirq) != BOSS_IRQ_OWNER_LINUX) &&
		(UART_DMA_IRQ != data->hwirq))
		return;

	/* Enable in BOSS. */
	boss_enable_irq(data->hwirq);
#endif

#if defined(CONFIG_PLAT_AMBARELLA_AMBALINK)
	/* Saved the IRQ used in Linux for resuming purpose. */
	vic_linux_only[HWIRQ_TO_BANK(data->hwirq)] |= mask;

	/* Using IRQ for Linux. */
	val0 = amba_readl(reg_base + VIC_INT_SEL_OFFSET);
	val0 &= ~mask;
	amba_writel(reg_base + VIC_INT_SEL_OFFSET, val0);

#if defined(CONFIG_AMBALINK_MULTIPLE_CORE) && !defined(CONFIG_PLAT_AMBARELLA_BOSS)
	/*
	 * We need to set the interrupt target.
	 * Actually, this work can be done by irq_set_affinity(),
	 * but the API is SMP related in kernel.
	 * So we use irq_enable/disable to achieve this work.
	 */
	{
		u32 val1;

		val0 = amba_readl(reg_base + VIC_INT_PTR0_OFFSET);
		val1 = amba_readl(reg_base + VIC_INT_PTR1_OFFSET);

		val0 &= ~mask;
		val1 |= mask;

		amba_writel(reg_base + VIC_INT_PTR0_OFFSET, val0);
		amba_writel(reg_base + VIC_INT_PTR1_OFFSET, val1);
	}
#endif
#endif

	amba_writel(reg_base + VIC_INT_EN_INT_OFFSET, offset);
}

static void ambvic_mask_ack_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	amba_writel(reg_base + VIC_INT_EN_CLR_INT_OFFSET, offset);
#if (VIC_SUPPORT_CPU_OFFLOAD == 1)
	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);
#else
	amba_writel(reg_base + VIC_INT_EDGE_CLR_OFFSET, offset);
#endif

#ifdef CONFIG_PLAT_AMBARELLA_BOSS
	/* Disable in BOSS. */
	boss_disable_irq(data->hwirq);
#endif
}

static int ambvic_set_type_irq(struct irq_data *data, unsigned int type)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	struct irq_desc *desc = irq_to_desc(data->irq);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		amba_writel(reg_base + VIC_INT_SENSE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_BOTHEDGE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_EVT_INT_OFFSET, offset);
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		amba_writel(reg_base + VIC_INT_SENSE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_BOTHEDGE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_EVT_CLR_INT_OFFSET, offset);
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		amba_writel(reg_base + VIC_INT_SENSE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_BOTHEDGE_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_EVT_CLR_INT_OFFSET, offset);
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		amba_writel(reg_base + VIC_INT_SENSE_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_BOTHEDGE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_EVT_INT_OFFSET, offset);
		desc->handle_irq = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		amba_writel(reg_base + VIC_INT_SENSE_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_BOTHEDGE_CLR_INT_OFFSET, offset);
		amba_writel(reg_base + VIC_INT_EVT_CLR_INT_OFFSET, offset);
		desc->handle_irq = handle_level_irq;
		break;
	default:
		pr_err("%s: irq[%d] type[%d] fail!\n",
			__func__, data->irq, type);
		return -EINVAL;
	}

	/* clear obsolete irq */
#if (VIC_SUPPORT_CPU_OFFLOAD == 1)
	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);
#else
	amba_writel(reg_base + VIC_INT_EDGE_CLR_OFFSET, offset);
#endif

	return 0;
}

#else
static void ambvic_ack_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);
}

static void ambvic_mask_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	amba_writel(reg_base + VIC_INTEN_CLR_OFFSET, 0x1 << offset);
}

static void ambvic_unmask_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	amba_writel(reg_base + VIC_INTEN_OFFSET, 0x1 << offset);
}

static void ambvic_mask_ack_irq(struct irq_data *data)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);

	amba_writel(reg_base + VIC_INTEN_CLR_OFFSET, 0x1 << offset);
	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);
}

static int ambvic_set_type_irq(struct irq_data *data, unsigned int type)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	struct irq_desc *desc = irq_to_desc(data->irq);
	u32 offset = HWIRQ_TO_OFFSET(data->hwirq);
	u32 mask, bit, sense, bothedges, event;

	mask = ~(0x1 << offset);
	bit = (0x1 << offset);
	sense = amba_readl(reg_base + VIC_SENSE_OFFSET);
	bothedges = amba_readl(reg_base + VIC_BOTHEDGE_OFFSET);
	event = amba_readl(reg_base + VIC_EVENT_OFFSET);
	switch (type) {
	case IRQ_TYPE_EDGE_RISING:
		sense &= mask;
		bothedges &= mask;
		event |= bit;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_FALLING:
		sense &= mask;
		bothedges &= mask;
		event &= mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_EDGE_BOTH:
		sense &= mask;
		bothedges |= bit;
		event &= mask;
		desc->handle_irq = handle_edge_irq;
		break;
	case IRQ_TYPE_LEVEL_HIGH:
		sense |= bit;
		bothedges &= mask;
		event |= bit;
		desc->handle_irq = handle_level_irq;
		break;
	case IRQ_TYPE_LEVEL_LOW:
		sense |= bit;
		bothedges &= mask;
		event &= mask;
		desc->handle_irq = handle_level_irq;
		break;
	default:
		pr_err("%s: irq[%d] type[%d] fail!\n",
			__func__, data->irq, type);
		return -EINVAL;
	}

	amba_writel(reg_base + VIC_SENSE_OFFSET, sense);
	amba_writel(reg_base + VIC_BOTHEDGE_OFFSET, bothedges);
	amba_writel(reg_base + VIC_EVENT_OFFSET, event);
	/* clear obsolete irq */
	amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0x1 << offset);

	return 0;
}

#endif

#ifdef CONFIG_SMP
static int ambvic_set_affinity(struct irq_data *data,
		const struct cpumask *mask_val, bool force)
{
	void __iomem *reg_base = irq_data_get_irq_chip_data(data);
	u32 cpu = cpumask_any_and(mask_val, cpu_online_mask);
	u32 mask = 1 << HWIRQ_TO_OFFSET(data->hwirq);
	u32 val0, val1;

	if (cpu >= nr_cpu_ids)
		return -EINVAL;

	raw_spin_lock(&irq_controller_lock);

	val0 = amba_readl(reg_base + VIC_INT_PTR0_OFFSET);
	val1 = amba_readl(reg_base + VIC_INT_PTR1_OFFSET);

	if (cpu == 0) {
		val0 |= mask;
		val1 &= ~mask;
	} else {
		val0 &= ~mask;
		val1 |= mask;
	}

	amba_writel(reg_base + VIC_INT_PTR0_OFFSET, val0);
	amba_writel(reg_base + VIC_INT_PTR1_OFFSET, val1);

	raw_spin_unlock(&irq_controller_lock);

	return 0;
}
#endif

static struct irq_chip ambvic_chip = {
	.name		= "VIC",
	.irq_ack	= ambvic_ack_irq,
	.irq_mask	= ambvic_mask_irq,
	.irq_mask_ack	= ambvic_mask_ack_irq,
	.irq_unmask	= ambvic_unmask_irq,
	.irq_set_type	= ambvic_set_type_irq,
#ifdef CONFIG_SMP
	.irq_set_affinity = ambvic_set_affinity,
#endif
#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
	.flags		= (IRQCHIP_SET_TYPE_MASKED | IRQCHIP_MASK_ON_SUSPEND |
			IRQCHIP_SKIP_SET_WAKE),
#else
	.flags		= (IRQCHIP_MASK_ON_SUSPEND | IRQCHIP_SKIP_SET_WAKE),
#endif
};

/* ==========================================================================*/
#ifdef CONFIG_SMP
static void ambvic_raise_softirq(const struct cpumask *mask, unsigned int irq)
{
	int cpu, softirq;

	/* currently we only support dual cores, so we just pick the
	 * first cpu we find in 'mask'.. */
	cpu = cpumask_any(mask);

	/*
	 * Ensure that stores to Normal memory are visible to the
	 * other CPUs before issuing the IPI.
	 */
	dsb();

	/* submit softirq */
	softirq = irq + ((cpu == 0) ? (IPI01_IRQ % 32) : (IPI11_IRQ % 32));
	ambvic_sw_set(ambvic_data.ipi_reg_base, softirq);
}

static void ambvic_smp_softirq_init(void)
{
	void __iomem *reg_base = ambvic_data.ipi_reg_base;
	u32 val;

	raw_spin_lock(&irq_controller_lock);

	/* Clear pending IPIs */
	amba_writel(reg_base + VIC_SOFTEN_CLR_OFFSET, IPI_IRQ_MASK);

	/* Enable all of IPIs */
	val = amba_readl(reg_base + VIC_INT_PTR0_OFFSET);
	val |= IPI0_IRQ_MASK;
	val &= ~IPI1_IRQ_MASK;
	amba_writel(reg_base + VIC_INT_PTR0_OFFSET, val);

	val = amba_readl(reg_base + VIC_INT_PTR1_OFFSET);
	val |= IPI1_IRQ_MASK;
	val &= ~IPI0_IRQ_MASK;
	amba_writel(reg_base + VIC_INT_PTR1_OFFSET, val);

	amba_writel(reg_base + VIC_SENSE_OFFSET, IPI_IRQ_MASK);
	amba_writel(reg_base + VIC_EVENT_OFFSET, IPI_IRQ_MASK);
	amba_writel(reg_base + VIC_INTEN_OFFSET, IPI_IRQ_MASK);

	raw_spin_unlock(&irq_controller_lock);
}
#endif

static inline int ambvic_handle_ipi(struct pt_regs *regs,
		struct irq_domain *domain, u32 hwirq)
{
#ifdef CONFIG_SMP
	/* IPI Handling */
	if (AMBVIC_IRQ_IS_IPI(hwirq)) {
		//printk("***********  hwirq = %d\n", hwirq);
		ambvic_sw_clr(ambvic_data.ipi_reg_base, hwirq % 32);
		if (AMBVIC_IRQ_IS_IPI0(hwirq))
			handle_IPI(hwirq - IPI01_IRQ, regs);
		else
			handle_IPI(hwirq - IPI11_IRQ, regs);

		return 1;
	}
#endif
	return 0;
}

#if (VIC_SUPPORT_CPU_OFFLOAD >= 2)
static int ambvic_handle_scratchpad_vic(struct pt_regs *regs,
	struct irq_domain *domain)
{
	u32 scratchpad;
	u32 irq, hwirq, irq_sta;
	int handled = 0;

#if defined(CONFIG_PLAT_AMBARELLA_AMBALINK) && defined(CONFIG_AMBALINK_MULTIPLE_CORE)
	/* Interrupt is ent to Core-1 in multiple core AmbaLink. */
	scratchpad = AHB_SCRATCHPAD_REG(0x40);
#else
	if (smp_processor_id()) {
		scratchpad = AHBSP_PRI_IRQ_C1_REG;
	} else {
		scratchpad = AHBSP_PRI_IRQ_C0_REG;
	}
#endif

	do {
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
		hwirq = *boss->irqno;
#else
		hwirq = amba_readl(scratchpad);
#endif
		if (hwirq == VIC_NULL_PRI_IRQ_VAL) {
#if (VIC_NULL_PRI_IRQ_FIX == 1)
			void __iomem *reg_base = ambvic_data.reg_base[2];
			if ((amba_readl(reg_base + VIC_IRQ_STA_OFFSET) & 0x1) == 0)
				break;
#else
			break;
#endif
                } else if (hwirq == 0) {
                        irq_sta = amba_readl(ambvic_data.reg_base[0] + VIC_IRQ_STA_OFFSET);
                        if ((irq_sta & 0x1) == 0)
                           break;
                }
#if 0
		printk("CPU%d_%s: %d_%d\n",
			smp_processor_id(), __func__,
			(hwirq >> 5) & 0x3, hwirq & 0x1F);
#endif
		if (ambvic_handle_ipi(regs, domain, hwirq)) {
			handled = 1;
			continue;
		}

		irq = irq_find_mapping(domain, hwirq);
		handle_IRQ(irq, regs);
		handled = 1;
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
		/* Linux will continue to process interrupt, */
		/* but we must enter IRQ from AmbaBoss_Irq in BOSS system. */
		break;
#endif
	} while (1);

	return handled;
}
#else
static int ambvic_handle_one(struct pt_regs *regs,
		struct irq_domain *domain, u32 bank)
{
#ifndef CONFIG_PLAT_AMBARELLA_BOSS
	void __iomem *reg_base = ambvic_data.reg_base[bank];
	u32 irq_sta;
#endif
	u32 irq;
	u32 hwirq;
	int handled = 0;

	do {
#if (VIC_SUPPORT_CPU_OFFLOAD == 1)
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
		hwirq = *boss->irqno;
		if (hwirq == 0) {
			break;
		}
#else
		hwirq = amba_readl(reg_base + VIC_INT_PENDING_OFFSET);
		if (hwirq == 0) {
			irq_sta = amba_readl(reg_base + VIC_IRQ_STA_OFFSET);
			if ((irq_sta & 0x00000001) == 0) {
				break;
			}
		}
#endif
#else
		irq_sta = amba_readl(reg_base + VIC_IRQ_STA_OFFSET);
		if (irq_sta == 0) {
			break;
		}
		hwirq = ffs(irq_sta) - 1;
#endif
		hwirq += bank * NR_VIC_IRQ_SIZE;

		if (ambvic_handle_ipi(regs, domain, hwirq)) {
			handled = 1;
			continue;
		}

		irq = irq_find_mapping(domain, hwirq);
		handle_IRQ(irq, regs);
		handled = 1;
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
		/* Linux will continue to process interrupt, */
		/* but we must enter IRQ from AmbaBoss_Irq in BOSS system. */
		break;
#endif
	} while (1);

	return handled;
}
#endif

static asmlinkage void __exception_irq_entry ambvic_handle_irq(struct pt_regs *regs)
{
#ifdef CONFIG_PLAT_AMBARELLA_BOSS
#if (VIC_SUPPORT_CPU_OFFLOAD >= 2)
	ambvic_handle_scratchpad_vic(regs, ambvic_data.domain);
#else
	ambvic_handle_one(regs, ambvic_data.domain, 0);
#endif
#else
	int handled;

	do {
		handled = 0;
#if (VIC_SUPPORT_CPU_OFFLOAD >= 2)
		handled = ambvic_handle_scratchpad_vic(regs, ambvic_data.domain);
#else
{
		int i;

		for (i = 0; i < VIC_INSTANCES; i++) {
			handled |= ambvic_handle_one(regs, ambvic_data.domain, i);
		}
}
#endif
	} while (handled);
#endif
}

static int ambvic_irq_domain_map(struct irq_domain *d,
		unsigned int irq, irq_hw_number_t hwirq)
{
	if (hwirq > NR_VIC_IRQS)
		return -EPERM;

	irq_set_chip_and_handler(irq, &ambvic_chip, handle_level_irq);
	irq_set_chip_data(irq, ambvic_data.reg_base[HWIRQ_TO_BANK(hwirq)]);
	set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);

	return 0;
}

static struct irq_domain_ops amb_irq_domain_ops = {
	.map = ambvic_irq_domain_map,
	.xlate = irq_domain_xlate_twocell,
};


#if defined(CONFIG_PM)

struct ambvic_pm_reg {
	u32 int_sel_reg;
	u32 inten_reg;
	u32 soften_reg;
	u32 proten_reg;
	u32 sense_reg;
	u32 bothedge_reg;
	u32 event_reg;
	u32 int_ptr0_reg;
	u32 int_ptr1_reg;
};

static struct ambvic_pm_reg ambvic_pm[VIC_INSTANCES];

static int ambvic_suspend(void)
{
	struct ambvic_pm_reg *pm_val;
	void __iomem *reg_base;
	int i;

	for (i = 0; i < VIC_INSTANCES; i++) {
		reg_base = ambvic_data.reg_base[i];
		pm_val = &ambvic_pm[i];

		pm_val->int_sel_reg = amba_readl(reg_base + VIC_INT_SEL_OFFSET);
		pm_val->inten_reg = amba_readl(reg_base + VIC_INTEN_OFFSET);
		pm_val->soften_reg = amba_readl(reg_base + VIC_SOFTEN_OFFSET);
		pm_val->proten_reg = amba_readl(reg_base + VIC_PROTEN_OFFSET);
		pm_val->sense_reg = amba_readl(reg_base + VIC_SENSE_OFFSET);
		pm_val->bothedge_reg = amba_readl(reg_base + VIC_BOTHEDGE_OFFSET);
		pm_val->event_reg = amba_readl(reg_base + VIC_EVENT_OFFSET);
		pm_val->int_ptr0_reg = amba_readl(reg_base + VIC_INT_PTR0_OFFSET);
		pm_val->int_ptr1_reg = amba_readl(reg_base + VIC_INT_PTR1_OFFSET);
	}

	return 0;
}

static void ambvic_resume(void)
{
	struct ambvic_pm_reg *pm_val;
	void __iomem *reg_base;
	int i;

#ifdef CONFIG_PLAT_AMBARELLA_AMBALINK
	u32 cur_val, resume_mask, resume_val;
	/* Skip shared IRQs because RTOS may access the resource at the same time. */
	/* NAND related IRQs, SD IRQ, DMA IRQ. */
	cur_val = 1 << HWIRQ_TO_OFFSET(FIOCMD_IRQ);
	vic_linux_only[HWIRQ_TO_BANK(FIOCMD_IRQ)] &= ~cur_val;
	cur_val = 1 << HWIRQ_TO_OFFSET(FIODMA_IRQ);
	vic_linux_only[HWIRQ_TO_BANK(FIODMA_IRQ)] &= ~cur_val;
	cur_val = 1 << HWIRQ_TO_OFFSET(DMA_FIOS_IRQ);
	vic_linux_only[HWIRQ_TO_BANK(DMA_FIOS_IRQ)] &= ~cur_val;
	cur_val = 1 << HWIRQ_TO_OFFSET(SD_IRQ);
	vic_linux_only[HWIRQ_TO_BANK(SD_IRQ)] &= ~cur_val;
	cur_val = 1 << HWIRQ_TO_OFFSET(DMA_IRQ);
	vic_linux_only[HWIRQ_TO_BANK(DMA_IRQ)] &= ~cur_val;

	for (i = VIC_INSTANCES - 1; i >= 0; i--) {
		reg_base = ambvic_data.reg_base[i];
		pm_val = &ambvic_pm[i];
		resume_mask = vic_linux_only[i];

		/* Only restore the setting of Linux's driver.	*/
		/* Read back and clear Linux's bit then OR the Linux only suspended value. */

		cur_val = amba_readl(reg_base + VIC_INT_SEL_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->int_sel_reg & resume_mask);
		amba_writel(reg_base + VIC_INT_SEL_OFFSET, resume_val);

		amba_writel(reg_base + VIC_INTEN_CLR_OFFSET, (0xffffffff & resume_mask));
		amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, (0xffffffff & resume_mask));

		cur_val = amba_readl(reg_base + VIC_INTEN_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->inten_reg & resume_mask);
		amba_writel(reg_base + VIC_INTEN_OFFSET, resume_val);

#if 0
		/* do not clear SOFT IRQ because RTOS may send a wake up IRQ to Linux. */
		amba_writel(reg_base + VIC_SOFTEN_CLR_OFFSET, (0xffffffff & resume_mask));
#endif

		cur_val = amba_readl(reg_base + VIC_SOFTEN_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->soften_reg & resume_mask);
		amba_writel(reg_base + VIC_SOFTEN_OFFSET, resume_val);

		cur_val = amba_readl(reg_base + VIC_PROTEN_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->proten_reg & resume_mask);
		amba_writel(reg_base + VIC_PROTEN_OFFSET, resume_val);

		cur_val = amba_readl(reg_base + VIC_SENSE_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->sense_reg & resume_mask);
		amba_writel(reg_base + VIC_SENSE_OFFSET, resume_val);

		cur_val = amba_readl(reg_base + VIC_BOTHEDGE_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->bothedge_reg & resume_mask);
		amba_writel(reg_base + VIC_BOTHEDGE_OFFSET, resume_val);

		cur_val = amba_readl(reg_base + VIC_EVENT_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->event_reg & resume_mask);
		amba_writel(reg_base + VIC_EVENT_OFFSET, resume_val);

#if defined(CONFIG_AMBALINK_MULTIPLE_CORE) && !defined(CONFIG_PLAT_AMBARELLA_BOSS)
		cur_val = amba_readl(reg_base + VIC_INT_PTR0_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->int_ptr0_reg & resume_mask);
		amba_writel(reg_base + VIC_INT_PTR0_OFFSET, resume_val);

		cur_val = amba_readl(reg_base + VIC_INT_PTR1_OFFSET);
		resume_val = (cur_val & ~resume_mask) | (pm_val->int_ptr1_reg & resume_mask);
		amba_writel(reg_base + VIC_INT_PTR1_OFFSET, resume_val);
#endif
	}
#else
	for (i = VIC_INSTANCES - 1; i >= 0; i--) {
		reg_base = ambvic_data.reg_base[i];
		pm_val = &ambvic_pm[i];

		amba_writel(reg_base + VIC_INT_SEL_OFFSET, pm_val->int_sel_reg);
		amba_writel(reg_base + VIC_INTEN_CLR_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_INTEN_OFFSET, pm_val->inten_reg);
		amba_writel(reg_base + VIC_SOFTEN_CLR_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_SOFTEN_OFFSET, pm_val->soften_reg);
		amba_writel(reg_base + VIC_PROTEN_OFFSET, pm_val->proten_reg);
		amba_writel(reg_base + VIC_SENSE_OFFSET, pm_val->sense_reg);
		amba_writel(reg_base + VIC_BOTHEDGE_OFFSET, pm_val->bothedge_reg);
		amba_writel(reg_base + VIC_EVENT_OFFSET, pm_val->event_reg);
	}
#endif
}

struct syscore_ops ambvic_syscore_ops = {
	.suspend	= ambvic_suspend,
	.resume		= ambvic_resume,
};

#endif

int __init ambvic_of_init(struct device_node *np, struct device_node *parent)
{
	void __iomem *reg_base;
	int i, irq;

	memset(&ambvic_data, 0, sizeof(struct ambvic_chip_data));

	for (i = 0; i < VIC_INSTANCES; i++) {
		reg_base = of_iomap(np, i);
		BUG_ON(!reg_base);

#ifndef CONFIG_PLAT_AMBARELLA_AMBALINK
		amba_writel(reg_base + VIC_INT_SEL_OFFSET, 0x00000000);
		amba_writel(reg_base + VIC_INTEN_OFFSET, 0x00000000);
		amba_writel(reg_base + VIC_INTEN_CLR_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_EDGE_CLR_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_INT_PTR0_OFFSET, 0xffffffff);
		amba_writel(reg_base + VIC_INT_PTR1_OFFSET, 0x00000000);
#endif
		ambvic_data.reg_base[i] = reg_base;
	}

	set_handle_irq(ambvic_handle_irq);

	ambvic_data.domain = irq_domain_add_linear(np,
			NR_VIC_IRQS, &amb_irq_domain_ops, NULL);
	BUG_ON(!ambvic_data.domain);

	// WORKAROUND only, will be removed finally
	for (i = 1; i < NR_VIC_IRQS; i++) {
		irq = irq_create_mapping(ambvic_data.domain, i);
		irq_set_chip_and_handler(irq, &ambvic_chip, handle_level_irq);
		irq_set_chip_data(irq, ambvic_data.reg_base[HWIRQ_TO_BANK(i)]);
		set_irq_flags(irq, IRQF_VALID | IRQF_PROBE);
	}

#ifdef CONFIG_SMP
	ambvic_data.ipi_reg_base = ambvic_data.reg_base[IPI01_IRQ / 32];
	ambvic_smp_softirq_init();
	set_smp_cross_call(ambvic_raise_softirq);
#if 0
	/*
	 * Set the default affinity from all CPUs to the boot cpu.
	 * This is required since the MPIC doesn't limit several CPUs
	 * from acknowledging the same interrupt.
	 */
	cpumask_clear(irq_default_affinity);
	cpumask_set_cpu(smp_processor_id(), irq_default_affinity);
#endif
#endif

#if defined(CONFIG_PM)
	register_syscore_ops(&ambvic_syscore_ops);
#endif
	return 0;
}

IRQCHIP_DECLARE(ambvic, "ambarella,vic", ambvic_of_init);

/* ==========================================================================*/
#if (VIC_SUPPORT_CPU_OFFLOAD >= 1)
void ambvic_sw_set(void __iomem *reg_base, u32 offset)
{
	amba_writel(reg_base + VIC_SOFT_INT_INT_OFFSET, offset);
}

void ambvic_sw_clr(void __iomem *reg_base, u32 offset)
{
	amba_writel(reg_base + VIC_SOFT_INT_CLR_INT_OFFSET, offset);
}
#else
void ambvic_sw_set(void __iomem *reg_base, u32 offset)
{
	amba_writel(reg_base + VIC_SOFTEN_OFFSET, 0x1 << offset);
}

void ambvic_sw_clr(void __iomem *reg_base, u32 offset)
{
	amba_writel(reg_base + VIC_SOFTEN_CLR_OFFSET, 0x1 << offset);
}
#endif


