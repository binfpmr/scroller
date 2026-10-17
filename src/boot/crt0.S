/*
 * UEFI entry point.
 *
 * The firmware calls _start with the Microsoft x64 ABI: %rcx = ImageHandle, %rdx = SystemTable. The image was linked
 * at address 0 and loaded anywhere, so before any C++ runs we apply the ELF R_X86_64_RELATIVE relocations ourselves
 * (*(base + r_offset) = base + r_addend). Only caller-saved registers (MS ABI) are used here; efi_main is itself
 * declared ms_abi, so it preserves everything the firmware expects.
 */

#define R_X86_64_RELATIVE 8
#define ELF64_RELA_SIZE 24

	.section .text.start, "ax", @progbits
	.globl	_start
	.type	_start, @function
_start:
	pushq	%rcx				/* ImageHandle; %rsp is now 16-byte aligned */
	pushq	%rdx				/* SystemTable */
	subq	$40, %rsp			/* 32 bytes shadow space + 8 to keep alignment */

	leaq	__image_base(%rip), %r8		/* load address = link address 0 + displacement */
	leaq	__rela_start(%rip), %r9
	leaq	__rela_end(%rip), %r10
1:	cmpq	%r10, %r9
	jae	3f
	cmpl	$R_X86_64_RELATIVE, 8(%r9)	/* ELF64_R_TYPE(r_info) is the low 32 bits */
	jne	2f
	movq	(%r9), %rax			/* r_offset */
	movq	16(%r9), %r11			/* r_addend */
	addq	%r8, %r11
	movq	%r11, (%r8,%rax)
	addq	$ELF64_RELA_SIZE, %r9
	jmp	1b

2:	movabsq	$0x8000000000000001, %rax	/* EFI_LOAD_ERROR: unsupported relocation type */
	jmp	4f

3:	movq	48(%rsp), %rcx
	movq	40(%rsp), %rdx
	call	efi_main
4:	addq	$56, %rsp
	ret
	.size	_start, . - _start

/*
 * Placeholder PE base relocation block (page 0, two IMAGE_REL_BASED_ABSOLUTE no-op entries). Its only job is to give
 * the PE file a base relocation directory, so firmware treats the image as relocatable; the real work is done above.
 */
	.section .reloc, "a"
	.long	0				/* PageRVA */
	.long	12				/* BlockSize */
	.word	0
	.word	0

	.section .note.GNU-stack, "", @progbits
