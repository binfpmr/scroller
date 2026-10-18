# Architecture

A small 2D platformer that boots directly on x86_64 hardware via UEFI. No OS, no libc, no third-party runtime.

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Boot | UEFI x86_64 application (`BOOTX64.EFI`) | Boots on modern PCs and QEMU/OVMF; firmware gives us long mode, paging, a framebuffer and file access for free. Legacy BIOS/Mode X is not planned. |
| Firmware use | UEFI as a *loader*, then `ExitBootServices` | UEFI input has no key-release events and would fight us over IRQs/DMA (sound). After exit we own the machine. |
| Video | Game renders into a 320x240 XRGB8888 buffer in RAM; `present()` integer-scales it into the GOP framebuffer | No real Mode X under UEFI. 32-bit colour, no palette emulation. |
| Video mode | Keep the firmware's current mode if usable (native panel mode on real hardware; QEMU's preferred 960x720), otherwise the mode with the highest integer scale, exact fit preferred (`gfx::ChooseVideoMode`). The 320x240 4:3 screen is drawn at the largest integer scale that fits (`gfx::ComputeViewport`, up to x16), centred with black bars | Pixel-perfect with no panel rescaling. On the 16:9 HP Pavilion: x4 (1280x960). A screen whose width follows the display (e.g. 480x270 at x4) was considered and rejected: classic fixed 4:3 view. |
| Toolchain | GCC (`gcc`, `g++`) + GNU binutils (`as`, `ld`, `objcopy`) as a freestanding ELF toolchain, no OS target assumed; clang (`--target=x86_64-unknown-none-elf`) as an optional second compiler | No EDK2, no gnu-efi. A real `x86_64-elf` cross compiler can be plugged in via a toolchain prefix if wanted; the host GCC with freestanding flags produces equivalent code. |
| Executable format | Link a static PIE ELF at base 0 (`scroller.elf`, keeps DWARF for gdb), convert to PE with `objcopy --target=efi-app-x86_64` (`scroller.efi`); `crt0.S` applies the ELF `R_X86_64_RELATIVE` relocations itself before any C++ runs | UEFI loads PE images at an arbitrary address. ELF section addresses equal the PE RVAs, so debugging uses the ELF at a known offset. |
| Calling convention | Our code uses the System V ABI; UEFI functions and `efi_main`'s firmware entry use the Microsoft x64 ABI (`EFIAPI` = `__attribute__((ms_abi))`; `crt0.S` bridges the entry) | Standard approach for GCC on ELF. |
| Languages | Freestanding C++23 for almost everything, C23 where it fits (self-contained low-level pieces), GNU `as` for what needs assembly (entry/relocation, ISR stubs, descriptor loads). AT&T syntax everywhere, including inline asm | One main language keeps it simple. |
| UEFI headers | Our own minimal definitions (`src/uefi/`), only what we use | A few hundred lines; no external dependency. |
| Timing | Fixed 60 Hz game step on the TSC (calibrated against UEFI `Stall()` before exit); a 1 kHz local APIC timer interrupt wakes the main loop from `hlt` | The TSC keeps the game deterministic; the timer saves power and will drive sound. |
| Input | i8042 PS/2 keyboard (scan code set 1) on IRQ 1 through the IOAPIC, bytes queued by the handler; falls back to polling if a byte waits 50 ms without an interrupt | QEMU's default keyboard; most laptop built-in keyboards are also PS/2 behind the EC. USB (xHCI + HID boot keyboard) is a later milestone. |
| Assets | Plain text (levels one character per tile, sprites as text art with a palette; `docs/ASSETS.md`), packed into one file (`assets.pak`) that is linked into the image (`boot/assets_blob.S`), parsed at boot by the same code the host tests run | Diff-friendly, editable in any editor, no binary format to keep in sync; no file system needed, so the same image boots from disk, USB stick or network. |
| Controls | Left/Right move, Up or Space jump, Down ducks (hold 1 s: duck-jump), Esc back to level 1, Ctrl+Alt+Delete reboots, Ctrl+Alt+R hot-reloads from the network-boot server, F2 skips to the next level (development aid); volume up/down/mute with the media keys or `=`/`-`/`M`, `N` music on/off (off at start); F1 shows/hides the performance overlay (its last line shows the last raw key code) | |
| Exit | Ctrl+Alt+Delete (Esc on the error and crash screens) -> `ResetSystem(EfiResetCold)` (UEFI runtime services stay callable after `ExitBootServices`); fallback: 8042 reset pulse, then triple fault | Works on real hardware and QEMU. |
| Tests | Game logic, graphics and lib code also build for the host and have unit tests (CTest) | Fast feedback without booting. |

## Boot / runtime phases

```
OVMF / firmware
  -> _start                             [boot/crt0.S]  MS ABI entry: apply relocations, clear .bss, call efi_main
  -> efi_main()                         [boot/]  boot services available
       - init debug log (QEMU debugcon port 0xE9 + serial COM1)
       - GOP: pick video mode, remember framebuffer + pixel format
       - load assets from \scroller\ on the boot volume (SimpleFileSystem)
       - allocate one big memory arena (AllocatePages)
       - calibrate TSC with Stall()
       - GetMemoryMap + ExitBootServices
  -> RunGame(BootInfo&)                 [platform/game_loop]  we own the machine
       - write-combining framebuffer, own GDT/TSS/IDT with crash reports (x86::InitInterrupts, platform/panic)
       - ACPI MADT -> PICs masked, local APIC + 1 kHz timer, IOAPIC (platform/interrupt_controllers)
       - keyboard on IRQ 1, vsync source, asset pack, game
       - main loop: input -> game.Update() at fixed 60 Hz -> Render() -> wait for vsync (polling input) -> Present()
```

`BootInfo` is the only thing passed across the boundary: framebuffer description, loaded asset blobs, arena, TSC frequency, runtime services pointer. Nothing after `ExitBootServices` may call into UEFI boot services; the only runtime service we use is `ResetSystem`.

After exit we keep the firmware's identity-mapped page tables and GDT until we need our own (exceptions/interrupts milestone).

## Source layout (planned)

```
src/
  boot/          crt0.S (entry, self-relocation), linker script, UEFI loader phase, BootInfo
  uefi/          minimal UEFI type and protocol definitions
  arch/x86_64/   port I/O, cpu helpers (rdtsc, cli/sti, hlt), later GDT/IDT + GNU as stubs
  drivers/       debug log (debugcon/serial), ps2 keyboard; later: hpet/apic timer, sb16, hda, xhci, usb_hid, ahci, nvme
  platform/      the interface the game sees (input state, time, present, assets) + its bare-metal implementation
  gfx/           Surface (320x240 XRGB), blits, sprites, tilemap renderer, scaler to GOP
  game/          pure game logic: level, tile collision, player physics, camera
  net/           network stack (ARP, IPv4, ICMP, UDP, DHCP client), platform independent
  lib/           freestanding runtime: mem*/str* functions, fmt/log formatting, arena allocator, fixed-point math
tools/           host tools: qemu.py runner, pack_assets.py, version_header.py, gdb helper, levelcheck (C++)
assets/          text-art sprite sheets (sprites/), text levels (levels/), see docs/ASSETS.md
scripts/         build-image / run-qemu / smoke-test shell scripts
tests/           host unit tests for game/, gfx/, lib/
```

Dependency rule: `game/` and `gfx/` depend only on `lib/` and the `platform/` interface headers. Only `boot/`, `arch/`, `drivers/` and `platform/` implementations touch hardware or UEFI. This is what makes the host build and tests possible.

## Game core

- Fixed timestep, 60 updates/s. Render once per update (no interpolation in v1).
- Positions/velocities in 24.8 fixed point (`lib::Fixed`, 1/256 px) so physics is deterministic and identical on host and target.
- World = tilemap of 16x16 tiles (`game::Level`), larger than the screen in both axes; outside the map counts as walls and floor, open above. Camera (`game::Camera`) keeps the player in a ±24 px dead zone around the screen centre and is clamped to level bounds, so scrolling works in all 4 directions.
- Player (`game/player.*`): 12x14 hitbox, AABB collision against solid tiles resolved one axis at a time. Speeds stay below one tile per tick, so checking the leading edge's tile row/column after a move is enough (no tunnelling). Run with acceleration/friction, jump with buffering, coyote time and variable height (release early = lower jump), gravity with terminal velocity. Down on the ground ducks (2 px lower, eye shut, a soft blip); a jump while ducking or up to 10 ticks after is a duck-jump (50% higher), and holding Down for 1 s does the duck-jump by itself (once per press). Jumping in the air while touching a side wall (or up to 6 ticks after) is a wall-jump: a full jump from zero horizontal speed, with the input overridden as "jump + away from the wall held" until the top of the arc. All tuning constants are in `PlayerTuning`; changing them changes which platforms are reachable, so re-run `levelcheck` and the tests.
- Enemies (`game/enemy.*`): walkers, jumpers and flyers from `w`/`j`/`f` in the level, 12x12 hitboxes, simple
  tile collision; they are only avoided: touching one (hitbox shrunk by 2 px) respawns the player at the last
  checkpoint and resets all enemies. Falling below the map (a bottomless pit) respawns too.
- Rendering: sky gradient, parallax layers (clouds at 1/6 of the camera speed, far hills 1/4, near hills 1/2, bushes 3/4; hills computed per column, sinking as the camera climbs), then only the visible tiles, then the player sprite (frame chosen from its state, `game::PlayerFrame`), into the 320x240 surface.
- Level design check: `levelcheck` (host tool, `tools/levelcheck/`) searches all resting positions reachable from the start with the real physics and prints the level with reachable spots marked.

## Interrupts

- Our GDT: code 0x08, data 0x10, TSS 0x18 (IST1 stack for double fault, NMI, machine check). IDT: 256 interrupt gates
  into `arch/x86_64/interrupt_stubs.S` (16-byte stubs), common path saves all registers and SSE state (`fxsave64`)
  around `interrupt_dispatch()`, so handlers are ordinary C++.
- Vectors: 0-31 CPU exceptions (crash report, `platform/panic`), 0x20-0x2F masked 8259 PICs (spurious 0x27/0x2F
  ignored), 0x40 APIC timer, 0x41 keyboard, other vectors 0x30+ acknowledged and counted as unexpected, 0xFF APIC
  spurious. Handlers for APIC-delivered vectors must call `lapic::EndOfInterrupt()`.
- Local APIC in whatever mode the firmware left: xAPIC (MMIO) or x2APIC (MSRs). IOAPIC inputs stay masked until a
  driver routes one with `platform::RouteIsaIrq` (MADT overrides applied).
- Crash reports: name, error code, CR2, registers, call chain as image offsets (frame pointers kept), version; the
  log line `decode: addr2line ...` resolves them against `build/uefi/scroller.elf`.

## Sound

- `src/audio/` (shared core, host-tested): voices (square with duty, triangle, saw, 15-bit LFSR noise) with
  attack/decay/sustain/release envelopes, integer only; `Song` parses the text tracker format (`docs/ASSETS.md`);
  `Engine` plays one music song plus two sound-effect slots and mixes to 16-bit mono.
- `platform/sound` picks a device and feeds the engine: Intel HDA if present (`drivers/hda`: CORB/RIRB commands,
  widget graph search from speaker/headphone/line-out pins to a DAC, 48 kHz stereo looping buffer refilled from the
  1 kHz timer interrupt 43 ms ahead of the play position, so no PCI interrupt routing is needed), else a Sound Blaster
  16 (`drivers/sb16`: ISA DMA auto-init over a page below 16 MiB, refilled from its IRQ). Engine calls from the main
  loop run with interrupts disabled because rendering happens in interrupt handlers.
- Volumes: music off at start (`N` toggles it), at 50 % relative to the effects when on, master volume 0..100 % on a decibel scale (5 dB per 10 %, default 60 %) in software, changed in
  steps of 10 % by the volume keys.
- Game sound events: `Player::events` (jump, wall-jump, land, duck) and the goal, collected by `Game::TakeEvents()`.

## Network

- `src/net/stack` (shared core, host-tested): Ethernet II, ARP (8-entry cache, one datagram parked while resolving),
  IPv4 without fragments, ICMP echo, UDP with checksums, a DHCP client (no lease renewal), a TFTP client (`net/tftp`,
  for Ctrl+Alt+R). Single-threaded and
  polled: the main loop feeds received frames and calls `Tick`.
- Drivers implement `nic::Driver` (`drivers/nic.h`: send a frame, receive the next one, link speed), polled with
  32-entry descriptor rings in arena memory (identity mapped, so virtual = physical for DMA): `drivers/e1000`
  (Intel 8254x, QEMU) and `drivers/rtl8168` (Realtek 8168/8111, relies on the firmware's PHY setup).
- `platform/network` picks the first supported Ethernet controller; the address comes from the PXE boot
  (`BootInfo.netboot`), else DHCP. `platform/devlink` is the UDP debug protocol for `tools/devlink.py` (header comment
  in `devlink.h`); it reads the log from the history ring in `drivers/debug_log` (last 32 KiB). The main loop, the
  start-up error screen and the crash screen all poll the network.
- `platform/reload`: hot reload (`tools/devlink.py reload`). The new image is uploaded over the debug link, loaded by
  `lib/pe` into one of two slots reserved before ExitBootServices (alternating, so the running code is never
  overwritten), and called through its normal entry point with a `ReloadHandoff` instead of the system table;
  `efi_main` recognises it and goes straight to `RunGame`. DMA devices (network, USB, sound) are stopped first,
  because the new image reuses the game memory; our page tables live in their own region for the same reason.
  A marker in every image (reload ABI version, handoff size) lets the running game refuse an incompatible build.
- `platform/gdb_stub`: gdb remote protocol over three debug-link commands (packet, wait for stop, interrupt); the
  PC-side `tools/devlink.py gdb` turns them into gdb's usual TCP port. The machine stops inside the exception
  handler (int3 breakpoints written into the code with CR0.WP cleared, trap flag for single steps, an int3 from the
  main loop for Ctrl+C, any CPU exception while gdb is attached) and polls the network there until gdb resumes; the
  frame's registers are what gdb reads and writes, and returning from the handler resumes. A design for a small TCP
  (so gdb could connect directly) is in `docs/PLAN.md`.

## Freestanding C++ rules

- Compile flags (roughly): `-std=c++23` / `-std=c23`, `-ffreestanding -fpie -fvisibility=hidden -fno-plt -fno-exceptions -fno-rtti -fno-stack-protector -fno-stack-check -fcf-protection=none -fno-asynchronous-unwind-tables -mno-red-zone`.
- Link (roughly): `-nostdlib -static-pie -Wl,--no-dynamic-linker -Wl,-z,norelro -T src/boot/efi.ld`, then `objcopy --target=efi-app-x86_64 --subsystem=10`.
- UEFI strings are `char16_t` (`u"..."` literals), so no `-fshort-wchar`.
- No exceptions, no RTTI, no heap `new`/`delete` (allocate from the arena), no global objects with non-trivial constructors (nothing runs them).
- Headers: only the compiler's own freestanding C headers (`<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>`, `<limits.h>`, ...); the target build uses `-nostdinc` so glibc headers cannot leak in. The host's libstdc++ headers are not usable (they depend on glibc), so C++ utilities we need (`Span`, `Min`/`Max`, placement new, type traits) live in `lib/`, mostly as thin wrappers over compiler builtins. A real `x86_64-elf` cross GCC would make libstdc++'s freestanding headers available if we ever want them.
- The compiler may emit calls to `memset`/`memcpy`/`memmove`/`memcmp`; `lib/` provides them.
- SSE is enabled by UEFI and may be used; floats are allowed, but game physics uses fixed point.

## Known risks

- **USB-only keyboards**: handled by our xHCI driver and HID boot keyboards (`drivers/xhci`, `platform/usb`); no hubs yet.
- **Framebuffer speed on real hardware**: the GOP framebuffer is typically mapped uncached; writing ~2.8 MB/frame (x3; now ~4.9 MB at x4) gave only a few FPS on an HP Pavilion (present 167 ms/frame uncached, 0.40 ms with WC; render 0.05 ms; CPU at 3.9 GHz). `x86::EnableWriteCombining` reprograms PAT entry 7 to WC and points the framebuffer's page table entries at it (splitting large pages), after ExitBootServices only. F1 shows a performance overlay.
- **GOP pixel formats**: handle BGRX and RGBX; bitmask formats only if we meet one. `BltOnly` modes (no framebuffer) are not supported.
- **No vsync** under GOP: frames are copied at times unrelated to the display refresh, so a tear line drifts across the screen. `platform::Vsync` picks what the hardware offers and waits for a new frame before presenting: on Intel GPUs the enabled pipe's scanline counter (PIPEDSL in BAR0, `drivers/intel_display`), otherwise the legacy VGA retrace bit (port 0x3DA) if it shows a steady 30-125 Hz rhythm (QEMU's fake bit is rejected; the UHD 630 in the HP Pavilion does not emulate it). Pipe selection uses the counter that actually runs: on Gen9 an eDP panel goes through the eDP transcoder and all PIPECONF registers read 0. Verified on the HP Pavilion (i5-8300H, UHD 630): no tearing, smooth. AMD would need its own CRTC scanline register. If vsync is off, F1 shows the Intel diagnostics (BAR0, pipe, lines, rate). The F1 overlay shows the CPU, the GPU's PCI ID and the vsync state.
- **Sound on real hardware**: Sound Blaster 16 exists only in QEMU/emulators; real PCs need Intel HDA.
- **Runtime disk access on real hardware**: likely NVMe, or USB mass storage when booting from a stick; both are substantial drivers.
