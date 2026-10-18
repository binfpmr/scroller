#!/usr/bin/env python3
"""Talks to the running game over the network debug link (src/platform/devlink.h has the protocol).

    tools/devlink.py [--target HOST] info                 version, addresses, screen
    tools/devlink.py [--target HOST] log [--all]           stream the log (--all: from boot); Ctrl+C stops
    tools/devlink.py [--target HOST] shot out.png          screenshot of the 320x240 game screen
    tools/devlink.py [--target HOST] keys "right:2 jump"   key script (see below)
    tools/devlink.py [--target HOST] peek ADDR [LEN]       hex dump of memory
    tools/devlink.py [--target HOST] poke ADDR HEXBYTES    write memory
    tools/devlink.py [--target HOST] reboot                reboot (a network-booted PC loads the current build)
    tools/devlink.py discover                              broadcast INFO; lists every game on the LAN

The target defaults to $SCROLLER_TARGET, else 127.0.0.1 (QEMU forwards the port, see tools/qemu.py). On real hardware
use the address the F1 overlay shows on its NET line, or find it with `discover`.

Key scripts: words separated by spaces. KEY taps a key, KEY:SECS holds it, +KEY / -KEY press / release, wait:SECS
pauses, shot:FILE.png takes a screenshot. Keys: left right down jump (= up) esc f1 volup voldown mute.
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

PORT = 5555
MAX_READ = 1024

CMD_INFO, CMD_READ, CMD_WRITE, CMD_KEY, CMD_REBOOT, CMD_SUBSCRIBE, CMD_LOG = 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x80
STATUS_TEXT = {1: "address not mapped", 2: "malformed request", 3: "unknown command"}

# platform::Key values (src/platform/input.h).
KEYS = {"left": 0, "right": 1, "down": 2, "jump": 3, "up": 3, "esc": 4, "f1": 5, "volup": 6, "voldown": 7,
        "mute": 8}


class DevlinkError(Exception):
    pass


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

    def request(self, command: int, body: bytes = b"") -> bytes:
        return self.pipelined([(command, body)])[0]

    def pipelined(self, requests: list[tuple[int, bytes]], window: int = 16) -> list[bytes]:
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
                        raise DevlinkError(STATUS_TEXT.get(reply[1], f"status {reply[1]}"))
                    results[index] = reply[3]
                    done += 1
            now = time.monotonic()
            for sequence, (index, sent, attempts) in list(outstanding.items()):
                if now - sent < self.timeout:
                    continue
                if attempts >= self.retries:
                    raise DevlinkError(f"no answer from {self.address[0]}:{PORT}")
                del outstanding[sequence]
                new_sequence = self._next_sequence()
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
    sub.add_parser("reboot")
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
        elif args.command == "reboot":
            link.request(CMD_REBOOT)
            print("rebooting")
    except DevlinkError as e:
        print(f"devlink: {e}", file=sys.stderr)
        return 1
    except (KeyboardInterrupt, BrokenPipeError):
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
