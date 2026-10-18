// Interface of the Ethernet drivers (polled, no interrupts): what platform/network.cpp and the net::Stack need.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "net/stack.h"

namespace nic {

inline constexpr int kRxRing = 64;
inline constexpr int kTxRing = 32;
inline constexpr size_t kBufferSize = 2048;  // per descriptor; a frame always fits in one

class Driver : public net::Link {
 public:
  // The next received frame (without FCS), or 0 if none. `*frame` stays valid until the next call.
  virtual size_t Receive(const uint8_t** frame) = 0;
  // Link speed in Mbit/s, 0 while the cable is unplugged or negotiation is still running.
  virtual int LinkSpeed() = 0;
  // Stops sending and receiving (no more DMA into our buffers), before a hot reload.
  virtual void Stop() = 0;
  // Resets the controller and sets it up again (recovery when receiving has stalled). False if the driver has no
  // need for that.
  virtual bool Restart() { return false; }
  // Register state for the F1 overlay when bringing up a driver on new hardware; empty if there is nothing to show.
  virtual void Describe(char* out, size_t size) {
    if (size > 0) {
      out[0] = '\0';
    }
  }

  const net::Mac& mac() const { return mac_; }

 protected:
  ~Driver() = default;
  net::Mac mac_;
};

}  // namespace nic
