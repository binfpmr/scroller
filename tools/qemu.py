#!/usr/bin/env python3
"""Run scroller in QEMU with OVMF.

  qemu.py run      interactive window, serial on stdio
  qemu.py debug    like run, plus gdb stub on :1234; the game waits in efi_main until gdb releases it
  qemu.py smoke    headless: boot, wait for a line in the debug log, take a screenshot, quit (exit 1 on failure)

The debug log (QEMU debugcon, port 0xE9) always goes to <build dir>/debugcon.log.
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OVMF_CODE = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
OVMF_VARS = Path("/usr/share/OVMF/OVMF_VARS_4M.fd")
DEBUGCON_READBACK_DEBUG = 0xDB  # must match kDebugconReadbackDebug in src/boot/efi_main.cpp


def parse_cpu_list(text: str) -> set[int]:
    cpus = set()
    for part in text.strip().split(","):
        lo, _, hi = part.partition("-")
        cpus.update(range(int(lo), int(hi or lo) + 1))
    return cpus


def pin_to_one_core_type() -> None:
    """On Intel hybrid CPUs, keep QEMU on the E-cores.

    With KVM, OVMF intermittently hangs at the logo when the vCPU thread migrates between P- and E-cores
    (seen on an i5-1334U, kernel 7.0, QEMU 10.2); pinned to one core type it boots reliably.
    """
    atom = Path("/sys/devices/cpu_atom/cpus")
    if atom.exists():
        os.sched_setaffinity(0, parse_cpu_list(atom.read_text()))


def qemu_command(build: Path, *, debug: bool, headless: bool, qmp: Path | None) -> list[str]:
    vars_copy = build / "OVMF_VARS.fd"
    shutil.copyfile(OVMF_VARS, vars_copy)  # fresh NVRAM every run: no stale boot entries

    kvm = os.access("/dev/kvm", os.R_OK | os.W_OK)
    readback = DEBUGCON_READBACK_DEBUG if debug else 0xE9
    cmd = [
        "qemu-system-x86_64",
        "-name", "scroller",
        "-machine", "q35",
        "-m", "256M",
        "-accel", "kvm" if kvm else "tcg",
        "-cpu", "host" if kvm else "max",
        "-drive", f"if=pflash,format=raw,readonly=on,file={OVMF_CODE}",
        "-drive", f"if=pflash,format=raw,file={vars_copy}",
        "-drive", f"format=raw,file={build / 'scroller.img'}",
        "-chardev", f"file,id=debugcon,path={build / 'debugcon.log'}",
        "-device", f"isa-debugcon,iobase=0xe9,chardev=debugcon,readback={readback}",
        "-nic", "none",
    ]
    if headless:
        cmd += ["-display", "none", "-serial", f"file:{build / 'serial.log'}"]
    else:
        cmd += ["-serial", "stdio"]
    if debug:
        cmd += ["-s"]
    if qmp:
        cmd += ["-qmp", f"unix:{qmp},server=on,wait=off"]
    return cmd


def wait_for_log(log: Path, text: str, timeout: float, proc: subprocess.Popen) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if log.exists() and text in log.read_text(errors="replace"):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.1)
    return False


class Qmp:
    def __init__(self, path: Path, timeout: float = 10.0):
        deadline = time.monotonic() + timeout
        while True:
            try:
                self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                self.sock.connect(str(path))
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.1)
        self.file = self.sock.makefile("rw")
        self._read()  # greeting
        self.execute("qmp_capabilities")

    def _read(self) -> dict:
        while True:
            msg = json.loads(self.file.readline())
            if "event" not in msg:
                return msg

    def execute(self, command: str, **arguments) -> dict:
        self.file.write(json.dumps({"execute": command, "arguments": arguments}) + "\n")
        self.file.flush()
        reply = self._read()
        if "error" in reply:
            raise RuntimeError(f"QMP {command}: {reply['error']}")
        return reply.get("return", {})


def cmd_run(args) -> int:
    os.execvp("qemu-system-x86_64", qemu_command(args.build, debug=False, headless=False, qmp=None))


def cmd_debug(args) -> int:
    log = args.build / "debugcon.log"
    log.unlink(missing_ok=True)
    print("qemu: starting", flush=True)
    proc = subprocess.Popen(qemu_command(args.build, debug=True, headless=False, qmp=None))
    if wait_for_log(log, "waiting for debugger", args.timeout, proc):
        print("qemu: waiting for gdb on :1234", flush=True)
    else:
        print("qemu: game did not reach the debugger wait loop", flush=True)
    return proc.wait()


def cmd_smoke(args) -> int:
    log = args.build / "debugcon.log"
    qmp_path = args.build / "qmp.sock"
    screenshot = args.screenshot or args.build / "smoke.png"
    log.unlink(missing_ok=True)
    qmp_path.unlink(missing_ok=True)

    proc = subprocess.Popen(qemu_command(args.build, debug=False, headless=True, qmp=qmp_path))
    try:
        ok = wait_for_log(log, args.expect, args.timeout, proc)
        if proc.poll() is None:
            if ok:
                time.sleep(args.settle)
            qmp = Qmp(qmp_path)
            qmp.execute("screendump", filename=str(screenshot), format="png")  # also on failure, to see why
            qmp.execute("quit")
    finally:
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()

    serial = args.build / "serial.log"
    print(f"--- {serial} ---")
    print(serial.read_text(errors="replace") if serial.exists() else "(no log)")
    print(f"--- {log} ---")
    print(log.read_text(errors="replace") if log.exists() else "(no log)")
    if not ok:
        print(f"smoke test FAILED: '{args.expect}' not seen within {args.timeout}s; screenshot: {screenshot}",
              file=sys.stderr)
        return 1
    print(f"smoke test passed, screenshot: {screenshot}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", type=Path, default=ROOT / "build" / "uefi", help="build directory with scroller.img")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("run").set_defaults(func=cmd_run)
    debug = sub.add_parser("debug")
    debug.add_argument("--timeout", type=float, default=60)
    debug.set_defaults(func=cmd_debug)
    smoke = sub.add_parser("smoke")
    smoke.add_argument("--expect", default="boot ok", help="text that must appear in the debug log")
    smoke.add_argument("--timeout", type=float, default=30)
    smoke.add_argument("--settle", type=float, default=0.5, help="seconds to wait before the screenshot")
    smoke.add_argument("--screenshot", type=Path)
    smoke.set_defaults(func=cmd_smoke)
    args = parser.parse_args()
    args.build = args.build.resolve()
    pin_to_one_core_type()
    if not (args.build / "scroller.img").exists():
        print(f"{args.build / 'scroller.img'} not found; build first (cmake --build --preset uefi)", file=sys.stderr)
        return 1
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
