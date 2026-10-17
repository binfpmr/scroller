// Marks a physical range (the framebuffer) write-combining by editing the firmware's identity-mapped page tables.
//
// Firmware usually maps the framebuffer uncached, so every store goes to the device on its own; write-combining lets
// the CPU merge stores into full bursts, which makes presenting a frame many times faster on real hardware.
// PAT entry 7 (normally UC) is reprogrammed to WC and the framebuffer's page table entries are pointed at it. Large
// pages that only partly cover the range are split first. Only valid after ExitBootServices, with interrupts off.
#pragma once

#include <stdint.h>

#include "lib/arena.h"

namespace x86 {

class WriteCombining {
 public:
  // Prepares the page table entries (splitting large pages with tables from `arena`) and switches the range to WC.
  // Returns false (and changes nothing) if the CPU or the page tables are not what we expect.
  bool Init(lib::Arena& arena, uint64_t base, uint64_t size);

  // Switches between WC and the firmware's original memory type, e.g. to compare speed.
  void Enable(bool on);
  bool enabled() const { return enabled_; }
  bool available() const { return count_ > 0; }

 private:
  struct Entry {
    uint64_t* entry;
    uint64_t original;
    uint64_t write_combining;
  };

  Entry* entries_ = nullptr;
  int count_ = 0;
  bool enabled_ = false;
};

}  // namespace x86
