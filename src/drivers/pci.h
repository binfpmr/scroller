// PCI configuration space access through the legacy I/O ports 0xCF8/0xCFC (enough for the first 256 bytes).
#pragma once

#include <stdint.h>

namespace pci {

uint32_t Read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

struct Device {
  uint8_t bus = 0;
  uint8_t device = 0;
  uint8_t function = 0;
  uint16_t vendor_id = 0xffff;
  uint16_t device_id = 0xffff;
  uint32_t class_code = 0;  // class << 16 | subclass << 8 | prog-if
};

// First device with base class `base_class` (e.g. 0x03, display controller) on any bus. False if none.
bool FindByClass(uint8_t base_class, Device* out);

}  // namespace pci
