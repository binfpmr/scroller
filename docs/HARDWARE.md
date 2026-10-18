# Test hardware

What we know about the machines the game runs on, from the firmware setup screen, the F1 overlay and the logs.
Serial numbers and UUIDs are left out on purpose.

## HP Pavilion Gaming Laptop 15-cx0xxx (main test PC)

| Part | Details | Driver / notes |
|---|---|---|
| Model | 15-cx0xxx, system board ID 8478, built 2019 | |
| Firmware | Insyde H2O, BIOS F.16 (setup: Esc / F10 at power-on, boot menu F9) | Secure Boot off; PXE IPv4 boot works |
| CPU | Intel Core i5-8300H (Coffee Lake-H, 4 cores / 8 threads), 2.3 GHz, turbo ~3.9 GHz | TSC 2304 MHz, invariant |
| Memory | 16 GB | arena of 32 MiB at ~0x6078_9000 |
| Interrupts | ACPI MADT: 8 CPUs, xAPIC, one IOAPIC (GSIs 0..119), 8259 present | LAPIC timer input 1500 kHz after /16 |
| GPU | Intel UHD 630 `8086:3E9B` at 00:02.0, BAR0 0xB2000000 | eDP panel on pipe A (via the eDP transcoder, PIPECONF reads 0), 1140 lines, 60 Hz; vsync from the scanline counter |
| Display | GOP modes 1920x1080, 640x480, 800x600, 1024x768, 1280x1024; firmware leaves 1024x768 | scale 3, framebuffer write-combined (present 0.4 ms instead of 167 ms) |
| Audio | HDA controller `8086:A348`, codec Realtek ALC295 `10EC:0295` | speaker pin 20 and headphone pin 33 -> DAC 2 |
| Wired network | Realtek RTL8168H `10EC:8168` (chip version 541) at 05:00.0 | `drivers/rtl8168`: set up twice after the firmware (the first setup stops after one ring pass) |
| Wi-Fi | Realtek RTL8822BE `10EC:B822` at 02:00.0 | not supported (needs firmware blobs, WPA) |
| Keyboard | internal keyboard on PS/2 (i8042) | IRQ 1; media keys F6-F8 send codes the game does not use yet |
| USB | Intel 300-series PCH xHCI (to be confirmed, expected `8086:A36D`) | M7 |

## QEMU (development)

q35, OVMF, `-cpu host` with KVM (pinned to E-cores on hybrid CPUs, see `tools/qemu.py`), VGA with EDID 960x720,
intel-hda + hda-output (or SB16), e1000 on user-mode networking (debug link port forwarded), PS/2 keyboard.
