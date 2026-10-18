/*
 * Entry points for all 256 interrupt vectors, 16 bytes apart starting at isr_stubs (the IDT points vector n at
 * isr_stubs + 16 * n). Each stub makes the stack look the same (pushes 0 where the CPU pushes no error code, then
 * the vector) and jumps to isr_common, which saves all general registers and the SSE state (handlers are ordinary
 * C++ that may use SSE), calls interrupt_dispatch(InterruptFrame*) and returns with iretq.
 * Layout must match x86::InterruptFrame in interrupts.h.
 */

	.text
	.globl	isr_stubs
	.balign	16
isr_stubs:
	.set	vector, 0
	.rept	256
	.balign	16
	/* Exceptions with a CPU-pushed error code: #DF #TS #NP #SS #GP #PF #AC #CP #VC #SX */
	.if (vector == 8) || (vector == 10) || (vector == 11) || (vector == 12) || (vector == 13) || (vector == 14) || (vector == 17) || (vector == 21) || (vector == 29) || (vector == 30)
	.else
	pushq	$0
	.endif
	pushq	$vector
	jmp	isr_common
	.set	vector, vector + 1
	.endr

isr_common:
	pushq	%rax
	pushq	%rbx
	pushq	%rcx
	pushq	%rdx
	pushq	%rsi
	pushq	%rdi
	pushq	%rbp
	pushq	%r8
	pushq	%r9
	pushq	%r10
	pushq	%r11
	pushq	%r12
	pushq	%r13
	pushq	%r14
	pushq	%r15
	movq	%rsp, %rdi		/* InterruptFrame* */
	movq	%rsp, %rbx		/* rbx is saved above; restored after rsp is */
	andq	$-16, %rsp
	subq	$512, %rsp
	fxsave64 (%rsp)
	cld
	call	interrupt_dispatch
	fxrstor64 (%rsp)
	movq	%rbx, %rsp
	popq	%r15
	popq	%r14
	popq	%r13
	popq	%r12
	popq	%r11
	popq	%r10
	popq	%r9
	popq	%r8
	popq	%rbp
	popq	%rdi
	popq	%rsi
	popq	%rdx
	popq	%rcx
	popq	%rbx
	popq	%rax
	addq	$16, %rsp		/* vector and error code */
	iretq

	.section .note.GNU-stack, "", @progbits
