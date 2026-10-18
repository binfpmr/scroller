/*
 * Static-PIE ELF linked at address 0, converted to PE by objcopy (see cmake/uefi.cmake).
 *
 * ELF virtual addresses become PE RVAs unchanged, so every section that goes into the PE file starts on a 4 KiB
 * boundary (PE SectionAlignment) and the first page is left free for the PE headers. Only .text, .rodata, .data,
 * .rela and .reloc are copied into the PE file; section names stay within the 8 characters PE allows.
 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
  . = 0;
  __image_base = .;
  . = 0x1000;

  .text : ALIGN(0x1000) {
    KEEP(*(.text.start))
    *(.text .text.*)
  }

  .rodata : ALIGN(0x1000) {
    *(.rodata .rodata.*)
  }

  /* .bss is folded into .data so the loader does not have to zero-fill anything; keep big buffers out of .bss. */
  .data : ALIGN(0x1000) {
    *(.data.rel.ro .data.rel.ro.*)
    *(.data .data.*)
    *(.got .got.plt)
    *(.bss .bss.* COMMON)
  }

  /* Dynamic relocations (only R_X86_64_RELATIVE is allowed), applied by crt0.S at startup. */
  .rela : ALIGN(0x1000) {
    __rela_start = .;
    *(.rela.dyn .rela.*)
    __rela_end = .;
  }

  /* Placeholder PE base relocation table from crt0.S, so the image is marked relocatable. */
  .reloc : ALIGN(0x1000) {
    KEEP(*(.reloc))
  }
  __image_end = .;  /* end of what the PE file contains; used to keep crash backtraces inside our code */

  /* Required by the static-PIE link, not copied into the PE file. */
  . = ALIGN(0x1000);
  .dynamic : { *(.dynamic) }
  .dynsym : { *(.dynsym) }
  .dynstr : { *(.dynstr) }
  .hash : { *(.hash) }
  .gnu.hash : { *(.gnu.hash) }

  /DISCARD/ : {
    *(.interp)
    *(.eh_frame .eh_frame_hdr)
    *(.note .note.*)
    *(.comment)
  }
}
