#!/usr/bin/env bash
# Builds the bootable images from scroller.efi, without root:
#   esp.img      FAT32 EFI system partition with /EFI/BOOT/BOOTX64.EFI (can be dd'ed into an existing partition)
#   scroller.img GPT disk with esp.img as its only partition (can be dd'ed to a whole USB stick or disk)
# Usage: build-image.sh <scroller.efi> <output dir>
set -euo pipefail
efi=$1
out=$2

esp_mib=48             # FAT32 needs at least ~33 MiB with 512-byte sectors
esp_start_sector=2048  # 1 MiB alignment
disk_mib=$((esp_mib + 2))

esp=$out/esp.img
disk=$out/scroller.img
tmp_esp=$esp.tmp
tmp_disk=$disk.tmp

rm -f "$tmp_esp" "$tmp_disk"
truncate -s ${esp_mib}M "$tmp_esp"
mkfs.fat -F 32 -n SCROLLER "$tmp_esp" >/dev/null
mmd -i "$tmp_esp" ::/EFI ::/EFI/BOOT
mcopy -i "$tmp_esp" "$efi" ::/EFI/BOOT/BOOTX64.EFI

truncate -s ${disk_mib}M "$tmp_disk"
sgdisk --clear \
  --new=1:${esp_start_sector}:+${esp_mib}M --typecode=1:ef00 --change-name=1:"EFI System" \
  "$tmp_disk" >/dev/null
dd if="$tmp_esp" of="$tmp_disk" bs=512 seek=$esp_start_sector conv=notrunc status=none

mv "$tmp_esp" "$esp"
mv "$tmp_disk" "$disk"
