# Architecture

A small 2D platformer that boots directly on x86_64 hardware via UEFI. No OS, no libc, no third-party runtime.

## Decisions

| Topic | Decision | Why |
|---|---|---|
| Boot | UEFI x86_64 application (`BOOTX64.EFI`) | Boots on modern PCs and QEMU/OVMF; firmware gives us long mode, paging, a framebuffer and file access for free. Legacy BIOS/Mode X is not planned. |
| Firmware use | UEFI as a *loader*, then `ExitBootServices` | UEFI input has no key-release events and would fight us over IRQs/DMA (sound). After exit we own the machine. |
| Video | Game renders into a 320x240 XRGB8888 buffer in RAM; `present()` integer-scales it into the GOP framebuffer | No real Mode X under UEFI. 32-bit colour, no palette emulation. |
| Video mode | Highest integer scale (capped at x3, `gfx::kMaxScale`), then an exact fit (960x720, 640x480), then the firmware's current mode; centered with black borders when not exact (`gfx::ChooseVideoMode`) | Pixel-perfect scaling with whatever modes the firmware offers. Preferring the current mode on ties keeps a laptop on its native panel mode rather than a stretched low resolution. The cap bounds framebuffer bandwidth. |
| Toolchain | GCC (`gcc`, `g++`) + GNU binutils (`as`, `ld`, `objcopy`) as a freestanding ELF toolchain, no OS target assumed; clang (`--target=x86_64-unknown-none-elf`) as an optional second compiler | No EDK2, no gnu-efi. A real `x86_64-elf` cross compiler can be plugged in via a toolchain prefix if wanted; the host GCC with freestanding flags produces equivalent code. |
| Executable format | Link a static PIE ELF at base 0 (`scroller.elf`, keeps DWARF for gdb), convert to PE with `objcopy --target=efi-app-x86_64` (`scroller.efi`); `crt0.S` applies the ELF `R_X86_64_RELATIVE` relocations itself before any C++ runs | UEFI loads PE images at an arbitrary address. ELF section addresses equal the PE RVAs, so debugging uses the ELF at a known offset. |
| Calling convention | Our code uses the System V ABI; UEFI functions and `efi_main`'s firmware entry use the Microsoft x64 ABI (`EFIAPI` = `__attribute__((ms_abi))`; `crt0.S` bridges the entry) | Standard approach for GCC on ELF. |
| Languages | Freestanding C++23 for almost everything, C23 where it fits (self-contained low-level pieces), GNU `as` for what needs assembly (entry/relocation, ISR stubs, descriptor loads). AT&T syntax everywhere, including inline asm | One main language keeps it simple. |
| UEFI headers | Our own minimal definitions (`src/uefi/`), only what we use | A few hundred lines; no external dependency. |
| Timing | Fixed 60 Hz game step; TSC calibrated against UEFI `Stall()` before exit; no interrupts needed in v1 | Simplest thing that works; interrupts come with sound. |
| Input (v1) | Poll the i8042 PS/2 controller (scan code set 1/2 make/break codes) | QEMU's default keyboard; most laptop built-in keyboards are also PS/2 behind the EC. USB (xHCI + HID boot keyboard) is a later milestone. |
| Input fallback | Build option `SCROLLER_UEFI_INPUT` (preset `uefi-input`): stay in boot services, read `ConIn`, treat a key as held for 550 ms after its first event and 150 ms after each typematic repeat | Lets us try real USB-only hardware before the xHCI driver exists. Input feel is worse; debug aid only. |
| Assets | Plain-text sources: levels as one character per tile, placeholder sprites as text art (one character per palette colour). Converted at build time by host Python tools into simple binary blobs, copied onto the ESP, loaded into RAM before `ExitBootServices` | Diff-friendly, editable in any editor, no level editor needed yet. No disk driver needed for v1. |
| Controls | Left/Right move, Up or Space jump, Down ducks, Esc reboots; F1 performance overlay, F2 framebuffer write-combining on/off | |
| Exit | Esc -> `ResetSystem(EfiResetCold)` (UEFI runtime services stay callable after `ExitBootServices`); fallback: 8042 reset pulse, then triple fault | Works on real hardware and QEMU. |
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
  -> kernel_main(BootInfo&)             [platform/]  we own the machine
       - (later) own GDT/IDT, exception handlers, timer, sound, USB, disk
       - main loop: poll input -> game.update() at fixed 60 Hz -> game.render() -> present()
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
  lib/           freestanding runtime: mem*/str* functions, fmt/log formatting, arena allocator, fixed-point math
tools/           host tools: qemu.py runner, embed_text.py, gdb helper, levelcheck (C++), later asset conversion
assets/          text-art sprites, palette, text levels
scripts/         build-image / run-qemu / smoke-test shell scripts
tests/           host unit tests for game/, gfx/, lib/
```

Dependency rule: `game/` and `gfx/` depend only on `lib/` and the `platform/` interface headers. Only `boot/`, `arch/`, `drivers/` and `platform/` implementations touch hardware or UEFI. This is what makes the host build and tests possible.

## Game core

- Fixed timestep, 60 updates/s. Render once per update (no interpolation in v1).
- Positions/velocities in 24.8 fixed point (`lib::Fixed`, 1/256 px) so physics is deterministic and identical on host and target.
- World = tilemap of 16x16 tiles (`game::Level`), larger than the screen in both axes; outside the map counts as walls and floor, open above. Camera (`game::Camera`) keeps the player in a ±24 px dead zone around the screen centre and is clamped to level bounds, so scrolling works in all 4 directions.
- Player (`game/player.*`): 12x14 hitbox, AABB collision against solid tiles resolved one axis at a time. Speeds stay below one tile per tick, so checking the leading edge's tile row/column after a move is enough (no tunnelling). Run with acceleration/friction, jump with buffering, coyote time and variable height (release early = lower jump), gravity with terminal velocity. Down on the ground ducks (look only); a jump while ducking or up to 10 ticks after is a duck-jump (50% higher). Jumping in the air while touching a side wall (or up to 6 ticks after) is a wall-jump: a full jump from zero horizontal speed, with the input overridden as "jump + away from the wall held" until the top of the arc. All tuning constants are in `PlayerTuning`; changing them changes which platforms are reachable, so re-run `levelcheck` and the tests.
- Rendering: draw only visible tiles, then sprites, into the 320x240 surface. Until M3 the graphics are procedural placeholders (`game/render.cpp`).
- Level design check: `levelcheck` (host tool, `tools/levelcheck/`) searches all resting positions reachable from the start with the real physics and prints the level with reachable spots marked.

## Freestanding C++ rules

- Compile flags (roughly): `-std=c++23` / `-std=c23`, `-ffreestanding -fpie -fvisibility=hidden -fno-plt -fno-exceptions -fno-rtti -fno-stack-protector -fno-stack-check -fcf-protection=none -fno-asynchronous-unwind-tables -mno-red-zone`.
- Link (roughly): `-nostdlib -static-pie -Wl,--no-dynamic-linker -Wl,-z,norelro -T src/boot/efi.ld`, then `objcopy --target=efi-app-x86_64 --subsystem=10`.
- UEFI strings are `char16_t` (`u"..."` literals), so no `-fshort-wchar`.
- No exceptions, no RTTI, no heap `new`/`delete` (allocate from the arena), no global objects with non-trivial constructors (nothing runs them).
- Headers: only the compiler's own freestanding C headers (`<stdint.h>`, `<stddef.h>`, `<stdarg.h>`, `<stdbool.h>`, `<limits.h>`, ...); the target build uses `-nostdinc` so glibc headers cannot leak in. The host's libstdc++ headers are not usable (they depend on glibc), so C++ utilities we need (`Span`, `Min`/`Max`, placement new, type traits) live in `lib/`, mostly as thin wrappers over compiler builtins. A real `x86_64-elf` cross GCC would make libstdc++'s freestanding headers available if we ever want them.
- The compiler may emit calls to `memset`/`memcpy`/`memmove`/`memcmp`; `lib/` provides them.
- SSE is enabled by UEFI and may be used; floats are allowed, but game physics uses fixed point.

## Known risks

- **USB-only keyboards** need an xHCI + HID driver after `ExitBootServices` (milestone M6). Until then: PS/2 or the `SCROLLER_UEFI_INPUT` fallback.
- **Framebuffer speed on real hardware**: the GOP framebuffer is typically mapped uncached; writing ~2.8 MB/frame at x3 gave only a few FPS on an HP Pavilion (present 167 ms/frame uncached, 0.40 ms with WC; render 0.05 ms; CPU at 3.9 GHz). `x86::WriteCombining` reprograms PAT entry 7 to WC and points the framebuffer's page table entries at it (splitting large pages), after ExitBootServices only. F1 shows a performance overlay, F2 toggles WC for comparison.
- **GOP pixel formats**: handle BGRX and RGBX; bitmask formats only if we meet one. `BltOnly` modes (no framebuffer) are not supported.
- **No vsync** under GOP: expect some tearing; acceptable.
- **Sound on real hardware**: Sound Blaster 16 exists only in QEMU/emulators; real PCs need Intel HDA.
- **Runtime disk access on real hardware**: likely NVMe, or USB mass storage when booting from a stick; both are substantial drivers.
