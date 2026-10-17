# Plan

Milestones are ordered; each one ends in something that boots and can be seen working in QEMU. Items get checked off as they land. Refine as we go.

## M0: toolchain and "hello pixels"

- [x] Install missing tools (see `docs/WORKFLOW.md`)
- [x] CMake project with `uefi` (GCC), `uefi-clang` and `host` presets, freestanding toolchain file
- [x] `crt0.S` + linker script + `objcopy` to PE; verify relocations are applied and OVMF loads the image
- [x] Minimal UEFI headers + `efi_main` that logs to debugcon/serial and fills the screen with a colour
- [x] `scripts/build-image.sh`: GPT disk image with a FAT32 ESP (`/EFI/BOOT/BOOTX64.EFI`), plus a bare ESP image for copying onto an existing partition
- [x] `scripts/run-qemu.sh` (interactive, `--debug` for gdb stub)
- [x] `scripts/smoke-test.sh`: headless boot, wait, capture screenshot + log, fail on missing "boot ok" line
- [x] gdb: breakpoint in `efi_main` hits with source (command line, verified)
- [x] VS Code: build, run, debug from the UI (checked by user)
- [x] `lib/` basics: mem*, minimal formatted logging

## M1: own the machine

- [x] GOP mode selection (integer scale preference), pixel format handling
- [x] 320x240 `Surface` + scaled `present()`
- [x] TSC calibration, `ExitBootServices`, `BootInfo` hand-off
- [x] PS/2 keyboard polling -> `InputState` (held / pressed / released per key)
- [x] 60 Hz fixed-step loop; test pattern that scrolls with arrow keys
- [x] `SCROLLER_UEFI_INPUT` fallback build for real-HW trials (preset `uefi-input`)
- [x] Esc reboots (`ResetSystem`, with fallbacks)
- [ ] First real hardware test from a USB stick (needs a human; both `uefi` and `uefi-input` images)

## M2: platformer core

- [x] Fixed-point math, AABB, tilemap with solid tiles (test level `assets/levels/test.txt`, embedded at build time)
- [x] Player: run, jump (variable height, coyote time, buffering), gravity, tile collision
- [x] Camera with dead zone, clamped to level, 4-direction scrolling
- [x] Host unit tests for collision and physics; random-play invariant test; `levelcheck` reachability tool
- [ ] Feel check by a human: run/jump tuning in `PlayerTuning`

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
