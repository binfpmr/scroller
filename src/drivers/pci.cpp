#include "drivers/pci.h"

#include "arch/x86_64/io.h"

namespace pci {

uint32_t Read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  const uint32_t address =
      0x80000000u | (uint32_t{bus} << 16) | (uint32_t{device} << 11) | (uint32_t{function} << 8) | (offset & 0xfc);
  x86::Outl(0xcf8, address);
  return x86::Inl(0xcfc);
}

bool FindByClass(uint8_t base_class, Device* out) {
  for (int bus = 0; bus < 256; ++bus) {
    for (int dev = 0; dev < 32; ++dev) {
      for (int fn = 0; fn < 8; ++fn) {
        const uint32_t id = Read32(bus, dev, fn, 0x00);
        if ((id & 0xffff) == 0xffff) {
          if (fn == 0) {
            break;  // no device here
          }
          continue;
        }
        const uint32_t class_code = Read32(bus, dev, fn, 0x08) >> 8;
        if ((class_code >> 16) == base_class) {
          *out = {.bus = static_cast<uint8_t>(bus),
                  .device = static_cast<uint8_t>(dev),
                  .function = static_cast<uint8_t>(fn),
                  .vendor_id = static_cast<uint16_t>(id & 0xffff),
                  .device_id = static_cast<uint16_t>(id >> 16),
                  .class_code = class_code};
          return true;
        }
        const bool multi_function = (Read32(bus, dev, 0, 0x0c) & 0x00800000) != 0;
        if (fn == 0 && !multi_function) {
          break;
        }
      }
    }
  }
  return false;
}

}  // namespace pci
