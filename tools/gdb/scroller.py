"""gdb support for debugging scroller.

`scroller-attach [build dir]`: after connecting to QEMU's gdb stub, it loads the symbols of scroller.elf at the image
base that efi_main wrote to debugcon.log (the ELF is linked at 0, so the offset is the base) and releases the wait
loop in efi_main. The game is started with `tools/qemu.py debug` (or `scripts/run-qemu.sh --debug`).

`scroller-net-attach [target]`: after connecting to the game's own stub through `tools/devlink.py gdb` (real hardware
or QEMU, over the debug link), asks the game for its image base and loads the symbols there. The target defaults to
$SCROLLER_TARGET, else the target the running bridge serves (build/gdb-bridge-target), else 127.0.0.1.
"""

import os
import re
import sys
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

# The first packet of a session waits until the game has stopped; over Wi-Fi that can take longer than gdb's default
# 2 s, and a timed-out first packet leaves gdb guessing the architecture (i386) for the whole session.
gdb.execute("set remotetimeout 10")


class ScrollerNetAttach(gdb.Command):
    """Load scroller symbols at the image base of the game on the debug link: scroller-net-attach [target]"""

    def __init__(self):
        super().__init__("scroller-net-attach", gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        sys.path.insert(0, str(ROOT / "tools"))
        import devlink  # noqa: E402 (tools/devlink.py)

        bridge_file = ROOT / "build" / "gdb-bridge-target"
        target = arg.strip() or os.environ.get("SCROLLER_TARGET", "")
        if not target and bridge_file.exists():
            target = bridge_file.read_text().strip()
        target = target or "127.0.0.1"
        try:
            info = devlink.Link(target).info()
        except devlink.DevlinkError as e:
            raise gdb.GdbError(f"scroller-net-attach: {e}")
        base = int(info["image_base"], 16)
        build = ROOT / "build" / "uefi"
        version_header = (build / "generated" / "scroller_version.h").read_text(errors="replace")
        if f'"{info.get("version")}"' not in version_header:
            print(f"scroller: WARNING: the game runs {info.get('version')}, {build / 'scroller.elf'} is another build")
        gdb.execute("symbol-file", to_string=True)
        gdb.execute(f"add-symbol-file {build / 'scroller.elf'} -o {base:#x}", to_string=True)
        print(f"scroller: symbols loaded at {base:#x} (version {info.get('version')})")


ScrollerNetAttach()
