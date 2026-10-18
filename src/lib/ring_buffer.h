// Fixed-size single-producer single-consumer byte queue, e.g. an interrupt handler producing and the main loop
// consuming on the same CPU. Indices are volatile; each side writes only its own index.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lib {

template <size_t kCapacity>
class RingBuffer {
  static_assert((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");

 public:
  // Producer side. Returns false (dropping the byte) if full.
  bool Push(uint8_t byte) {
    const size_t head = head_;
    if (head - tail_ == kCapacity) {
      return false;
    }
    data_[head & (kCapacity - 1)] = byte;
    asm volatile("" ::: "memory");  // data before index
    head_ = head + 1;
    return true;
  }

  // Consumer side. Returns false if empty.
  bool Pop(uint8_t* byte) {
    const size_t tail = tail_;
    if (tail == head_) {
      return false;
    }
    *byte = data_[tail & (kCapacity - 1)];
    asm volatile("" ::: "memory");
    tail_ = tail + 1;
    return true;
  }

  size_t size() const { return head_ - tail_; }

 private:
  uint8_t data_[kCapacity] = {};
  volatile size_t head_ = 0;
  volatile size_t tail_ = 0;
};

}  // namespace lib
