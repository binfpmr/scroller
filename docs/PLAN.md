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
- [x] Network boot on the HP Pavilion (proxy mode through a Wi-Fi repeater, `scripts/netboot-server.sh proxy wlp1s0`)
- [x] NIC drivers behind one interface: e1000 for QEMU, RTL8168 `10EC:8168` for the HP Pavilion (RTL8168H, version
      541: needs its setup done twice after the firmware, plus a 3 s receive watchdog as a safety net)
- [x] Minimal ARP/IPv4/ICMP/UDP + DHCP client (address from the PXE boot, else DHCP), host-tested
- [x] UDP debug protocol + `tools/devlink.py`: log stream, screenshots, key injection, reboot (= load the new build),
      memory peek/poke (verified in QEMU)
- [x] Debug link on the HP Pavilion: ping, log stream, screenshots, keys, remote reboot into the new build (~30 s)
- [x] GDB stub over the debug link + `tools/devlink.py gdb` bridge (TCP on localhost:2345 for gdb / VS Code):
      breakpoints, single steps, registers, memory, Ctrl+C, crashes stop in gdb, kill = reboot (verified in QEMU)
- [x] GDB on the HP Pavilion (three sessions in a row: breakpoint, variables, next, stepi, detach)
- [x] Hot reload over the debug link (`tools/devlink.py reload`: ~1.2 s in QEMU, ~0.9 s on the HP Pavilion); warm
      reboot by default (skips the Pavilion's false fan error at a cold start)
- [ ] Optional: simplified TCP in `net/` (design below), then gdb connects to the game directly

### Simplified TCP (design, not scheduled)

Goal: gdb (`target remote <ip>:2345`) and maybe a text console without the PC-side bridge. Server side only, one
connection at a time, LAN only, so most of TCP's machinery can go:

- States: LISTEN -> SYN_RCVD -> ESTABLISHED -> CLOSE_WAIT -> LAST_ACK -> CLOSED, plus FIN_WAIT_1/2 for a close from
  our side; no TIME_WAIT (the peer actively closes), no simultaneous open. Anything unexpected gets a RST.
- Receive: in-order segments only (seq == rcv_nxt); anything else is dropped and re-ACKed, the peer retransmits.
  Fixed receive ring (4 KiB), the window advertised is its free space. ACK every segment at once (no delayed ACK).
- Send: unacknowledged bytes stay in a send ring (8 KiB) until ACKed; up to 4 segments in flight within the peer's
  window, no Nagle, no congestion control (a LAN). Retransmit from the oldest unacked byte after 200 ms, doubling up
  to 3 s; give up after 10 tries (RST, back to LISTEN).
- Options: send MSS 1460 in the SYN-ACK and respect the peer's MSS; ignore window scale, SACK and timestamps (they
  are only used when both SYNs carry them, so not echoing them switches them off). Initial sequence number from the TSC.
- Same polling model as UDP: `Stack::Tick` runs the retransmit timer; works while stopped in the debugger because the
  stub keeps polling the network.
- API: `stack.Listen(port, callbacks)`; callbacks for connect, received bytes and close; `TcpSend` returns how many
  bytes fit in the send ring.
- Tests: host tests with two stacks and with captured Linux segments (handshake, loss and retransmission, window
  full, FIN from either side, RST); then QEMU (`hostfwd=tcp`) against real gdb and `nc`.
- Size: roughly 500 lines plus tests. The gdb stub stays as it is: only its transport changes (bytes over TCP with
  the usual `$...#cs` framing and acks, instead of whole packets over the debug link).

## M7: USB keyboard

- [x] PCI enumeration
- [x] xHCI: BIOS handoff, reset, command and event rings (polled), No-Op command; ports listed in the log
- [x] Device enumeration on root ports: Enable Slot, Address Device, device and configuration descriptors (logged;
      HP Pavilion: xHCI `8086:A36D`, camera and Bluetooth enumerate)
- [x] HID boot-protocol keyboard (interrupt IN endpoint, 8-byte reports) -> same `InputState`, next to PS/2
- [x] QEMU: `tools/qemu.py --usb-kbd` (qemu-xhci + usb-kbd)
- [ ] HP Pavilion with an external USB keyboard (needs a human)
- [ ] Remove the `uefi-input` build
- [ ] Later: hubs, HID gamepads (report descriptor parsing)

## M8: disk access (re-scoped)

The assets are inside the image, so a disk is only needed for saves or levels loaded separately.

- [ ] USB mass storage (bulk-only transport, SCSI READ(10)) on the xHCI driver: read the stick we booted from
- [ ] Optional, later: NVMe for internal drives (QEMU `-device nvme`)
- [ ] Optional: saves (write support)

## M9: polish

- [x] Write-combining framebuffer via PAT (pulled forward: real hardware ran at a few FPS)
- [x] On-screen performance overlay (F1, hidden by default), 5x7 bitmap font
- [x] Vsync: Intel display pipe scanline counter (verified on HP Pavilion), VGA retrace bit fallback
- [x] Git version shown at boot and in the overlay; local `scripts/flash-usb.sh` (not in git)
- [ ] Parallax background, more enemies/levels, title screen

## Settled

- Controls: Left/Right move, Up or Space jump, Down ducks (hold 2 s: duck-jump), Esc back to level 1, Ctrl+Alt+Delete reboots, media keys or `=`/`-`/`M` for volume, `N` music on/off (off at start).
- Art: placeholder sprites drawn as text art in the repo.
- Levels: plain text, one character per tile; no level editor for now.
- Toolchain: host GCC with freestanding flags (no cross GCC for now) + GNU as/binutils, clang optional; no NASM. AT&T syntax.

## Open questions

- (none)
