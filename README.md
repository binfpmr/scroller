# scroller

A small 2D platformer that boots on bare-metal x86_64 via UEFI: 320x240 game screen scaled to the display, tile-based levels scrolling in all four directions, running, jumping and gravity. No OS, no libc. Freestanding C++23/C23 and a bit of GNU assembly, built with CMake + GCC/binutils, run in QEMU or from a USB stick.

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md): decisions, boot phases, source layout, freestanding rules
- [docs/PLAN.md](docs/PLAN.md): milestones and progress
- [docs/WORKFLOW.md](docs/WORKFLOW.md): prerequisites, build, run, debug, writing to USB, development loop
- [docs/REFERENCES.md](docs/REFERENCES.md): manuals and links

Status: planning, nothing builds yet (milestone M0 next).
