// Loading a PE32+ image (our own scroller.efi) into memory, for the hot reload: sections copied to their RVAs,
// everything else zeroed. Relocations are not applied here: our entry point (crt0.S) relocates itself.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lib {

struct PeImage {
  uint32_t size_of_image = 0;  // bytes the loaded image spans
  uint32_t entry_rva = 0;      // AddressOfEntryPoint
};

// Checks headers and section bounds. On error returns false with a static message.
bool ParsePe(const uint8_t* file, size_t size, PeImage* image, const char** error);

// Loads into `dest` (at least size_of_image bytes, 4 KiB aligned): headers, sections, zero fill.
bool LoadPe(const uint8_t* file, size_t size, uint8_t* dest, size_t dest_size, PeImage* image, const char** error);

// CRC-32 (IEEE, as zlib.crc32) of `size` bytes.
uint32_t Crc32(const uint8_t* data, size_t size);

}  // namespace lib
