#!/usr/bin/env python3
"""Embed a text file into a C++ header as a string constant.

  embed_text.py <input> <output.h> <symbol>

Produces `inline constexpr char <symbol>[] = "...";` (NUL-terminated; sizeof - 1 is the text length).
"""

import sys
from pathlib import Path


ESCAPES = {"\n": "\\n", "\t": "\\t", '"': '\\"', "\\": "\\\\"}


def main() -> int:
    src, dst, symbol = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    data = src.read_bytes()
    lines = [f"// Generated from {src.name} by tools/embed_text.py. Do not edit.", "#pragma once", "",
             f"inline constexpr char {symbol}[] ="]
    for line in data.decode("latin-1").splitlines(keepends=True) or [""]:
        part = "".join(ESCAPES.get(c, c) if 32 <= ord(c) < 127 or c in ESCAPES else f"\\x{ord(c):02x}\"\"" for c in line)
        lines.append(f'    "{part}"')
    lines[-1] += ";"
    dst.parent.mkdir(parents=True, exist_ok=True)
    text = "\n".join(lines) + "\n"
    if not dst.exists() or dst.read_text() != text:  # keep the timestamp when unchanged
        dst.write_text(text)
    return 0


if __name__ == "__main__":
    sys.exit(main())
