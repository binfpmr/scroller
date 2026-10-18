#include "lib/ring_buffer.h"

#include "test.h"

TEST(RingBufferFifoAndWrapAround) {
  lib::RingBuffer<4> ring;
  uint8_t b = 0;
  EXPECT_TRUE(!ring.Pop(&b));
  for (int round = 0; round < 5; ++round) {  // indices wrap past the capacity several times
    EXPECT_TRUE(ring.Push(1) && ring.Push(2) && ring.Push(3));
    EXPECT_TRUE(ring.Pop(&b) && b == 1);
    EXPECT_TRUE(ring.Push(4) && ring.Push(5));
    EXPECT_TRUE(!ring.Push(6));  // full: 2 3 4 5
    EXPECT_EQ(ring.size(), 4u);
    for (uint8_t expected : {2, 3, 4, 5}) {
      EXPECT_TRUE(ring.Pop(&b) && b == expected);
    }
    EXPECT_TRUE(!ring.Pop(&b));
  }
}
