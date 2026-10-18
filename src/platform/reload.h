// Hot reload: starts a new build of the game without a reboot (no firmware, no network boot: ~1 s instead of ~30).
//
// The new scroller.efi arrives over the debug link (tools/devlink.py reload). PrepareReload loads it into the reload
// slot the running image does not use (BootInfo.reload_slots). ExecuteReload stops every device that writes into
// memory by DMA, then calls the new image's entry point (crt0.S, which relocates it) on the original stack with
// ImageHandle = kReloadHandle and, in place of the system table, a ReloadHandoff carrying the BootInfo. The new
// efi_main sees the handle, skips the UEFI phase and runs the game: it reuses the game memory from scratch and
// initialises every device itself. Page tables we added live in their own memory (x86::SetPageTableArena), which the
// handoff passes on with its fill level.
//
// Compatibility: every image contains a ReloadAbi marker. A build whose marker differs (BootInfo or the handoff
// changed) is refused before anything is stopped; the tool then reboots instead. Bump kReloadVersion when BootInfo
// changes meaning without changing size.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "platform/boot_info.h"

namespace platform {

inline constexpr uint32_t kReloadVersion = 1;
inline constexpr uint64_t kReloadHandle = 0x4441'4f4c'4552'4353;  // "SCRELOAD", passed as ImageHandle

struct ReloadHandoff {
  uint64_t magic = 0;
  uint32_t version = 0;
  uint32_t size = 0;  // sizeof(ReloadHandoff)
  BootInfo boot;
};

// Checks and loads `file` (a scroller.efi) into the free slot. On error returns false with a static message (also
// for an incompatible build).
bool PrepareReload(const BootInfo& boot, const uint8_t* file, size_t size, const char** error);

// After a successful PrepareReload: stops DMA devices and starts the new image. Does not return.
[[noreturn]] void ExecuteReload(const BootInfo& boot);

// In efi_main: true if this start is a hot reload; then *boot is the previous image's BootInfo (assets still to be
// set to the new image's). Reboots if the handoff is not one this build understands.
bool AcceptReload(const void* handle, const void* system_table, BootInfo* boot);

}  // namespace platform
