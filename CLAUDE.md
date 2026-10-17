# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Bare-metal x86_64 UEFI platformer. Read `docs/ARCHITECTURE.md` (decisions and layering) and `docs/PLAN.md` (current milestone) before making changes; `docs/WORKFLOW.md` has all commands.

## Commands

```sh
cmake --preset uefi && cmake --build --preset uefi      # target build -> build/uefi/scroller.efi, scroller.img
cmake --preset host && cmake --build --preset host       # host build of game/gfx/lib + tests
ctest --preset host                                      # all host unit tests
ctest --preset host -R <regex>                           # single test
scripts/smoke-test.sh                                    # headless QEMU boot; check build/uefi/debugcon.log and smoke.png
scripts/run-qemu.sh [--debug]                            # interactive run (user's display), gdb stub with --debug
clang-format -i <files>                                  # Google style, 120 columns (.clang-format)
```

(Presets and scripts are created in milestone M0; if they don't exist yet, M0 is the task.)

## Rules that are easy to get wrong

- Target is `x86_64-unknown-windows` PE/COFF with the Microsoft x64 ABI (same as UEFI). NASM files use `-f win64` and must follow that ABI.
- Freestanding: no exceptions, RTTI, STL, libc, heap `new`, or global objects with non-trivial constructors. Memory comes from the arena allocated before `ExitBootServices`.
- Nothing after `ExitBootServices` may call UEFI boot services; everything needed later goes into `BootInfo`.
- `game/` and `gfx/` must not include anything from `boot/`, `uefi/`, `arch/`, `drivers/`, so they keep building for the host tests.
- Game physics uses fixed point, not floats.

## Working loop

Plan a small step -> implement -> build -> host tests and/or `scripts/smoke-test.sh` (read the log, look at the screenshot) -> refine -> small commit -> tick `docs/PLAN.md`. Never run `dd` or write to block devices; give the user the command instead (see `docs/WORKFLOW.md`).
