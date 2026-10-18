#include "drivers/pci.h"

#include "arch/x86_64/io.h"

namespace pci {

uint32_t Read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
  const uint32_t address =
      0x80000000u | (uint32_t{bus} << 16) | (uint32_t{device} << 11) | (uint32_t{function} << 8) | (offset & 0xfc);
  x86::Outl(0xcf8, address);
  return x86::Inl(0xcfc);
}

void Write32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset, uint32_t value) {
  const uint32_t address =
      0x80000000u | (uint32_t{bus} << 16) | (uint32_t{device} << 11) | (uint32_t{function} << 8) | (offset & 0xfc);
  x86::Outl(0xcf8, address);
  x86::Outl(0xcfc, value);
}

uint64_t MemoryBar(const Device& d, int index) {
  const uint8_t offset = static_cast<uint8_t>(0x10 + 4 * index);
  const uint32_t low = Read32(d.bus, d.device, d.function, offset);
  if ((low & 1) != 0) {
    return 0;  // I/O BAR
  }
  uint64_t bar = low & ~0xfull;
  if (((low >> 1) & 3) == 2 && index < 5) {
    bar |= static_cast<uint64_t>(Read32(d.bus, d.device, d.function, static_cast<uint8_t>(offset + 4))) << 32;
  }
  return bar;
}

namespace {

// Calls `visit(device)` for every function on every bus until it returns false.
template <typename Visit>
void Scan(Visit visit) {
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
        const Device device = {.bus = static_cast<uint8_t>(bus),
                               .device = static_cast<uint8_t>(dev),
                               .function = static_cast<uint8_t>(fn),
                               .vendor_id = static_cast<uint16_t>(id & 0xffff),
                               .device_id = static_cast<uint16_t>(id >> 16),
                               .class_code = Read32(bus, dev, fn, 0x08) >> 8};
        if (!visit(device)) {
          return;
        }
        const bool multi_function = (Read32(bus, dev, 0, 0x0c) & 0x00800000) != 0;
        if (fn == 0 && !multi_function) {
          break;
        }
      }
    }
  }
}

}  // namespace

bool FindByClass(uint8_t base_class, uint8_t subclass, Device* out) {
  bool found = false;
  Scan([&](const Device& d) {
    found = (d.class_code >> 8) == ((uint32_t{base_class} << 8) | subclass);
    if (found) {
      *out = d;
    }
    return !found;
  });
  return found;
}

bool FindByClass(uint8_t base_class, Device* out) {
  bool found = false;
  Scan([&](const Device& d) {
    found = (d.class_code >> 16) == base_class;
    if (found) {
      *out = d;
    }
    return !found;
  });
  return found;
}

int FindAllByClass(uint8_t base_class, Device* out, int max) {
  int count = 0;
  Scan([&](const Device& d) {
    if ((d.class_code >> 16) == base_class) {
      out[count++] = d;
    }
    return count < max;
  });
  return count;
}

}  // namespace pci
