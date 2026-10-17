#!/usr/bin/env python3
"""Build the asset pack (see src/lib/pack.h): pack_assets.py <assets dir> <output.pak>

Every file under <assets dir> is stored under its relative path with '/' separators, e.g. "levels/test.txt".
Files whose name starts with '.' or ends with '~' are skipped. The output is rewritten only if it changes.
"""

import struct
import sys
from pathlib import Path

MAGIC = b"SCRPAK01"
NAME_SIZE = 56
ALIGN = 16


def main() -> int:
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    files = sorted(p for p in root.rglob("*") if p.is_file() and not p.name.startswith(".") and not p.name.endswith("~"))
    entries = []
    for path in files:
        name = path.relative_to(root).as_posix().encode()
        if len(name) >= NAME_SIZE:
            print(f"error: asset name too long (max {NAME_SIZE - 1}): {name.decode()}", file=sys.stderr)
            return 1
        entries.append((name, path.read_bytes()))

    offset = 16 + len(entries) * (NAME_SIZE + 8)
    directory = b""
    data = b""
    for name, blob in entries:
        pad = (-(offset + len(data))) % ALIGN
        data += b"\0" * pad
        directory += name.ljust(NAME_SIZE, b"\0") + struct.pack("<II", offset + len(data), len(blob))
        data += blob
    pack = MAGIC + struct.pack("<II", len(entries), 0) + directory + data

    if not out.exists() or out.read_bytes() != pack:
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(pack)
    return 0


if __name__ == "__main__":
    sys.exit(main())
