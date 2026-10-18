// I/O APIC: routes device interrupts (global system interrupts, GSIs) to CPU vectors.
#pragma once

#include <stdint.h>

namespace ioapic {

class IoApic {
 public:
  // Maps the registers if needed and masks every input. False if the registers cannot be reached.
  bool Init(uint64_t address, uint32_t gsi_base);

  bool Handles(uint32_t gsi) const { return gsi >= gsi_base_ && gsi < gsi_base_ + inputs_; }
  int inputs() const { return static_cast<int>(inputs_); }

  // Routes `gsi` to `vector` on the CPU with local APIC id `apic_id` and unmasks it. `flags` are MADT override flags
  // (0: ISA default, active high and edge triggered).
  void Route(uint32_t gsi, uint8_t vector, uint16_t flags, uint32_t apic_id);
  void Mask(uint32_t gsi);
  void MaskAll();

 private:
  uint32_t Read(uint32_t reg) const;
  void Write(uint32_t reg, uint32_t value);

  volatile uint32_t* mmio_ = nullptr;
  uint32_t gsi_base_ = 0;
  uint32_t inputs_ = 0;
};

}  // namespace ioapic
