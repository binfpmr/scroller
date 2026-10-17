"""gdb support for debugging scroller in QEMU.

Defines the command `scroller-attach [build dir]`: after connecting to QEMU's gdb stub, it loads the symbols of
scroller.elf at the image base that efi_main wrote to debugcon.log (the ELF is linked at 0, so the offset is the base)
and releases the wait loop in efi_main. The game is started with `tools/qemu.py debug` (or `scripts/run-qemu.sh --debug`).
"""

import re
from pathlib import Path

import gdb

ROOT = Path(__file__).resolve().parent.parent.parent


class ScrollerAttach(gdb.Command):
    """Load scroller symbols at the logged image base and let efi_main continue: scroller-attach [build dir]"""

    def __init__(self):
        super().__init__("scroller-attach", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        build = Path(arg.strip()) if arg.strip() else ROOT / "build" / "uefi"
        log = (build / "debugcon.log").read_text(errors="replace")
        bases = re.findall(r"image base (0x[0-9a-fA-F]+)", log)
        if not bases or "waiting for debugger" not in log:
            raise gdb.GdbError(f"{build / 'debugcon.log'}: game is not waiting for a debugger (run tools/qemu.py debug)")
        base = int(bases[-1], 16)
        gdb.execute("symbol-file", to_string=True)  # drop symbols loaded at the link address (0)
        gdb.execute(f"add-symbol-file {build / 'scroller.elf'} -o {base:#x}", to_string=True)
        gdb.execute("set var scroller_debugger_wait = 0")
        print(f"scroller: symbols loaded at {base:#x}, efi_main released")


ScrollerAttach()
