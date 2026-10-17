# Plan

Milestones are ordered; each one ends in something that boots and can be seen working in QEMU. Items get checked off as they land. Refine as we go.

## M0: toolchain and "hello pixels"

- [x] Install missing tools (see `docs/WORKFLOW.md`)
- [ ] CMake project with `uefi` (GCC), `uefi-clang` and `host` presets, freestanding toolchain file
- [ ] `crt0.S` + linker script + `objcopy` to PE; verify relocations are applied and OVMF loads the image
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
- [ ] Esc reboots (`ResetSystem`, with fallbacks)
- [ ] First real hardware test from a USB stick

## M2: platformer core

- [ ] Fixed-point math, AABB, tilemap with solid tiles (hard-coded test level, bigger than the screen both ways)
- [ ] Player: run, jump (variable height), gravity, tile collision
- [ ] Camera with dead zone, clamped to level, 4-direction scrolling
- [ ] Host unit tests for collision and physics

## M3: assets

- [ ] Asset converter: text-art sprites + palette -> sprite/tileset blob, text level -> tilemap blob
- [ ] Placeholder art: player (idle/run/jump), a handful of tiles
- [ ] Load assets from the ESP at boot
- [ ] Sprite drawing with transparency and horizontal flip, simple animations (idle/run/jump)
- [ ] A small real level

## M4: interrupts and diagnostics

- [ ] Own GDT and IDT, GNU as ISR stubs
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

## Settled

- Controls: arrows move, Space or Z jump, Esc reboots.
- Art: placeholder sprites drawn as text art in the repo.
- Levels: plain text, one character per tile; no level editor for now.
- Toolchain: host GCC with freestanding flags (no cross GCC for now) + GNU as/binutils, clang optional; no NASM. AT&T syntax.

## Open questions

- (none)
