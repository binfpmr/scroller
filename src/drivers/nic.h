// Interface of the Ethernet drivers (polled, no interrupts): what platform/network.cpp and the net::Stack need.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/stack.h"

namespace nic {

inline constexpr int kRxRing = 32;
inline constexpr int kTxRing = 32;
inline constexpr size_t kBufferSize = 2048;  // per descriptor; a frame always fits in one

class Driver : public net::Link {
 public:
  // The next received frame (without FCS), or 0 if none. `*frame` stays valid until the next call.
  virtual size_t Receive(const uint8_t** frame) = 0;
  // Link speed in Mbit/s, 0 while the cable is unplugged or negotiation is still running.
  virtual int LinkSpeed() = 0;

  const net::Mac& mac() const { return mac_; }

 protected:
  ~Driver() = default;
  net::Mac mac_;
};

}  // namespace nic
