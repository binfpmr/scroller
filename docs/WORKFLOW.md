# Workflow

## Prerequisites (Ubuntu)

```sh
sudo apt install gcc g++ binutils cmake ninja-build mtools qemu-system-x86 ovmf gdb \
                 python3 gdisk dosfstools clang-format clangd
sudo apt install clang            # optional, for the uefi-clang preset
```

VS Code extensions (recommended in `.vscode/extensions.json`): clangd, CMake Tools, C/C++ (only for the gdb debugger front end; its IntelliSense is disabled in favour of clangd), x86 assembly syntax (GAS).

## Build

CMake presets, all using Ninja, output under `build/<preset>/`:

| Preset | What | Output |
|---|---|---|
| `uefi` | Freestanding target build with GCC | `build/uefi/scroller.elf` (with symbols), `scroller.efi` (PE), `scroller.img` (GPT disk), `esp.img` (FAT ESP only) |
| `uefi-input` | Like `uefi`, but stays in boot services and uses UEFI keyboard input (USB-only machines) | `build/uefi-input/...` |
| `uefi-clang` | Same with clang (`x86_64-unknown-none-elf`), GNU ld/objcopy | `build/uefi-clang/...` |
| `host` | Game/gfx/lib code + unit tests for Linux | `build/host/tests/...` |

```sh
cmake --preset uefi && cmake --build --preset uefi
cmake --preset host && cmake --build --preset host && ctest --preset host
ctest --preset host -R player         # single test by name regex
build/host/levelcheck assets/levels/level1.txt # reachable spots 'o', dead ends 'x', reachable goal 'g'
```

`compile_commands.json` is exported by the `uefi` preset; clangd reads it from `build/uefi/`.

## Run

```sh
scripts/run-qemu.sh                 # window, serial on stdio, debug log -> build/uefi/debugcon.log
scripts/run-qemu.sh --debug         # also: gdb stub on :1234; the game waits in efi_main for the debugger
scripts/smoke-test.sh               # headless; waits for "boot ok" in the log, writes build/uefi/smoke.png
```

All three wrap `tools/qemu.py` (`run`, `debug`, `smoke`); extra options: `--build build/uefi-clang` to run another preset's image, `--display` (default `gtk-gl`, a GPU-drawn window; plain `gtk` scales in software and slows the guest on fractionally scaled screens; also `sdl`, `sdl-gl`), `--resolution WxH` for the display's preferred mode (default 960x720; OVMF starts in it, like a native panel, e.g. try `1920x1080` to see the laptop case), `--headless` for run/debug, `--expect TEXT`, `--timeout S`, `--screenshot PATH` for smoke. The smoke test takes the screenshot on failure too, so a hang shows where it is stuck.

Sound: `--sound hda` (default, like real PCs), `--sound sb16` or `--sound none`. Windowed runs play through PipeWire;
smoke tests record to `build/uefi/audio.wav` (stereo, via the QEMU monitor's `wavcapture`). Music and effects can also
be rendered on the host: `build/host/render_audio <song.txt> <out.wav>` (see `docs/ASSETS.md`).

Smoke tests can drive the game with a key script (`tools/qemu.py smoke --help`):

```sh
scripts/smoke-test.sh --keys "shot:start right:2 down:1 shot:moved esc expect:reboot expect:efi_main"
```

Taps/holds use QEMU key names (`left`, `right`, `up`, `down`, `spc`, `esc`), `+KEY`/`-KEY` press/release separately, `shot:NAME` writes `build/uefi/smoke-NAME.png`, `expect:TEXT` waits for TEXT in the debug log after the previous match. The game logs timing and the camera position every 5 s, which makes scripted checks easy.

**Hybrid Intel CPUs:** with KVM, OVMF intermittently hangs at the TianoCore logo when QEMU's vCPU thread migrates between P- and E-cores (seen on an i5-1334U, kernel 7.0, QEMU 10.2). `tools/qemu.py` therefore pins itself to the E-cores when `/sys/devices/cpu_atom/cpus` exists. Without KVM it falls back to TCG (slower, never hung).

QEMU basics used: `-machine q35`, OVMF firmware (`/usr/share/OVMF/OVMF_CODE_4M.fd` + a copy of `OVMF_VARS_4M.fd` in `build/`), `-drive format=raw,file=build/uefi/scroller.img`, `-debugcon file:...` on port 0xE9, `-serial stdio`. Extra devices are added as milestones need them (`-device sb16`, `-device qemu-xhci -device usb-kbd`, `-device nvme`, ...).

## Debug

1. `scripts/run-qemu.sh --debug` sets the QEMU debugcon readback value to 0xDB. `efi_main` reads port 0xE9, sees it, logs its image base and `waiting for debugger`, and spins on `scroller_debugger_wait` (real hardware reads 0xFF and never waits).
2. gdb connects to `:1234`; the `scroller-attach` command (`tools/gdb/scroller.py`) reads the image base from `debugcon.log`, runs `add-symbol-file build/uefi/scroller.elf -o <base>` (the ELF is linked at 0, so the offset is the image base) and clears the flag.
3. Breakpoints in source then work as usual.

VS Code: launch config *Debug in QEMU* (F5) does all of this. Command line, from the repository root:

```sh
scripts/run-qemu.sh --debug &          # wait for "qemu: waiting for gdb"
gdb -x tools/gdb/gdbinit               # connects, loads symbols, releases efi_main; then e.g. `break DrawTestPattern`, `continue`
```

## Crash reports

A CPU exception shows a blue screen with the registers and the call chain (also in `debugcon.log`). Decode it:

```sh
addr2line -f -C -e build/uefi/scroller.elf 0x4daa 0x312c 0x1055   # RIP offset, then the CALLED FROM offsets
```

The log prints this command ready to copy (`decode: ...`). Use the `scroller.elf` of the same version (shown on the
crash screen).

## Network boot (test PC without USB sticks)

The test PC boots the current build straight from this PC over Ethernet (UEFI PXE):

```sh
scripts/netboot-server.sh proxy wlp1s0             # test PC wired to the same LAN/router as this PC
scripts/netboot-server.sh direct enx00e04cdc42a1   # cable from this PC's USB Ethernet adapter to the test PC
```

It runs dnsmasq in the foreground (sudo; Ctrl+C stops) and serves `build/uefi/scroller.efi`; rebuild, then reboot
the test PC (Esc in the game) to get the new build. On the test PC: Secure Boot off, network/PXE IPv4 boot enabled
and first in the boot order (or picked from the boot menu). F1 shows `NETBOOT <ip> FROM <server>` when it worked.
In QEMU: `scripts/run-qemu.sh --netboot` (QEMU's own DHCP/TFTP server, no dnsmasq needed).

## Debug link (log, screenshots, keys, reboot over the network)

When the game has a supported Ethernet controller (e1000 in QEMU, RTL8168 on the HP Pavilion) it takes the PXE
address, or asks DHCP, and listens on UDP port 5555 (F1: `NET` line with link speed and address).
`tools/devlink.py` is the PC side:

```sh
export SCROLLER_TARGET=192.168.178.40    # the test PC (F1 NET line, or: tools/devlink.py discover); QEMU: unset
tools/devlink.py info                    # version, boot id, image base, screen address
tools/devlink.py log --all               # log since boot, then live; follows reboots; Ctrl+C stops
tools/devlink.py shot out.png            # screenshot of the 320x240 game screen
tools/devlink.py keys "right:2 jump wait:1 shot:a.png"   # inject keys (left right down jump esc f1 volup voldown mute)
tools/devlink.py peek 0x... 64           # memory (unmapped pages are refused, MMIO reads are not)
tools/devlink.py poke 0x... 90 90
tools/devlink.py reboot                  # with network boot first: loads the current build
```

It keeps working on the crash screen, so a crash report can be read with `log --all`. The loop on real hardware:
rebuild, `tools/devlink.py reboot`, `tools/devlink.py log` / `shot`. In QEMU the port is forwarded from localhost
(any `tools/qemu.py` run), so the same commands work without `SCROLLER_TARGET`. No authentication: use it on a
trusted network only.

## Write to a USB stick / disk

**Double-check the device name; `dd` will overwrite whatever you point it at.**

```sh
lsblk                                              # find the stick, e.g. /dev/sdX (NOT a partition like sdX1)
sudo dd if=build/uefi/scroller.img of=/dev/sdX bs=4M conv=fsync status=progress
```

Into an existing partition instead of a whole disk: either copy `build/uefi/scroller.efi` as `EFI/BOOT/BOOTX64.EFI` onto an existing FAT ESP (the assets are inside it), or `dd` `build/uefi/esp.img` into a partition at least as large as the image. Then boot it from the firmware's boot menu (Secure Boot must be off; the binary is unsigned).

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
