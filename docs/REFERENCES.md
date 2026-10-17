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
| ACPI Specification 6.5 | M4 (MADT, HPET tables), reset | `refs/ACPI_6_5.pdf` |
| IA-PC HPET Specification 1.0a | M4 | `refs/hpet.pdf` |
| Creative "Sound Blaster Series Hardware Programming Guide" | M5 | `refs/sb-hw-prog-guide.pdf` |
| Intel High Definition Audio Specification 1.0a | M5 | `refs/hda.pdf` |
| xHCI Specification 1.2 | M6 | `refs/xhci-1.2.pdf` |
| USB 2.0 spec (ch. 9: device framework) and USB HID 1.11 + HID Usage Tables | M6 | `refs/usb20.pdf`, `refs/hid1_11.pdf`, `refs/hut.pdf` |
| PCI Local Bus / PCIe base (config space) | M6, M7 | `refs/pci.pdf` |
| Serial ATA AHCI 1.3.1 | M7 | `refs/ahci-1.3.1.pdf` |
| NVM Express Base Specification | M7 | `refs/nvme.pdf` |

## Online

- OSDev wiki: [UEFI](https://wiki.osdev.org/UEFI), [GOP](https://wiki.osdev.org/GOP), [PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard), [Sound Blaster 16](https://wiki.osdev.org/Sound_Blaster_16), [xHCI](https://wiki.osdev.org/EXtensible_Host_Controller_Interface), [AHCI](https://wiki.osdev.org/AHCI)
- `../flappy-512`: Flappy Bird in a 512-byte BIOS boot sector (mode 13h, PIT timing); style reference for tiny bare-metal games.
