#include <linux/init.h>
#include <linux/linkage.h>

#include <asm/assembler.h>
#include <asm/asm-offsets.h>
#include <asm/errno.h>
#include <asm/thread_info.h>

@ Bad Abort numbers
@ -----------------
@
#define BAD_PREFETCH	0
#define BAD_DATA	1
#define BAD_ADDREXCPTN	2
#define BAD_IRQ		3
#define BAD_UNDEFINSTR	4

@
@ Most of the stack format comes from struct pt_regs, but with
@ the addition of 8 bytes for storing syscall args 5 and 6.
@ This _must_ remain a multiple of 8 for EABI.
@
#define S_OFF		8

/*
 * The SWI code relies on the fact that R0 is at the bottom of the stack
 * (due to slow/fast restore user regs).
 */
#if S_R0 != 0
#error "Please fix"
#endif

	.macro	zero_fp
#ifdef CONFIG_FRAME_POINTER
	mov	fp, #0
#endif
	.endm

	.macro	alignment_trap, rtemp
#ifdef CONFIG_ALIGNMENT_TRAP
	ldr	\rtemp, .LCcralign
	ldr	\rtemp, [\rtemp]
	mcr	p15, 0, \rtemp, c1, c0
#endif
	.endm

	@
	@ Store/load the USER SP and LR registers by switching to the SYS
	@ mode. Useful in Thumb-2 mode where "stm/ldm rd, {sp, lr}^" is not
	@ available. Should only be called from SVC mode
	@
	.macro	store_user_sp_lr, rd, rtemp, offset = 0
	mrs	\rtemp, cpsr
	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch to the SYS mode

	str	sp, [\rd, #\offset]		@ save sp_usr
	str	lr, [\rd, #\offset + 4]		@ save lr_usr

	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch back to the SVC mode
	.endm

	.macro	load_user_sp_lr, rd, rtemp, offset = 0
	mrs	\rtemp, cpsr
	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch to the SYS mode

	ldr	sp, [\rd, #\offset]		@ load sp_usr
	ldr	lr, [\rd, #\offset + 4]		@ load lr_usr

	eor	\rtemp, \rtemp, #(SVC_MODE ^ SYSTEM_MODE)
	msr	cpsr_c, \rtemp			@ switch back to the SVC mode
	.endm

#ifndef CONFIG_THUMB2_KERNEL
	@
	@ For BOSS INT handling only:
	@ The macro is called after we are done with handling INT but before we
	@ return back to normal execution. At this point, guest_irq is likely
	@ enabled/disabled serverl times. Before we clear I bit through SPSR
	@ manipulation, we must clear software GUEST_IRQ_MASK and set all
	@ guest IRQ vic bits. Otherwise, guest irq will never get a chance
	@ to fire up.
	@
	.macro  boss_ret_prefix, rpsr
#if defined(CONFIG_PLAT_AMBARELLA_BOSS)
	@ Force to disable interrupt while doing boss interrupt update.
	@ Normally, interrupt is disabled before calling this macro,
	@ but sometimes interrupt is enabled in irq_exit() -> __do_softirq().
	cpsid	i
	ldr	r4, =boss
	ldr	r4, [r4]
	tst	\rpsr, #0x80
	moveq	r1, #0
	movne	r1, #1

	@ set/clear boss->guest_irq_mask
	str     r1, [r4, #BOSS_GUEST_IRQ_MASK_OFFSET]

#if defined(CONFIG_ARM_GIC)
	moveq	r2, #GIC_SET_ENABLE_OFFSET
	movne	r2, #GIC_CLEAR_ENABLE_OFFSET
	ldr	r3, =AMBARELLA_VA_GIC_DIST_BASE
	orr	r2, r3, r2

	@ enable/diable gicNmask IRQs
	mov	r1, #BOSS_INT0MASK_OFFSET
	mov	r3, #BOSS_GUEST_INT0_EN_OFFSET
	mov	r5, #0
1:
	add	r6, r1, r5
	ldr	r7, [r4, r6]
	add	r6, r3, r5
	ldr	r6, [r4, r6]
	and	r7, r7, r6

	str	r7, [r2, r5]

	add	r5, r5, #4
	cmp	r5, #32
	bne	1b

#else
	moveq   r2, #VIC_INTEN_OFFSET
	movne   r2, #VIC_INTEN_CLR_OFFSET

	@ enable/disable vicNmask IRQs
	ldr	r1, [r4, #BOSS_INT0MASK_OFFSET]
	ldr 	r3, [r4, #BOSS_GUEST_INT0_EN_OFFSET]
	and 	r1, r1, r3
	ldr     r3, =VIC0_BASE
	str     r1, [r3, r2]

	ldr	r1, [r4, #BOSS_INT1MASK_OFFSET]
	ldr 	r3, [r4, #BOSS_GUEST_INT1_EN_OFFSET]
	and 	r1, r1, r3
	ldr     r3, =VIC1_BASE
	str     r1, [r3, r2]

	ldr	r1, [r4, #BOSS_INT2MASK_OFFSET]
	ldr	r3, [r4, #BOSS_GUEST_INT2_EN_OFFSET]
	and	r1, r1, r3
	ldr	r3, =VIC2_BASE
	str	r1, [r3, r2]

#if (CHIP_REV == S3)
	ldr	r1, [r4, #BOSS_INT3MASK_OFFSET]
	ldr	r3, [r4, #BOSS_GUEST_INT3_EN_OFFSET]
	and	r1, r1, r3
	ldr	r3, =VIC3_BASE
	str	r1, [r3, r2]
#endif

#endif
#endif
	.endm

	.macro	svc_exit, rpsr, irq = 0
	.if	\irq != 0
	@ IRQs already off
#ifdef CONFIG_TRACE_IRQFLAGS
	@ The parent context IRQs must have been enabled to get here in
	@ the first place, so there's no point checking the PSR I bit.
	bl	trace_hardirqs_on
#endif
	.else
	@ IRQs off again before pulling preserved data off the stack
	disable_irq_notrace
#ifdef CONFIG_TRACE_IRQFLAGS
	tst	\rpsr, #PSR_I_BIT
	bleq	trace_hardirqs_on
	tst	\rpsr, #PSR_I_BIT
	blne	trace_hardirqs_off
#endif
	.endif
	msr	spsr_cxsf, \rpsr
	boss_ret_prefix \rpsr
#if defined(CONFIG_CPU_V6)
	ldr	r0, [sp]
	strex	r1, r2, [sp]			@ clear the exclusive monitor
	ldmib	sp, {r1 - pc}^			@ load r1 - pc, cpsr
#elif defined(CONFIG_CPU_32v6K)
	clrex					@ clear the exclusive monitor
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr
#else
	ldmia	sp, {r0 - pc}^			@ load r0 - pc, cpsr
#endif
	.endm

	.macro	restore_user_regs, fast = 0, offset = 0
	ldr	r1, [sp, #\offset + S_PSR]	@ get calling cpsr
	ldr	lr, [sp, #\offset + S_PC]!	@ get pc
	msr	spsr_cxsf, r1			@ save in spsr_svc
	boss_ret_prefix r1
#if defined(CONFIG_CPU_V6)
	strex	r1, r2, [sp]			@ clear the exclusive monitor
#elif defined(CONFIG_CPU_32v6K)
	clrex					@ clear the exclusive monitor
#endif
	.if	\fast
	ldmdb	sp, {r1 - lr}^			@ get calling r1 - lr
	.else
	ldmdb	sp, {r0 - lr}^			@ get calling r0 - lr
	.endif
	mov	r0, r0				@ ARMv5T and earlier require a nop
						@ after ldm {}^
	add	sp, sp, #S_FRAME_SIZE - S_PC
	movs	pc, lr				@ return & move spsr_svc into cpsr
	.endm

	.macro	get_thread_info, rd
	mov	\rd, sp, lsr #13
	mov	\rd, \rd, lsl #13
	.endm

	@
	@ 32-bit wide "mov pc, reg"
	@
	.macro	movw_pc, reg
	mov	pc, \reg
	.endm
#else	/* CONFIG_THUMB2_KERNEL */
	.macro	svc_exit, rpsr, irq = 0
	.if	\irq != 0
	@ IRQs already off
#ifdef CONFIG_TRACE_IRQFLAGS
	@ The parent context IRQs must have been enabled to get here in
	@ the first place, so there's no point checking the PSR I bit.
	bl	trace_hardirqs_on
#endif
	.else
	@ IRQs off again before pulling preserved data off the stack
	disable_irq_notrace
#ifdef CONFIG_TRACE_IRQFLAGS
	tst	\rpsr, #PSR_I_BIT
	bleq	trace_hardirqs_on
	tst	\rpsr, #PSR_I_BIT
	blne	trace_hardirqs_off
#endif
	.endif
	ldr	lr, [sp, #S_SP]			@ top of the stack
	ldrd	r0, r1, [sp, #S_LR]		@ calling lr and pc
	clrex					@ clear the exclusive monitor
	stmdb	lr!, {r0, r1, \rpsr}		@ calling lr and rfe context
	ldmia	sp, {r0 - r12}
	mov	sp, lr
	ldr	lr, [sp], #4
	rfeia	sp!
	.endm

	.macro	restore_user_regs, fast = 0, offset = 0
	clrex					@ clear the exclusive monitor
	mov	r2, sp
	load_user_sp_lr r2, r3, \offset + S_SP	@ calling sp, lr
	ldr	r1, [sp, #\offset + S_PSR]	@ get calling cpsr
	ldr	lr, [sp, #\offset + S_PC]	@ get pc
	add	sp, sp, #\offset + S_SP
	msr	spsr_cxsf, r1			@ save in spsr_svc
	.if	\fast
	ldmdb	sp, {r1 - r12}			@ get calling r1 - r12
	.else
	ldmdb	sp, {r0 - r12}			@ get calling r0 - r12
	.endif
	add	sp, sp, #S_FRAME_SIZE - S_SP
	movs	pc, lr				@ return & move spsr_svc into cpsr
	.endm

	.macro	get_thread_info, rd
	mov	\rd, sp
	lsr	\rd, \rd, #13
	mov	\rd, \rd, lsl #13
	.endm

	@
	@ 32-bit wide "mov pc, reg"
	@
	.macro	movw_pc, reg
	mov	pc, \reg
	nop
	.endm
#endif	/* !CONFIG_THUMB2_KERNEL */

/*
 * Context tracking subsystem.  Used to instrument transitions
 * between user and kernel mode.
 */
	.macro ct_user_exit, save = 1
#ifdef CONFIG_CONTEXT_TRACKING
	.if	\save
	stmdb   sp!, {r0-r3, ip, lr}
	bl	user_exit
	ldmia	sp!, {r0-r3, ip, lr}
	.else
	bl	user_exit
	.endif
#endif
	.endm

	.macro ct_user_enter, save = 1
#ifdef CONFIG_CONTEXT_TRACKING
	.if	\save
	stmdb   sp!, {r0-r3, ip, lr}
	bl	user_enter
	ldmia	sp!, {r0-r3, ip, lr}
	.else
	bl	user_enter
	.endif
#endif
	.endm

/*
 * These are the registers used in the syscall handler, and allow us to
 * have in theory up to 7 arguments to a function - r0 to r6.
 *
 * r7 is reserved for the system call number for thumb mode.
 *
 * Note that tbl == why is intentional.
 *
 * We must set at least "tsk" and "why" when calling ret_with_reschedule.
 */
scno	.req	r7		@ syscall number
tbl	.req	r8		@ syscall table pointer
why	.req	r8		@ Linux syscall (!= 0)
tsk	.req	r9		@ current thread_info
