#!/usr/bin/env bash
# Fails if the linked ELF contains dynamic relocations other than R_X86_64_RELATIVE, which crt0.S cannot apply.
# Usage: check-relocs.sh <readelf> <scroller.elf>
set -euo pipefail
readelf=$1
elf=$2

bad=$("$readelf" -rW "$elf" | awk '$3 ~ /^R_X86_64_/ && $3 != "R_X86_64_RELATIVE"')
if [[ -n $bad ]]; then
  echo "error: $elf has relocations crt0.S cannot apply:" >&2
  echo "$bad" >&2
  exit 1
fi
