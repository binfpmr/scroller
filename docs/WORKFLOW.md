# Workflow

> Most commands below refer to files created in milestone M0 (CMake presets, `scripts/`). Until then this is the intended setup.

## Prerequisites (Ubuntu)

```sh
sudo apt install gcc g++ binutils cmake ninja-build mtools qemu-system-x86 ovmf gdb \
                 python3 gdisk dosfstools clang-format clangd
sudo apt install clang lld        # optional, for the uefi-clang preset
```

VS Code extensions (recommended in `.vscode/extensions.json`): clangd, CMake Tools, C/C++ (only for the gdb debugger front end; its IntelliSense is disabled in favour of clangd), x86 assembly syntax (GAS).

## Build

CMake presets, all using Ninja, output under `build/<preset>/`:

| Preset | What | Output |
|---|---|---|
| `uefi` | Freestanding target build with GCC | `build/uefi/scroller.elf` (with symbols), `scroller.efi` (PE), `scroller.img` (GPT disk), `esp.img` (FAT ESP only) |
| `uefi-clang` | Same with clang (`x86_64-unknown-none-elf`), GNU ld/objcopy | `build/uefi-clang/...` |
| `host` | Game/gfx/lib code + unit tests for Linux | `build/host/tests/...` |

```sh
cmake --preset uefi && cmake --build --preset uefi
cmake --preset host && cmake --build --preset host && ctest --preset host
ctest --preset host -R collision      # single test by name regex
```

`compile_commands.json` is exported by the `uefi` preset; clangd reads it from `build/uefi/`.

## Run

```sh
scripts/run-qemu.sh                 # window, PS/2 keyboard, debug log -> build/uefi/debugcon.log
scripts/run-qemu.sh --debug         # also: gdb stub on :1234, CPU paused until debugger attaches
scripts/smoke-test.sh               # headless; boots, waits, writes build/uefi/smoke.png + log, non-zero exit on failure
```

QEMU basics used: `-machine q35`, OVMF firmware (`/usr/share/OVMF/OVMF_CODE_4M.fd` + a copy of `OVMF_VARS_4M.fd` in `build/`), `-drive format=raw,file=build/uefi/scroller.img`, `-debugcon file:...` on port 0xE9, `-serial stdio`. Extra devices are added as milestones need them (`-device sb16`, `-device qemu-xhci -device usb-kbd`, `-device nvme`, ...).

## Debug

1. `scripts/run-qemu.sh --debug` (VS Code task *QEMU (debug)* does this).
2. In debug builds `efi_main` logs its image base to the debug log and spins on a `volatile` flag until a debugger clears it.
3. The gdb helper (`tools/gdb/`) reads the image base from the log, runs `add-symbol-file build/uefi/scroller.elf -o <base>` (the ELF is linked at 0, so the offset is the image base), clears the flag, and continues.
4. VS Code launch config *Debug in QEMU* does steps 1 to 3; breakpoints in source then work as usual.

## Write to a USB stick / disk

**Double-check the device name; `dd` will overwrite whatever you point it at.**

```sh
lsblk                                              # find the stick, e.g. /dev/sdX (NOT a partition like sdX1)
sudo dd if=build/uefi/scroller.img of=/dev/sdX bs=4M conv=fsync status=progress
```

Into an existing partition instead of a whole disk: either copy `EFI/BOOT/BOOTX64.EFI` and the `scroller/` asset folder onto an existing FAT ESP, or `dd` `build/uefi/esp.img` into a partition at least as large as the image. Then boot it from the firmware's boot menu (Secure Boot must be off; the binary is unsigned).

## Development loop

Every change, by human or by Claude, goes through the same loop:

1. **Plan:** pick the next unchecked item in `docs/PLAN.md`; if it is bigger than one or two commits, split it first.
2. **Implement** the smallest step that can be observed working.
3. **Build** `cmake --build --preset uefi` (warnings are errors).
4. **Test**
   - `ctest --preset host` for anything in `game/`, `gfx/`, `lib/`;
   - `scripts/smoke-test.sh`, then look at the log and the screenshot;
   - for input-driven behaviour, the smoke test can inject keys through the QEMU monitor (`sendkey`) and take screenshots at points in time.
5. **Refine:** fix, simplify, run `clang-format`.
6. **Commit:** small, focused commits; tick the item in `docs/PLAN.md`.
7. Every few milestones (or when something hardware-sensitive changes): **real hardware run** from a USB stick.
