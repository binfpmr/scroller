# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Bare-metal x86_64 UEFI platformer. Read `docs/ARCHITECTURE.md` (decisions and layering) and `docs/PLAN.md` (current milestone) before making changes; `docs/WORKFLOW.md` has all commands.

## Commands

```sh
cmake --preset uefi && cmake --build --preset uefi      # target build -> build/uefi/scroller.efi, scroller.img
cmake --preset host && cmake --build --preset host       # host build of game/gfx/lib + tests
ctest --preset host                                      # all host unit tests
ctest --preset host -R <regex>                           # single test
scripts/smoke-test.sh [--keys "right:2 shot:x esc ..."]     # headless QEMU boot + key script; read debugcon.log and the PNGs
scripts/run-qemu.sh [--debug] [--netboot]                # interactive run (user's display), gdb stub with --debug
tools/devlink.py info|log|shot F.png|keys "..."|reboot   # debug link to the running game (QEMU: localhost; hardware: SCROLLER_TARGET=ip)
tools/devlink.py reload   # start the freshly built image in the running game without a reboot (QEMU or SCROLLER_TARGET)
tools/devlink.py gdb   # gdb server on :2345 -> game's stub; gdb -batch -ex 'source tools/gdb/scroller.py' -ex 'target remote :2345' -ex scroller-net-attach ...
build/host/render_audio <song.txt> <out.wav>             # render a song with the game's synth (to listen/analyse)
build/host/levelcheck <level.txt>                        # which spots of a level the player can reach (after host build)
clang-format -i <files>                                  # Google style, 120 columns (.clang-format)
```

Debugging without a window (how Claude can use gdb):

```sh
python3 tools/qemu.py debug --headless > /tmp/qd.txt 2>&1 &     # wait until /tmp/qd.txt says "qemu: waiting for gdb"
gdb -batch -x tools/gdb/gdbinit -ex 'break SomeFunction' -ex continue -ex bt -ex 'info locals' -ex kill
```

The debug log (`build/uefi/debugcon.log`) is the main diagnostic; `Log()` writes to it. Screenshots (`smoke.png`) can be viewed directly.

Killing background tools: the pattern must not match the shell running pkill, e.g. `pkill -f 'tools/devlink.py g[d]b'`.

## Rules that are easy to get wrong

- Toolchain is GCC + GNU binutils (clang optional, no NASM). Output is a static-PIE ELF linked at 0, converted to PE by `objcopy`; `src/boot/crt0.S` self-relocates. Any absolute address in data needs a `R_X86_64_RELATIVE` reloc, which crt0 handles; other dynamic reloc types are a link error to fix, not to ignore.
- Our code is System V ABI; anything UEFI calls or that calls UEFI is `EFIAPI` (`__attribute__((ms_abi))`).
- Assembly is GNU `as` in AT&T syntax (the default), inline asm too.
- Freestanding C++23 / C23: no exceptions, RTTI, STL, libc, heap `new`, or global objects with non-trivial constructors. Memory comes from the arena allocated before `ExitBootServices`.
- Nothing after `ExitBootServices` may call UEFI boot services; everything needed later goes into `BootInfo`.
- Interrupts are on after setup (`platform/interrupt_controllers`); code shared with an interrupt handler needs
  volatile/ring-buffer care. Handlers for APIC vectors must send `lapic::EndOfInterrupt()`.
- `game/` and `gfx/` must not include anything from `boot/`, `uefi/`, `arch/`, `drivers/`, so they keep building for the host tests.
- Game physics uses fixed point (`lib::Fixed`, 24.8), not floats. After changing `PlayerTuning` or a level, run the host tests (they include `levelcheck` reachability).
- Hot reload hands the previous image's `BootInfo` to the new one: when `BootInfo` changes, its size usually changes
  too (the game then refuses the reload and the tool reboots); if only its meaning changes, bump `kReloadVersion` in
  `platform/reload.h`. Anything that does DMA into the arena must be stopped in `ExecuteReload`.
- Assets are plain text under `assets/` (formats: `docs/ASSETS.md`), packed into `assets.pak` and parsed at boot. Every level must have a reachable goal and no dead ends: the host tests run `levelcheck` on every
  `assets/levels/*.txt` as its own test (`levelcheck_<name>`; level 1 with `--basic`: plain jumps only). F2 in the game
  skips to the next level for a quick look.

## Working loop

Plan a small step -> implement -> build -> host tests and/or `scripts/smoke-test.sh` (read the log, look at the screenshot) -> refine -> small commit -> tick `docs/PLAN.md`. Never run `dd` or write to block devices; give the user the command instead (see `docs/WORKFLOW.md`).
