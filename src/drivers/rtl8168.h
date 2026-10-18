// Realtek RTL8168/8111 family gigabit Ethernet (the HP Pavilion's 10EC:8168), descriptor rings, polled. Relies on the
// firmware having set up the PHY (as it does for network boot); chip-version specific fixes are not applied.
#pragma once

#include "drivers/nic.h"
#include "drivers/pci.h"
#include "lib/arena.h"

namespace rtl8168 {

// PCI IDs this driver handles.
bool Supports(const pci::Device& device);

class Driver final : public nic::Driver {
 public:
  // Resets the controller, reads the MAC and sets up the rings. False (logged) if the BAR or memory is unusable.
  bool Init(const pci::Device& device, lib::Arena& arena);

  bool Send(const uint8_t* frame, size_t length) override;
  size_t Receive(const uint8_t** frame) override;
  int LinkSpeed() override;
  void Describe(char* out, size_t size) override;
  bool Restart() override;
  void Stop() override;

 private:
  struct Descriptor {
    uint32_t opts1;  // owner, ring end, first/last fragment, length
    uint32_t opts2;
    uint64_t address;
  };
  static_assert(sizeof(Descriptor) == 16);

  void Setup();
  void GiveRx(int index);

  uint32_t version_ = 0;
  uint32_t misc_at_boot_ = 0;  // as the firmware left them
  uint8_t mcu_at_boot_ = 0;
  uint16_t seen_status_ = 0;  // every interrupt status bit seen since boot
  uint32_t restarts_ = 0;

  volatile uint8_t* mmio_ = nullptr;
  volatile Descriptor* rx_ = nullptr;
  volatile Descriptor* tx_ = nullptr;
  uint8_t* rx_buffers_ = nullptr;
  uint8_t* tx_buffers_ = nullptr;
  int rx_next_ = 0;
  int rx_held_ = -1;  // descriptor whose frame the caller still reads; returned to the hardware on the next Receive
  int tx_next_ = 0;
};

}  // namespace rtl8168
