// Debug link: a small UDP request/response protocol for developing on real hardware over the network
// (tools/devlink.py is the other end). The development PC reads the log, takes screenshots (memory reads of the
// 320x240 surface), injects keys, peeks/pokes memory, debugs with gdb, hot-reloads a new build and reboots.
//
// Port kDevlinkPort; every datagram starts with an 8-byte header (little endian):
//   char magic[2] = "DL"; u8 command; u8 status (replies: 0 ok, else an error); u32 sequence (echoed in the reply)
// Commands (the reply has the same command and sequence):
//   0x01 INFO          -> "key=value" text lines (version, boot id, image base, screen, addresses)
//   0x02 READ          u64 address, u32 length (<= kMaxRead) -> the bytes; status 1 if a page is not mapped
//   0x03 WRITE         u64 address, bytes -> empty; status 1 if a page is not mapped
//   0x04 KEY           u8 platform::Key, u8 down -> empty
//   0x05 REBOOT        [u8 platform::ResetMethod] -> empty, then the machine reboots
//   0x06 SUBSCRIBE     u8 from_start (1: whole kept history) -> empty; the log then streams to the sender
//   0x07 GDB           a gdb packet's payload (between '$' and '#') -> the reply payload (platform/gdb_stub);
//                      a repeated sequence number gets the cached reply
//   0x08 GDB_WAIT      -> status 0 and the stop reply if the machine is stopped, status 4 while it runs
//   0x09 GDB_STOP      -> empty; the machine stops at the next main loop iteration (gdb's Ctrl+C)
//   0x0A RELOAD_BEGIN  u32 size -> empty; status 1 if too large (platform/reload)
//   0x0B RELOAD_DATA   u32 offset, bytes -> empty
//   0x0C RELOAD_RUN    u32 size, u32 crc32 -> empty, then the new image starts; status 5 and a message if refused
//   0x80 LOG           (target -> subscriber, unsolicited) u64 position, text
#pragma once

#include <stdint.h>

#include "gfx/surface.h"
#include "lib/arena.h"
#include "net/stack.h"
#include "platform/boot_info.h"
#include "platform/input.h"

namespace platform {

inline constexpr uint16_t kDevlinkPort = 5555;
inline constexpr uint32_t kMaxRead = 1024;

// Binds the port. `tracker` receives injected keys; `screen` is reported by INFO.
// `arena` provides the upload buffer for a hot reload (allocated on the first one).
void InitDevlink(net::Stack* stack, const BootInfo& boot, InputTracker* tracker, const gfx::Surface* screen,
                 lib::Arena* arena);

// Streams new log output to the subscriber and carries out a requested reboot or reload. Call after PollNetwork.
void PollDevlink();

// Ctrl+Alt+R: fetches scroller.efi by TFTP from the server that network-booted the machine and hot-reloads it.
void RequestNetworkReload();
// A short status line for the screen while a reload is fetched or after it failed; empty otherwise.
const char* DevlinkMessage();

}  // namespace platform
