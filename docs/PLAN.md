# Plan

Milestones are ordered; each one ends in something that boots and can be seen working in QEMU. Items get checked off as they land. Refine as we go.

## M0: toolchain and "hello pixels"

- [ ] Install missing tools (see `docs/WORKFLOW.md`)
- [ ] CMake project with `uefi` and `host` presets, cross toolchain file
- [ ] Minimal UEFI headers + `efi_main` that logs to debugcon/serial and fills the screen with a colour
- [ ] `scripts/build-image.sh`: GPT disk image with a FAT32 ESP (`/EFI/BOOT/BOOTX64.EFI`), plus a bare ESP image for copying onto an existing partition
- [ ] `scripts/run-qemu.sh` (interactive, `--debug` for gdb stub)
- [ ] `scripts/smoke-test.sh`: headless boot, wait, capture screenshot + log, fail on missing "boot ok" line
- [ ] VS Code: build, run, debug (gdb breakpoint in `efi_main` hits with source)
- [ ] `lib/` basics: mem*, minimal formatted logging

## M1: own the machine

- [ ] GOP mode selection (integer scale preference), pixel format handling
- [ ] 320x240 `Surface` + scaled `present()`
- [ ] TSC calibration, `ExitBootServices`, `BootInfo` hand-off
- [ ] PS/2 keyboard polling -> `InputState` (held / pressed / released per key)
- [ ] 60 Hz fixed-step loop; test pattern that scrolls with arrow keys
- [ ] `SCROLLER_UEFI_INPUT` fallback build for real-HW trials
- [ ] First real hardware test from a USB stick

## M2: platformer core

- [ ] Fixed-point math, AABB, tilemap with solid tiles (hard-coded test level, bigger than the screen both ways)
- [ ] Player: run, jump (variable height), gravity, tile collision
- [ ] Camera with dead zone, clamped to level, 4-direction scrolling
- [ ] Host unit tests for collision and physics

## M3: assets

- [ ] Asset converter (PNG -> sprite/tileset blob, text/PNG level -> tilemap blob)
- [ ] Load assets from the ESP at boot
- [ ] Sprite drawing with transparency and horizontal flip, simple animations (idle/run/jump)
- [ ] A small real level

## M4: interrupts and diagnostics

- [ ] Own GDT and IDT, NASM ISR stubs
- [ ] Exception handler that dumps registers to the log and screen
- [ ] Interrupt controller (APIC/IOAPIC via ACPI MADT) and a timer (HPET or LAPIC)

## M5: sound

- [ ] Sound Blaster 16 in QEMU (`-device sb16`): DMA playback of a PCM buffer, simple mixer, jump/land effects
- [ ] Intel HDA for real hardware (QEMU `-device intel-hda -device hda-duplex` to develop)

## M6: USB keyboard

- [ ] PCI enumeration
- [ ] xHCI controller init, device enumeration
- [ ] HID boot-protocol keyboard -> same `InputState`
- [ ] Develop in QEMU with `-device qemu-xhci -device usb-kbd`, then real hardware

## M7: runtime disk access

- [ ] Read a raw data partition via AHCI (QEMU default on q35)
- [ ] NVMe (QEMU `-device nvme`) for modern PCs
- [ ] Optional: saves (write support); USB mass storage is out of scope unless needed

## M8: polish

- [ ] Write-combining framebuffer via PAT
- [ ] Parallax background, more enemies/levels, title screen

## Open questions

- Controls: arrows + Z/X? Or arrows + Space? (default: arrows to move, Space/Z jump, Esc reboots)
- Art: draw our own placeholder sprites, or use a CC0 pack (e.g. Kenney, 16x16)?
- Level format: plain text (one char per tile, easy to edit by hand) vs. Tiled `.tmx` export?
- Exit behaviour: Esc -> reboot (via keyboard controller/ACPI reset) or halt?
