#include "drivers/io_apic.h"

#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"

namespace ioapic {
namespace {

constexpr uint32_t kRegVersion = 0x01;
constexpr uint32_t kRegRedirection = 0x10;  // two 32-bit registers per input

constexpr uint32_t kMasked = 1u << 16;
constexpr uint32_t kLevelTriggered = 1u << 15;
constexpr uint32_t kActiveLow = 1u << 13;

}  // namespace

uint32_t IoApic::Read(uint32_t reg) const {
  mmio_[0] = reg;   // IOREGSEL
  return mmio_[4];  // IOWIN at +0x10
}

void IoApic::Write(uint32_t reg, uint32_t value) {
  mmio_[0] = reg;
  mmio_[4] = value;
}

bool IoApic::Init(uint64_t address, uint32_t gsi_base) {
  if (!x86::IsMapped(address) && !x86::MapUncached(address, 4096)) {
    Log("ioapic: cannot map %llx\n", static_cast<unsigned long long>(address));
    return false;
  }
  mmio_ = reinterpret_cast<volatile uint32_t*>(address);
  gsi_base_ = gsi_base;
  inputs_ = ((Read(kRegVersion) >> 16) & 0xff) + 1;
  for (uint32_t i = 0; i < inputs_; ++i) {
    Write(kRegRedirection + 2 * i, kMasked);
    Write(kRegRedirection + 2 * i + 1, 0);
  }
  Log("ioapic: %llx, GSIs %u..%u, all masked\n", static_cast<unsigned long long>(address), gsi_base,
      gsi_base + inputs_ - 1);
  return true;
}

void IoApic::Route(uint32_t gsi, uint8_t vector, uint16_t flags, uint32_t apic_id) {
  const uint32_t input = gsi - gsi_base_;
  uint32_t low = vector;  // fixed delivery, physical destination
  if ((flags & 0x3) == 0x3) {
    low |= kActiveLow;
  }
  if (((flags >> 2) & 0x3) == 0x3) {
    low |= kLevelTriggered;
  }
  Write(kRegRedirection + 2 * input + 1, apic_id << 24);
  Write(kRegRedirection + 2 * input, low);
}

void IoApic::Mask(uint32_t gsi) { Write(kRegRedirection + 2 * (gsi - gsi_base_), kMasked); }

void IoApic::MaskAll() {
  for (uint32_t i = 0; i < inputs_; ++i) {
    Mask(gsi_base_ + i);
  }
}

}  // namespace ioapic
