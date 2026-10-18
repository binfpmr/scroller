#include "drivers/acpi.h"

namespace acpi {
namespace {

constexpr size_t kHeaderSize = 36;  // standard description header

uint32_t Read32(const uint8_t* p) {
  return p[0] | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}
uint64_t Read64(const uint8_t* p) { return Read32(p) | (static_cast<uint64_t>(Read32(p + 4)) << 32); }
uint16_t Read16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }

bool ChecksumOk(const uint8_t* p, size_t length) {
  uint8_t sum = 0;
  for (size_t i = 0; i < length; ++i) {
    sum = static_cast<uint8_t>(sum + p[i]);
  }
  return sum == 0;
}

bool SignatureIs(const uint8_t* p, const char* signature, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    if (p[i] != static_cast<uint8_t>(signature[i])) {
      return false;
    }
  }
  return true;
}

const uint8_t* ValidTable(uint64_t address, const char* signature) {
  if (address == 0) {
    return nullptr;
  }
  const auto* table = reinterpret_cast<const uint8_t*>(address);
  const uint32_t length = Read32(table + 4);
  if ((signature != nullptr && !SignatureIs(table, signature, 4)) || length < kHeaderSize || length > (1u << 20) ||
      !ChecksumOk(table, length)) {
    return nullptr;
  }
  return table;
}

}  // namespace

const uint8_t* FindTable(const void* rsdp_pointer, const char (&signature)[5]) {
  const auto* rsdp = static_cast<const uint8_t*>(rsdp_pointer);
  if (rsdp == nullptr || !SignatureIs(rsdp, "RSD PTR ", 8) || !ChecksumOk(rsdp, 20)) {
    return nullptr;
  }
  const uint8_t revision = rsdp[15];
  const uint8_t* root = nullptr;
  size_t entry_size = 4;
  if (revision >= 2 && ChecksumOk(rsdp, Read32(rsdp + 20))) {
    root = ValidTable(Read64(rsdp + 24), "XSDT");
    entry_size = 8;
  }
  if (root == nullptr) {
    root = ValidTable(Read32(rsdp + 16), "RSDT");
    entry_size = 4;
  }
  if (root == nullptr) {
    return nullptr;
  }
  const size_t entries = (Read32(root + 4) - kHeaderSize) / entry_size;
  for (size_t i = 0; i < entries; ++i) {
    const uint8_t* entry = root + kHeaderSize + i * entry_size;
    const uint64_t address = entry_size == 8 ? Read64(entry) : Read32(entry);
    if (const uint8_t* table = ValidTable(address, signature)) {
      return table;
    }
  }
  return nullptr;
}

bool ParseMadt(const uint8_t* table, Madt* madt) {
  *madt = Madt();
  const uint32_t length = Read32(table + 4);
  if (length < kHeaderSize + 8) {
    return false;
  }
  madt->local_apic_address = Read32(table + kHeaderSize);
  madt->has_8259 = (Read32(table + kHeaderSize + 4) & 1) != 0;
  for (uint32_t offset = kHeaderSize + 8; offset + 2 <= length;) {
    const uint8_t* e = table + offset;
    const uint8_t type = e[0];
    const uint8_t size = e[1];
    if (size < 2 || offset + size > length) {
      return false;
    }
    switch (type) {
      case 0:  // processor local APIC
        if (size >= 8 && (Read32(e + 4) & 1) != 0) {
          ++madt->cpu_count;
        }
        break;
      case 1:  // I/O APIC
        if (size >= 12 && madt->io_apic_count < Madt::kMaxIoApics) {
          madt->io_apics[madt->io_apic_count++] = {.id = e[2], .address = Read32(e + 4), .gsi_base = Read32(e + 8)};
        }
        break;
      case 2:  // interrupt source override
        if (size >= 10 && e[2] == 0 && madt->override_count < Madt::kMaxOverrides) {
          madt->overrides[madt->override_count++] = {.irq = e[3], .gsi = Read32(e + 4), .flags = Read16(e + 8)};
        }
        break;
      case 5:  // 64-bit local APIC address override
        if (size >= 12) {
          madt->local_apic_address = Read64(e + 4);
        }
        break;
      case 9:  // processor local x2APIC
        if (size >= 16 && (Read32(e + 8) & 1) != 0) {
          ++madt->cpu_count;
        }
        break;
      default:
        break;
    }
    offset += size;
  }
  return true;
}

uint32_t Madt::GsiForIsaIrq(uint8_t irq, uint16_t* flags) const {
  for (int i = 0; i < override_count; ++i) {
    if (overrides[i].irq == irq) {
      *flags = overrides[i].flags;
      return overrides[i].gsi;
    }
  }
  *flags = 0;
  return irq;
}

}  // namespace acpi
