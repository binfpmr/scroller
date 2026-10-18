// ACPI table lookup and MADT (interrupt controller) parsing. Physical addresses are used as pointers (identity
// mapping), which also lets the host tests build tables in ordinary memory.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace acpi {

// Finds a table by signature (e.g. "APIC") through the RSDP's XSDT (ACPI 2+) or RSDT. Checksums of the RSDP, the root
// table and the found table are verified. Null if not found or invalid.
const uint8_t* FindTable(const void* rsdp, const char (&signature)[5]);

struct IoApic {
  uint8_t id = 0;
  uint64_t address = 0;
  uint32_t gsi_base = 0;
};

// An ISA IRQ wired to a different global system interrupt (GSI), possibly with non-ISA polarity/trigger.
struct IsaOverride {
  uint8_t irq = 0;
  uint32_t gsi = 0;
  uint16_t flags = 0;  // bits 0-1 polarity (3 = active low), bits 2-3 trigger mode (3 = level)
};

struct Madt {
  static constexpr int kMaxIoApics = 4;
  static constexpr int kMaxOverrides = 16;

  uint64_t local_apic_address = 0;
  int cpu_count = 0;  // enabled local APICs / x2APICs
  IoApic io_apics[kMaxIoApics];
  int io_apic_count = 0;
  IsaOverride overrides[kMaxOverrides];
  int override_count = 0;
  bool has_8259 = false;  // PCAT_COMPAT: legacy PICs present (must be masked)

  // GSI for an ISA IRQ (identity unless overridden); `flags` gets the override flags (0 = ISA default: active high,
  // edge triggered).
  uint32_t GsiForIsaIrq(uint8_t irq, uint16_t* flags) const;
};

// Parses a MADT ("APIC" table). False if the table is malformed.
bool ParseMadt(const uint8_t* table, Madt* madt);

}  // namespace acpi
