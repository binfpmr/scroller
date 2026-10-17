// Everything the loader phase (boot/) hands to the running game. After ExitBootServices this is the only link to the
// firmware: no boot service may be called any more, only the runtime services pointer stays valid.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gfx/framebuffer.h"
#include "uefi/uefi.h"

namespace platform {

struct BootInfo {
  gfx::Framebuffer framebuffer;
  uint64_t tsc_hz = 0;

  // Memory reserved for the game (EfiLoaderData pages).
  void* memory = nullptr;
  size_t memory_size = 0;

  // Final UEFI memory map, as passed to ExitBootServices (empty in the SCROLLER_UEFI_INPUT build).
  const EFI_MEMORY_DESCRIPTOR* memory_map = nullptr;
  size_t memory_map_size = 0;
  size_t memory_descriptor_size = 0;

  EFI_RUNTIME_SERVICES* runtime_services = nullptr;

  // Asset pack (lib/pack.h) read from \\scroller\\assets.pak on the boot volume; null if it could not be loaded.
  const void* assets = nullptr;
  size_t assets_size = 0;

  // True after ExitBootServices: the game owns the machine (page tables, devices). False in the uefi-input build.
  bool owns_machine = false;
};

}  // namespace platform
