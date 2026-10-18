// Intel 8254x gigabit Ethernet ("e1000"), the NIC QEMU emulates by default (8086:100E). Legacy descriptors, polled.
#pragma once

#include "drivers/nic.h"
#include "drivers/pci.h"
#include "lib/arena.h"

namespace e1000 {

// PCI IDs this driver handles.
bool Supports(const pci::Device& device);

class Driver final : public nic::Driver {
 public:
  // Resets the controller, reads the MAC and sets up the rings. False (logged) if the BAR or memory is unusable.
  bool Init(const pci::Device& device, lib::Arena& arena);

  bool Send(const uint8_t* frame, size_t length) override;
  size_t Receive(const uint8_t** frame) override;
  int LinkSpeed() override;

 private:
  struct Descriptor {
    uint64_t address;
    uint16_t length;
    uint8_t checksum_offset;  // rx: checksum low byte
    uint8_t command;          // rx: checksum high byte
    uint8_t status;
    uint8_t checksum_start;  // rx: errors
    uint16_t special;
  };
  static_assert(sizeof(Descriptor) == 16);

  volatile uint8_t* mmio_ = nullptr;
  volatile Descriptor* rx_ = nullptr;
  volatile Descriptor* tx_ = nullptr;
  uint8_t* rx_buffers_ = nullptr;
  uint8_t* tx_buffers_ = nullptr;
  int rx_next_ = 0;
  int rx_held_ = -1;  // descriptor whose frame the caller still reads; returned to the hardware on the next Receive
  int tx_next_ = 0;
};

}  // namespace e1000
