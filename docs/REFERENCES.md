# References

Local copies of manuals go in `refs/` (git-ignored). File names below are suggestions so tooling and notes can refer to them.

## Needed soon

| What | For | Suggested file |
|---|---|---|
| UEFI Specification 2.10 / 2.11 (uefi.org) | GOP, SimpleFileSystem, LoadedImage, memory map, ExitBootServices | `refs/UEFI_Spec_2_10_Aug29.pdf`, `refs/UEFI_Spec_Final_2.11.pdf` |
| Intel 64 and IA-32 SDM, Volume 3 (System Programming) | GDT/IDT, exceptions, APIC, PAT, TSC | `refs/325384-093-sdm-vol-3abcd.pdf` |

## Needed later

| What | Milestone | Suggested file |
|---|---|---|
| ACPI Specification 6.5 | done (MADT); later: power off, more tables | `refs/ACPI_6_5.pdf` |
| Intel High Definition Audio Specification 1.0a | done (M5) | `refs/hda.pdf` |
| xHCI Specification 1.2 (Intel) | M7 USB | `refs/xhci-1.2.pdf` |
| USB 2.0 specification, chapter 9 (device framework, descriptors, standard requests) | M7 | `refs/usb20.pdf` |
| USB HID 1.11 (boot protocol, appendix B) + HID Usage Tables 1.5 (keyboard page 0x07, gamepads) | M7 | `refs/hid1_11.pdf`, `refs/hut.pdf` |
| USB Mass Storage Class Bulk-Only Transport 1.0 + SCSI commands (READ(10), READ CAPACITY) | M8 (USB stick) | `refs/usbmassbulk_10.pdf` |
| NVM Express Base Specification 2.x | M8 option | `refs/nvme.pdf` |
| PCI Express base (config space, capabilities, MSI) | as needed | `refs/pci.pdf` |

Not public and not needed so far: Realtek RTL8168 and ALC295 datasheets (Linux `r8169` and `snd-hda-codec-realtek`
are the practical references), Intel PCH datasheets (the xHCI specification covers the USB controller).

## Online

- Linux sources as working references: `drivers/usb/host/xhci*.c`, `drivers/hid/usbhid/usbkbd.c` (boot keyboard),
  `drivers/net/ethernet/realtek/r8169_main.c`, `sound/pci/hda/`.
- OSDev wiki: [UEFI](https://wiki.osdev.org/UEFI), [GOP](https://wiki.osdev.org/GOP), [PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard), [Sound Blaster 16](https://wiki.osdev.org/Sound_Blaster_16), [xHCI](https://wiki.osdev.org/EXtensible_Host_Controller_Interface), [AHCI](https://wiki.osdev.org/AHCI)
- `../flappy-512`: Flappy Bird in a 512-byte BIOS boot sector (mode 13h, PIT timing); style reference for tiny bare-metal games.
