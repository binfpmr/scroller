# References

Local copies of manuals go in `refs/` (git-ignored). intel.com and uefi.org refuse scripted downloads; use a browser for those.

## In `refs/`

| What | File | Source |
|---|---|---|
| UEFI Specification 2.10 / 2.11 | `UEFI_Spec_2_10_Aug29.pdf`, `UEFI_Spec_2_10_A_Aug8.pdf`, `UEFI_Spec_Final_2.11.pdf` | uefi.org |
| Intel 64 and IA-32 SDM, Volume 3 (system programming: GDT/IDT, APIC, PAT, TSC) | `325384-093-sdm-vol-3abcd.pdf` | intel.com |
| USB 2.0 specification (chapter 9: device framework, descriptors, standard requests) | `usb20.pdf` (errata and ECNs in `usb_20.zip`) | usb.org, `usb_20_20250603.zip` |
| USB HID 1.11 (boot protocol: appendix B) | `hid1_11.pdf` | usb.org |
| HID Usage Tables 1.5 (keyboard page 0x07, consumer page, gamepads) | `hut1_5.pdf` | usb.org |
| USB Mass Storage Bulk-Only Transport 1.0 | `usbmassbulk_10.pdf` | usb.org |
| SCSI Command Reference (READ(10), READ CAPACITY, INQUIRY, TEST UNIT READY) | `scsi-commands-seagate.pdf` | seagate.com |
| xHCI specification 1.2c (and 2.0) | `xHCI__Rev1.2c.pdf`, `xHCI__Rev2.0.pdf` | intel.com (browser download) |
| Intel High Definition Audio Specification 1.0a | `high-definition-audio-specification.pdf` | intel.com (browser download) |
| ACPI Specification 6.6 | `ACPI_Spec_6.6.pdf` | uefi.org (browser download) |
| NVM Express Base 2.1, NVM Command Set 1.1, PCIe Transport 1.1 | `nvme-base-2.1.pdf`, `nvme-nvm-command-set-1.1.pdf`, `nvme-pcie-transport-1.1.pdf` | nvmexpress.org |

Not public and not needed so far: Realtek RTL8168 and ALC295 datasheets (Linux `r8169` and `snd-hda-codec-realtek`
are the practical references), Intel PCH datasheets (the xHCI specification covers the USB controller), PCI Express
base specification (PCI-SIG members only; the OSDev wiki covers configuration space and capabilities).

## Online

- Linux sources as working references: `drivers/usb/host/xhci*.c`, `drivers/hid/usbhid/usbkbd.c` (boot keyboard),
  `drivers/net/ethernet/realtek/r8169_main.c`, `sound/pci/hda/`.
- OSDev wiki: [UEFI](https://wiki.osdev.org/UEFI), [GOP](https://wiki.osdev.org/GOP), [PS/2 Keyboard](https://wiki.osdev.org/PS/2_Keyboard), [Sound Blaster 16](https://wiki.osdev.org/Sound_Blaster_16), [xHCI](https://wiki.osdev.org/EXtensible_Host_Controller_Interface), [AHCI](https://wiki.osdev.org/AHCI)
- `../flappy-512`: Flappy Bird in a 512-byte BIOS boot sector (mode 13h, PIT timing); style reference for tiny bare-metal games.
