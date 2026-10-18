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

  // Asset pack (lib/pack.h), linked into the image.
  const void* assets = nullptr;
  size_t assets_size = 0;

  // One 4 KiB page below 16 MiB for ISA DMA (Sound Blaster); 0 if none could be allocated.
  uint64_t isa_dma_buffer = 0;

  // Set when the image was booted over the network (PXE): the addresses the firmware got from DHCP. The server is
  // the development PC that served the image.
  struct NetBoot {
    bool valid = false;
    uint8_t ip[4] = {};
    uint8_t netmask[4] = {};
    uint8_t server[4] = {};
  } netboot;

  // ACPI root pointer (RSDP) from the UEFI configuration table; null if the firmware has no ACPI tables.
  const void* acpi_rsdp = nullptr;

  // True after ExitBootServices: the game owns the machine (page tables, devices). False in the uefi-input build.
  bool owns_machine = false;
};

}  // namespace platform
