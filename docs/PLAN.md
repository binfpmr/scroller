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
- [x] Esc reboots (`ResetSystem`, with fallbacks); later: Esc back to level 1, Ctrl+Alt+Delete reboots
- [x] First real hardware test from a USB stick: HP Pavilion laptop, `uefi` image (built-in keyboard works over PS/2)

## M2: platformer core

- [x] Fixed-point math, AABB, tilemap with solid tiles (test level `assets/levels/test.txt`, embedded at build time)
- [x] Player: run, jump (variable height, coyote time, buffering), gravity, tile collision
- [x] Camera with dead zone, clamped to level, 4-direction scrolling
- [x] Host unit tests for collision and physics; random-play invariant test; `levelcheck` reachability tool
- [x] Feel check by a human: run/jump tuning in `PlayerTuning`
- [x] Wall-jump, duck-jump (50% higher), ducking look (squashed, eye shut)

## M3: assets

- [x] Asset pack (`tools/pack_assets.py`), text sprites and levels parsed at boot (formats in `docs/ASSETS.md`)
- [x] Placeholder art: player (idle/blink/run/jump/fall/duck), ground, stone, goal flag
- [x] Load assets from the ESP at boot; startup errors shown on screen
- [x] Sprite drawing with transparency and horizontal flip, animations
- [x] Level 1 with a goal; `levelcheck` verifies the goal is reachable and finds dead ends

## M4: interrupts and diagnostics

- [x] Own GDT and IDT, GNU as ISR stubs
- [x] Exception handler that dumps registers to the log and screen (with call chain, addr2line hint)
- [x] Interrupt controller (APIC/IOAPIC via ACPI MADT) and a timer (LAPIC, 1 kHz)
- [x] Interrupt-driven PS/2 keyboard with polling fallback; idle with `hlt`
- [x] Real hardware check (HP Pavilion): TIMER IRQ 1000 HZ, OTHER IRQS 0, KBD PS/2 IRQ, vsync; F9/F10 crash screens decode with addr2line

## M5: sound

- [x] Chiptune engine (square/triangle/saw/noise, ADSR), plain-text tracker songs, `render_audio` host tool
- [x] Minuet in G (BWV Anh. 114) as background music; jump, land, wall-jump and goal effects
- [x] Sound Blaster 16 in QEMU (`--sound sb16`): ISA DMA auto-init, IRQ per half buffer
- [x] Intel HDA (`--sound hda`, default): CORB/RIRB, codec path search, timer-driven refill; verified in QEMU
- [x] Real hardware check: sound from the HP Pavilion's speakers and headphones (Realtek ALC295)
- [x] Music at 50 % of the effects; master volume (starts at 50 %) with on-screen indicator

## M6: network development loop (hardware-in-the-loop without USB sticks)

- [x] Assets linked into the image (no file system needed)
- [x] Network boot (PXE) in QEMU (`tools/qemu.py --netboot`); PXE addresses passed in `BootInfo`
- [x] `scripts/netboot-server.sh` (dnsmasq: `proxy` next to the router's DHCP, or `direct` cable)
- [ ] Network boot on the HP Pavilion (needs a human: cable, BIOS boot order)
- [x] NIC drivers behind one interface: e1000 for QEMU (verified), RTL8168 `10EC:8168` for the HP Pavilion (written,
      not yet tried on hardware)
- [x] Minimal ARP/IPv4/ICMP/UDP + DHCP client (address from the PXE boot, else DHCP), host-tested
- [x] UDP debug protocol + `tools/devlink.py`: log stream, screenshots, key injection, reboot (= load the new build),
      memory peek/poke (verified in QEMU)
- [ ] Debug link on the HP Pavilion (needs a human: cable, `SCROLLER_TARGET`)
- [ ] Optional: GDB remote stub over UDP (`target remote udp:...`)

## M7: USB keyboard

- [x] PCI enumeration
- [ ] xHCI controller init, device enumeration
- [ ] HID boot-protocol keyboard -> same `InputState`
- [ ] Develop in QEMU with `-device qemu-xhci -device usb-kbd`, then real hardware

## M8: runtime disk access

- [ ] Read a raw data partition via AHCI (QEMU default on q35)
- [ ] NVMe (QEMU `-device nvme`) for modern PCs
- [ ] Optional: saves (write support); USB mass storage is out of scope unless needed

## M9: polish

- [x] Write-combining framebuffer via PAT (pulled forward: real hardware ran at a few FPS)
- [x] On-screen performance overlay (F1, hidden by default), 5x7 bitmap font
- [x] Vsync: Intel display pipe scanline counter (verified on HP Pavilion), VGA retrace bit fallback
- [x] Git version shown at boot and in the overlay; local `scripts/flash-usb.sh` (not in git)
- [ ] Parallax background, more enemies/levels, title screen

## Settled

- Controls: Left/Right move, Up or Space jump, Down ducks, Esc back to level 1, Ctrl+Alt+Delete reboots, media keys or `=`/`-`/`M` for volume.
- Art: placeholder sprites drawn as text art in the repo.
- Levels: plain text, one character per tile; no level editor for now.
- Toolchain: host GCC with freestanding flags (no cross GCC for now) + GNU as/binutils, clang optional; no NASM. AT&T syntax.

## Open questions

- (none)
