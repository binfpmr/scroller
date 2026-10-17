#!/usr/bin/env bash
# Builds the bootable images from scroller.efi, without root:
#   esp.img      FAT32 EFI system partition with /EFI/BOOT/BOOTX64.EFI and /scroller/assets.pak (can be dd'ed into an
#                existing partition)
#   scroller.img GPT disk with esp.img as its only partition (can be dd'ed to a whole USB stick or disk)
# Both files are sparse: they take ~1 MiB on the build machine although their nominal size is ~33 MiB.
# Usage: build-image.sh <scroller.efi> <output dir> <assets.pak>
set -euo pipefail
efi=$1
out=$2
pak=$3

# Smallest valid FAT32 volume with 512-byte sectors and 1 sector per cluster: FAT32 needs at least 65525 clusters
# (fewer clusters make it FAT16 by definition, whatever the boot sector says). 66592 is the smallest size for which
# mkfs.fat lays out 65525+ clusters (it gets 65534). The UEFI spec requires FAT32 support for system partitions.
esp_sectors=66592
esp_start_sector=2048  # 1 MiB alignment, the usual first partition start on flash media
gpt_backup_sectors=33  # backup GPT at the end of the disk: 32 sectors of partition entries + header
disk_sectors=$((esp_start_sector + esp_sectors + gpt_backup_sectors))

esp=$out/esp.img
disk=$out/scroller.img
tmp_esp=$esp.tmp
tmp_disk=$disk.tmp

rm -f "$tmp_esp" "$tmp_disk"
truncate -s $((esp_sectors * 512)) "$tmp_esp"
mkfs_output=$(mkfs.fat -F 32 -S 512 -s 1 -n SCROLLER "$tmp_esp" 2>&1)
if grep -qi 'less than' <<<"$mkfs_output"; then
  echo "error: $esp_sectors sectors are too few for a valid FAT32 volume:" >&2
  echo "$mkfs_output" >&2
  exit 1
fi
mmd -i "$tmp_esp" ::/EFI ::/EFI/BOOT ::/scroller
mcopy -i "$tmp_esp" "$efi" ::/EFI/BOOT/BOOTX64.EFI
mcopy -i "$tmp_esp" "$pak" ::/scroller/assets.pak
fallocate --dig-holes "$tmp_esp"  # mkfs.fat writes its zeroed FATs explicitly

truncate -s $((disk_sectors * 512)) "$tmp_disk"
sgdisk --clear \
  --new=1:${esp_start_sector}:$((esp_start_sector + esp_sectors - 1)) --typecode=1:ef00 --change-name=1:"EFI System" \
  "$tmp_disk" >/dev/null
# conv=sparse skips zero blocks; the target is freshly truncated (all zeros), so skipping them is exact.
dd if="$tmp_esp" of="$tmp_disk" bs=512 seek=$esp_start_sector conv=notrunc,sparse status=none

mv "$tmp_esp" "$esp"
mv "$tmp_disk" "$disk"
