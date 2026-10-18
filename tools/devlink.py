#!/usr/bin/env python3
"""Talks to the running game over the network debug link (src/platform/devlink.h has the protocol).

    tools/devlink.py [--target HOST] info                 version, addresses, screen
    tools/devlink.py [--target HOST] log [--all]           stream the log (--all: from boot); Ctrl+C stops
    tools/devlink.py [--target HOST] shot out.png          screenshot of the 320x240 game screen
    tools/devlink.py [--target HOST] keys "right:2 jump"   key script (see below)
    tools/devlink.py [--target HOST] peek ADDR [LEN]       hex dump of memory
    tools/devlink.py [--target HOST] poke ADDR HEXBYTES    write memory
    tools/devlink.py [--target HOST] reboot [--method M]  reboot: warm (UEFI, default), cold (UEFI), soft (chipset
                                                          0xCF9 CPU reset), kbc (keyboard controller); a
                                                          network-booted PC loads the current build
    tools/devlink.py [--target HOST] reload [FILE]        start a new build without rebooting (~1 s; default
                                                          build/uefi/scroller.efi); reboots if it is incompatible
    tools/devlink.py [--target HOST] gdb [--port 2345]    gdb server on localhost for gdb / VS Code (see below)
    tools/devlink.py discover                              broadcast INFO; lists every game on the LAN

The target defaults to $SCROLLER_TARGET, else 127.0.0.1 (QEMU forwards the port, see tools/qemu.py). On real hardware
use the address the F1 overlay shows on its NET line, or find it with `discover`.

gdb bridge: listens on localhost:PORT (TCP, the usual gdb remote protocol) and passes the packets to the game's stub
over the debug link, which retries lost datagrams. Connecting stops the game; in gdb: `target remote :2345`, then
`scroller-net-attach` (tools/gdb/scroller.py) loads the symbols at the running image's base. VS Code: launch
configuration "Debug on test PC (network)".

Key scripts: words separated by spaces. KEY taps a key, KEY:SECS holds it, +KEY / -KEY press / release, wait:SECS
pauses, shot:FILE.png takes a screenshot. Keys: left right down jump (= up) esc f1 volup voldown mute music ctrl alt del
(reboot in the game: "+ctrl +alt del -alt -ctrl", or the reboot command).
"""

import argparse
import os
import random
import select
import socket
import struct
import sys
import time
import zlib
from pathlib import Path

PORT = 5555
MAX_READ = 1024

CMD_INFO, CMD_READ, CMD_WRITE, CMD_KEY, CMD_REBOOT, CMD_SUBSCRIBE, CMD_LOG = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x80
CMD_GDB, CMD_GDB_WAIT, CMD_GDB_STOP = 0x07, 0x08, 0x09
CMD_RELOAD_BEGIN, CMD_RELOAD_DATA, CMD_RELOAD_RUN = 0x0A, 0x0B, 0x0C
STATUS_RELOAD_REFUSED = 5
RESET_METHODS = {"warm": 0, "cold": 1, "soft": 2, "kbc": 3}  # platform::ResetMethod
STATUS_RUNNING = 4
STATUS_TEXT = {1: "address not mapped", 2: "malformed request", 3: "unknown command", STATUS_RUNNING: "running",
               STATUS_RELOAD_REFUSED: "reload refused"}

# platform::Key values (src/platform/input.h).
KEYS = {"left": 0, "right": 1, "down": 2, "jump": 3, "up": 3, "esc": 4, "f1": 5, "volup": 6, "voldown": 7,
        "mute": 8, "ctrl": 9, "alt": 10, "del": 11, "music": 12}


class DevlinkError(Exception):
    pass


class DevlinkStatus(DevlinkError):
    def __init__(self, status: int):
        super().__init__(STATUS_TEXT.get(status, f"status {status}"))
        self.status = status


class Link:
    def __init__(self, host: str, timeout: float = 0.3, retries: int = 8):
        self.address = (socket.gethostbyname(host), PORT)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(("", 0))
        self.timeout = timeout
        self.retries = retries
        self.sequence = random.randrange(1 << 32)
        self.log_handler = None  # called with (position, text) for LOG packets that arrive meanwhile

    def _next_sequence(self) -> int:
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF
        return self.sequence

    @staticmethod
    def _packet(command: int, sequence: int, body: bytes = b"") -> bytes:
        return b"DL" + struct.pack("<BBI", command, 0, sequence) + body

    def _dispatch(self, data: bytes):
        """Returns (command, status, sequence, body) of a reply, or None for LOG packets and garbage."""
        if len(data) < 8 or data[:2] != b"DL":
            return None
        command, status, sequence = struct.unpack_from("<BBI", data, 2)
        body = data[8:]
        if command == CMD_LOG:
            if self.log_handler and len(body) >= 8:
                self.log_handler(struct.unpack_from("<Q", body)[0], body[8:])
            return None
        return command, status, sequence, body

    def request(self, command: int, body: bytes = b"", same_sequence: bool = False) -> bytes:
        """same_sequence: retries reuse the sequence number, so the target can recognise a repeat (GDB packets)."""
        return self.pipelined([(command, body)], same_sequence=same_sequence)[0]

    def request_status(self, command: int, body: bytes = b"") -> tuple[int, bytes]:
        """Like request, but returns (status, body) instead of raising for a non-zero status."""
        try:
            return 0, self.request(command, body)
        except DevlinkStatus as e:
            return e.status, b""

    def pipelined(self, requests: list[tuple[int, bytes]], window: int = 16,
                  same_sequence: bool = False) -> list[bytes]:
        """Sends requests with up to `window` outstanding, retrying lost ones; returns the reply bodies in order."""
        results: list[bytes | None] = [None] * len(requests)
        outstanding: dict[int, tuple[int, float, int]] = {}  # sequence -> (index, sent time, attempts)
        next_index = 0
        done = 0
        while done < len(requests):
            while next_index < len(requests) and len(outstanding) < window:
                sequence = self._next_sequence()
                command, body = requests[next_index]
                self.sock.sendto(self._packet(command, sequence, body), self.address)
                outstanding[sequence] = (next_index, time.monotonic(), 1)
                next_index += 1
            ready, _, _ = select.select([self.sock], [], [], 0.05)
            if ready:
                reply = self._dispatch(self.sock.recv(2048))
                if reply and reply[2] in outstanding:
                    index, _, _ = outstanding.pop(reply[2])
                    if reply[1] != 0:
                        raise DevlinkStatus(reply[1])
                    results[index] = reply[3]
                    done += 1
            now = time.monotonic()
            for sequence, (index, sent, attempts) in list(outstanding.items()):
                if now - sent < self.timeout:
                    continue
                if attempts >= self.retries:
                    raise DevlinkError(f"no answer from {self.address[0]}:{PORT}")
                del outstanding[sequence]
                new_sequence = sequence if same_sequence else self._next_sequence()
                command, body = requests[index]
                self.sock.sendto(self._packet(command, new_sequence, body), self.address)
                outstanding[new_sequence] = (index, now, attempts + 1)
        return results  # type: ignore[return-value]

    def info(self) -> dict[str, str]:
        text = self.request(CMD_INFO).decode(errors="replace")
        return dict(line.split("=", 1) for line in text.splitlines() if "=" in line)

    def read(self, address: int, length: int) -> bytes:
        chunks = [(CMD_READ, struct.pack("<QI", a, min(MAX_READ, address + length - a)))
                  for a in range(address, address + length, MAX_READ)]
        return b"".join(self.pipelined(chunks))

    def write(self, address: int, data: bytes) -> None:
        step = MAX_READ
        self.pipelined([(CMD_WRITE, struct.pack("<Q", address + i) + data[i:i + step])
                        for i in range(0, len(data), step)])

    def key(self, name: str, down: bool) -> None:
        if name not in KEYS:
            raise DevlinkError(f"unknown key '{name}' (keys: {' '.join(KEYS)})")
        self.request(CMD_KEY, bytes([KEYS[name], 1 if down else 0]))

    def screenshot(self) -> tuple[int, int, bytes]:
        info = self.info()
        width, height = int(info["width"]), int(info["height"])
        pixels = self.read(int(info["screen"], 16), width * height * 4)
        return width, height, pixels


def write_png(path: str, width: int, height: int, xrgb: bytes) -> None:
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # filter: none
        row = xrgb[y * width * 4:(y + 1) * width * 4]
        rgb = bytearray(width * 3)
        rgb[0::3] = row[2::4]  # little-endian 0x00RRGGBB: bytes B, G, R, X
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows += rgb

    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(rows), 9)))
        f.write(chunk(b"IEND", b""))


def take_shot(link: Link, path: str) -> None:
    start = time.monotonic()
    width, height, pixels = link.screenshot()
    write_png(path, width, height, pixels)
    print(f"{path}: {width}x{height} in {time.monotonic() - start:.2f} s")


def run_keys(link: Link, script: str) -> None:
    for word in script.split():
        if word.startswith("wait:"):
            time.sleep(float(word[5:]))
        elif word.startswith("shot:"):
            take_shot(link, word[5:])
        elif word[0] in "+-":
            link.key(word[1:], word[0] == "+")
        else:
            name, _, hold = word.partition(":")
            link.key(name, True)
            time.sleep(float(hold) if hold else 0.1)
            link.key(name, False)


def stream_log(link: Link, from_start: bool) -> None:
    expected = [None]

    def show(position: int, text: bytes) -> None:
        if expected[0] is not None and position != expected[0]:
            if position > expected[0]:
                sys.stdout.write(f"\n[devlink: {position - expected[0]} bytes of log lost]\n")
            else:
                return  # duplicate
        sys.stdout.write(text.decode(errors="replace"))
        sys.stdout.flush()
        expected[0] = position + len(text)

    link.log_handler = show
    link.request(CMD_SUBSCRIBE, bytes([1 if from_start else 0]))
    boot_id = link.info().get("boot_id")
    last_ping = time.monotonic()
    while True:
        ready, _, _ = select.select([link.sock], [], [], 0.5)
        if ready:
            link._dispatch(link.sock.recv(2048))
        if time.monotonic() - last_ping < 2:
            continue
        # A new boot id means the target rebooted (and forgot us): subscribe again, from its boot.
        try:
            new_id = link.info().get("boot_id")
        except DevlinkError:
            sys.stdout.write("\n[devlink: target not answering; waiting]\n")
            sys.stdout.flush()
            new_id = None
            while new_id is None:
                try:
                    new_id = link.info().get("boot_id")
                except DevlinkError:
                    pass
        if new_id != boot_id:
            sys.stdout.write("\n[devlink: target rebooted]\n")
            link.request(CMD_SUBSCRIBE, b"\x01")
            expected[0] = None
            boot_id = new_id
        last_ping = time.monotonic()


def wait_for_new_boot(link: Link, old_boot_id: str | None, timeout: float) -> dict[str, str] | None:
    """Polls INFO until the boot id changes; returns the new info, or None after `timeout` seconds."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            info = link.info()
            if info.get("boot_id") != old_boot_id:
                return info
        except DevlinkError:
            pass
        time.sleep(0.2)
    return None


def reload(link: Link, path: str) -> int:
    data = Path(path).read_bytes()
    start = time.monotonic()
    old = link.info()
    link.request(CMD_RELOAD_BEGIN, struct.pack("<I", len(data)))
    link.pipelined([(CMD_RELOAD_DATA, struct.pack("<I", offset) + data[offset:offset + MAX_READ])
                    for offset in range(0, len(data), MAX_READ)])
    status, reply = 0, b""
    try:
        link.request(CMD_RELOAD_RUN, struct.pack("<II", len(data), zlib.crc32(data)))
    except DevlinkStatus as e:
        status = e.status
    if status != 0:
        # The reason is in the game's log (devlink: reload refused: ...).
        print(f"reload refused by {old.get('version')} ({STATUS_TEXT.get(status, status)}); rebooting instead")
        link.request(CMD_REBOOT, b"\0")
        info = wait_for_new_boot(link, old.get("boot_id"), 120)
    else:
        info = wait_for_new_boot(link, old.get("boot_id"), 15)
    if info is None:
        print("devlink: the game did not come back", file=sys.stderr)
        return 1
    print(f"{old.get('version')} -> {info.get('version')} in {time.monotonic() - start:.1f} s"
          f"{'' if status == 0 else ' (rebooted)'}")
    return 0


def gdb_frame(payload: bytes) -> bytes:
    return b"$" + payload + b"#" + f"{sum(payload) & 0xFF:02x}".encode()


class GdbConnection:
    """One gdb client: parses its packets ('$...#cs', '+'/'-' acks, 0x03 interrupt) from the TCP stream."""

    def __init__(self, conn: socket.socket):
        self.conn = conn
        self.buffer = b""
        self.interrupted = False
        self.pending: list[bytes] = []  # received and acknowledged, not yet passed on: never dropped

    def read_packets(self, timeout: float) -> bool:
        """Adds packets that arrive within `timeout` to self.pending (acknowledged). False when gdb disconnected."""
        ready, _, _ = select.select([self.conn], [], [], timeout)
        if ready:
            data = self.conn.recv(4096)
            if not data:
                return False
            self.buffer += data
        packets = self.pending
        while self.buffer:
            if self.buffer[0:1] in (b"+", b"-"):
                self.buffer = self.buffer[1:]
            elif self.buffer[0] == 0x03:
                self.buffer = self.buffer[1:]
                self.interrupted = True
            elif self.buffer[0:1] == b"$":
                end = self.buffer.find(b"#")
                if end < 0 or len(self.buffer) < end + 3:
                    break  # incomplete
                packets.append(self.buffer[1:end])
                self.buffer = self.buffer[end + 3:]
                self.conn.sendall(b"+")
            else:
                self.buffer = self.buffer[1:]  # noise
        return True

    def send(self, payload: bytes) -> None:
        self.conn.sendall(gdb_frame(payload))


def wait_for_stop(link: Link, gdb: GdbConnection) -> bytes | None:
    """Polls the target until it stops; forwards gdb's Ctrl+C. Returns the stop reply, None if gdb went away."""
    while True:
        status, reply = link.request_status(CMD_GDB_WAIT)
        if status == 0:
            return reply
        if not gdb.read_packets(0.03):  # packets arriving meanwhile wait in gdb.pending
            return None
        if gdb.interrupted:
            gdb.interrupted = False
            link.request(CMD_GDB_STOP)


def gdb_bridge(link: Link, port: int) -> None:
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", port))
    server.listen(1)
    info = link.info()
    # For scroller-net-attach (tools/gdb/scroller.py) when started from VS Code without SCROLLER_TARGET.
    try:
        (Path(__file__).resolve().parent.parent / "build" / "gdb-bridge-target").write_text(link.address[0] + "\n")
    except OSError:
        pass  # no build directory: scroller-net-attach then needs the address
    print(f"gdb bridge: target {link.address[0]} (version {info.get('version')}, image base {info.get('image_base')})")
    print(f"gdb bridge: listening on localhost:{port}", flush=True)
    while True:
        conn, _ = server.accept()
        conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        gdb = GdbConnection(conn)
        print("gdb bridge: gdb connected, stopping the game", flush=True)
        try:
            link.request(CMD_GDB_STOP)
            stopped = wait_for_stop(link, gdb) is not None
        except DevlinkError as e:
            print(f"gdb bridge: {e}", flush=True)
            stopped = False
        if not stopped:
            conn.close()
            continue
        detached = False
        try:
            detached = serve_gdb(link, gdb)
        except (ConnectionError, OSError, DevlinkError) as e:
            print(f"gdb bridge: connection lost ({e})", flush=True)
        conn.close()
        if not detached:
            # gdb went away without detaching: remove its breakpoints and let the game run, as a detach would.
            try:
                link.request(CMD_GDB_STOP)
                while link.request_status(CMD_GDB_WAIT)[0] != 0:
                    time.sleep(0.03)
                link.request(CMD_GDB, b"D", same_sequence=True)
                print("gdb bridge: detached the game for gdb", flush=True)
            except DevlinkError as e:
                print(f"gdb bridge: could not detach ({e})", flush=True)
        print("gdb bridge: gdb disconnected", flush=True)


def serve_gdb(link: Link, gdb: GdbConnection) -> bool:
    """Relays until gdb leaves. True if the game was detached (or killed) properly."""
    while True:
        if not gdb.pending and not gdb.read_packets(0.5):
            return False
        while gdb.pending:
            packet = gdb.pending.pop(0)
            reply = link.request(CMD_GDB, packet, same_sequence=True)
            if packet == b"k":
                return True  # the target reboots; gdb expects no reply
            if packet[:1] in (b"c", b"s"):
                stop = wait_for_stop(link, gdb)
                if stop is None:
                    return False
                gdb.send(stop)
                continue
            gdb.send(reply)
            if packet[:1] == b"D":
                return True


def discover() -> None:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    sock.bind(("", 0))
    for _ in range(3):
        sock.sendto(b"DL" + struct.pack("<BBI", CMD_INFO, 0, 1), ("255.255.255.255", PORT))
        time.sleep(0.1)
    seen = set()
    deadline = time.monotonic() + 1.5
    while time.monotonic() < deadline:
        ready, _, _ = select.select([sock], [], [], 0.1)
        if not ready:
            continue
        data, (host, _) = sock.recvfrom(2048)
        if data[:3] == b"DL" + bytes([CMD_INFO]) and host not in seen:
            seen.add(host)
            fields = dict(line.split("=", 1) for line in data[8:].decode(errors="replace").splitlines() if "=" in line)
            print(f"{host}  version {fields.get('version', '?')}  mac {fields.get('mac', '?')}")
    if not seen:
        print("no game answered (is it running, and on this network?)")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", default=os.environ.get("SCROLLER_TARGET", "127.0.0.1"))
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("info")
    log = sub.add_parser("log")
    log.add_argument("--all", action="store_true", help="start with everything logged since boot")
    shot = sub.add_parser("shot")
    shot.add_argument("file")
    keys = sub.add_parser("keys")
    keys.add_argument("script")
    peek = sub.add_parser("peek")
    peek.add_argument("address", type=lambda s: int(s, 0))
    peek.add_argument("length", type=lambda s: int(s, 0), nargs="?", default=64)
    poke = sub.add_parser("poke")
    poke.add_argument("address", type=lambda s: int(s, 0))
    poke.add_argument("hex")
    reboot = sub.add_parser("reboot")
    reboot.add_argument("--method", choices=list(RESET_METHODS), default="warm")
    reboot.add_argument("--cold", action="store_const", const="cold", dest="method", help="same as --method cold")
    reload_cmd = sub.add_parser("reload")
    reload_cmd.add_argument("file", nargs="?",
                            default=str(Path(__file__).resolve().parent.parent / "build" / "uefi" / "scroller.efi"))
    gdb = sub.add_parser("gdb")
    gdb.add_argument("--port", type=int, default=2345)
    sub.add_parser("discover")
    args = parser.parse_args()

    try:
        if args.command == "discover":
            discover()
            return 0
        link = Link(args.target)
        if args.command == "info":
            for key, value in link.info().items():
                print(f"{key:12} {value}")
        elif args.command == "log":
            stream_log(link, args.all)
        elif args.command == "shot":
            take_shot(link, args.file)
        elif args.command == "keys":
            run_keys(link, args.script)
        elif args.command == "peek":
            data = link.read(args.address, args.length)
            for offset in range(0, len(data), 16):
                row = data[offset:offset + 16]
                text = "".join(chr(b) if 32 <= b < 127 else "." for b in row)
                print(f"{args.address + offset:016x}  {row.hex(' '):47}  {text}")
        elif args.command == "poke":
            link.write(args.address, bytes.fromhex(args.hex))
        elif args.command == "reload":
            return reload(link, args.file)
        elif args.command == "gdb":
            gdb_bridge(link, args.port)
        elif args.command == "reboot":
            link.request(CMD_REBOOT, bytes([RESET_METHODS[args.method]]))
            print(f"rebooting ({args.method})")
    except DevlinkError as e:
        print(f"devlink: {e}", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, BrokenPipeError):
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
