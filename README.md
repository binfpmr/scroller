# scroller

A small 2D platformer that boots on bare-metal x86_64 via UEFI: 320x240 game screen scaled to the display, tile-based levels scrolling in all four directions, running, jumping and gravity. No OS, no libc. Freestanding C++23/C23 and a bit of GNU assembly, built with CMake + GCC/binutils, run in QEMU or from a USB stick.

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): decisions, boot phases, source layout, freestanding rules
- [docs/PLAN.md](docs/PLAN.md): milestones and progress
- [docs/WORKFLOW.md](docs/WORKFLOW.md): prerequisites, build, run, debug, writing to USB, development loop
- [docs/ASSETS.md](docs/ASSETS.md): level and sprite formats, `levelcheck`
- [docs/REFERENCES.md](docs/REFERENCES.md): manuals and links

Quick start (prerequisites in [docs/WORKFLOW.md](docs/WORKFLOW.md)):

```sh
cmake --preset uefi && cmake --build --preset uefi
scripts/run-qemu.sh
```

Status: M3 done (sprites, level 1 with a goal, assets loaded from the boot volume); runs on real hardware (HP Pavilion). M4 (interrupts, diagnostics) next.
