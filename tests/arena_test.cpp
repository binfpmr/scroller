#include "lib/arena.h"

#include <cstdint>

#include "test.h"

TEST(ArenaAlignsAndExhausts) {
  alignas(64) static uint8_t block[256];
  lib::Arena arena(block, sizeof(block));
  void* a = arena.Allocate(1);
  void* b = arena.Allocate(8, 64);
  EXPECT_EQ(a, static_cast<void*>(block));
  EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 64, 0u);
  EXPECT_EQ(arena.used(), 72u);
  EXPECT_TRUE(arena.Allocate(256) == nullptr);
  EXPECT_EQ(arena.used(), 72u);                               // failed allocation leaves the arena unchanged
  EXPECT_TRUE(arena.AllocateArray<uint32_t>(44) != nullptr);  // aligned to 16: 80 + 176 = 256, exactly full
  EXPECT_TRUE(arena.Allocate(1) == nullptr);
}

TEST(ArenaRejectsOverflowingCount) {
  alignas(16) static uint8_t block[64];
  lib::Arena arena(block, sizeof(block));
  EXPECT_TRUE(arena.AllocateArray<uint64_t>(SIZE_MAX / 4) == nullptr);
}
