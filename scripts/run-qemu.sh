#!/usr/bin/env bash
# Interactive QEMU run. --debug: gdb stub on :1234, game waits for the debugger. Extra args go to tools/qemu.py.
set -euo pipefail
here=$(dirname "$0")
if [[ ${1:-} == --debug ]]; then
  shift
  exec python3 "$here/../tools/qemu.py" debug "$@"
fi
exec python3 "$here/../tools/qemu.py" run "$@"
