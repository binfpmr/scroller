#!/usr/bin/env bash
# Headless boot test: waits for "boot ok" in the debug log, saves a screenshot. Extra args go to `tools/qemu.py smoke`.
set -euo pipefail
exec python3 "$(dirname "$0")/../tools/qemu.py" smoke "$@"
