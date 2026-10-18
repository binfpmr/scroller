// Debug link: a small UDP request/response protocol for developing on real hardware over the network
// (tools/devlink.py is the other end). The development PC reads the log, takes screenshots (memory reads of the
// 320x240 surface), injects keys, peeks/pokes memory and reboots the machine (which then network-boots the new build).
//
// Port kDevlinkPort; every datagram starts with an 8-byte header (little endian):
//   char magic[2] = "DL"; u8 command; u8 status (replies: 0 ok, else an error); u32 sequence (echoed in the reply)
// Commands (the reply has the same command and sequence):
//   0x01 INFO       -> text "key=value" lines (version, image base, screen address and size, addresses)
//   0x02 READ       u64 address, u32 length (<= kMaxRead)  -> the bytes; status 1 if a page is not mapped
//   0x03 WRITE      u64 address, bytes                     -> empty; status 1 if a page is not mapped
//   0x04 KEY        u8 platform::Key, u8 down               -> empty
//   0x05 REBOOT     -> empty reply, then the machine reboots
//   0x06 SUBSCRIBE  u8 from_start (1: whole kept history)   -> empty; the log then streams to the sender
//   0x07 GDB        a gdb packet's payload (between '$' and '#') -> the reply payload (platform/gdb_stub); a repeated
//                   sequence number gets the cached reply
//   0x08 GDB_WAIT   -> status 0 and the stop reply if the machine is stopped, status 4 while it runs
//   0x09 GDB_STOP   -> empty; the machine stops at the next main loop iteration (gdb's Ctrl+C)
//   0x80 LOG        (target -> subscriber, unsolicited)     u64 position, text
#pragma once

#include <stdint.h>

#include "gfx/surface.h"
#include "net/stack.h"
#include "platform/boot_info.h"
#include "platform/input.h"

namespace platform {

inline constexpr uint16_t kDevlinkPort = 5555;
inline constexpr uint32_t kMaxRead = 1024;

// Binds the port. `tracker` receives injected keys; `screen` is reported by INFO.
void InitDevlink(net::Stack* stack, const BootInfo& boot, InputTracker* tracker, const gfx::Surface* screen);

// Streams new log output to the subscriber and carries out a requested reboot. Call after PollNetwork.
void PollDevlink();

}  // namespace platform
