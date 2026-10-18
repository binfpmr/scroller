// PCI configuration space access through the legacy I/O ports 0xCF8/0xCFC (enough for the first 256 bytes).
#pragma once

#include <stdint.h>

namespace pci {

uint32_t Read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);
void Write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value);

struct Device {
  uint8_t bus = 0;
  uint8_t device = 0;
  uint8_t function = 0;
  uint16_t vendor_id = 0xffff;
  uint16_t device_id = 0xffff;
  uint32_t class_code = 0;  // class << 16 | subclass << 8 | prog-if
};

// First device with the given class and subclass (e.g. 0x04, 0x03: HD Audio). False if none.
bool FindByClass(uint8_t base_class, uint8_t subclass, Device* out);

// Memory BAR `index` (0..5) as a physical address, combining 64-bit BARs; 0 if it is not a memory BAR.
uint64_t MemoryBar(const Device& device, int index);

// First device with base class `base_class` (e.g. 0x03, display controller) on any bus. False if none.
bool FindByClass(uint8_t base_class, Device* out);

// Up to `max` devices with base class `base_class`, in bus order. Returns how many were found.
int FindAllByClass(uint8_t base_class, Device* out, int max);

}  // namespace pci
