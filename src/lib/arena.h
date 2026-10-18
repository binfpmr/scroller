// Bump allocator over one block of memory. Nothing is freed individually; the game allocates everything up front.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lib {

class Arena {
 public:
  Arena() = default;
  Arena(void* base, size_t size, size_t used = 0) : base_(static_cast<uint8_t*>(base)), size_(size), used_(used) {}

  // Returns nullptr when the arena is exhausted. `align` must be a power of two.
  void* Allocate(size_t size, size_t align = 16) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(base_) + used_;
    const uintptr_t aligned = (start + align - 1) & ~(static_cast<uintptr_t>(align) - 1);
    const size_t offset = aligned - reinterpret_cast<uintptr_t>(base_);
    if (offset > size_ || size > size_ - offset) {
      return nullptr;
    }
    used_ = offset + size;
    return base_ + offset;
  }

  // Uninitialized storage for `count` objects of a trivially constructible type.
  template <typename T>
  T* AllocateArray(size_t count) {
    static_assert(__is_trivially_constructible(T));
    if (count > SIZE_MAX / sizeof(T)) {
      return nullptr;
    }
    return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T) < 16 ? 16 : alignof(T)));
  }

  size_t used() const { return used_; }
  size_t size() const { return size_; }

 private:
  uint8_t* base_ = nullptr;
  size_t size_ = 0;
  size_t used_ = 0;
};

}  // namespace lib
