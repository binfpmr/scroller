/*
 * The asset pack (tools/pack_assets.py) linked into the image, so the game needs no file system: it boots the same
 * from a disk, a USB stick or the network. SCROLLER_ASSETS_PAK is the pack's path, set by cmake/uefi.cmake.
 */
	.section .rodata.assets, "a"
	.balign	16
	.globl	scroller_assets_begin
	.globl	scroller_assets_end
	.hidden	scroller_assets_begin
	.hidden	scroller_assets_end
scroller_assets_begin:
	.incbin	SCROLLER_ASSETS_PAK
scroller_assets_end:

	.section .note.GNU-stack, "", @progbits
