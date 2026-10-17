#include "platform/input.h"

#include "drivers/ps2_decoder.h"
#include "test.h"

using platform::InputTracker;
using platform::Key;

TEST(PressHoldRelease) {
  InputTracker t;
  t.OnKey(Key::kRight, true);
  auto s = t.NextTick();
  EXPECT_TRUE(s.Pressed(Key::kRight) && s.Held(Key::kRight) && !s.Released(Key::kRight));
  s = t.NextTick();
  EXPECT_TRUE(!s.Pressed(Key::kRight) && s.Held(Key::kRight));
  t.OnKey(Key::kRight, true);  // typematic repeat
  s = t.NextTick();
  EXPECT_TRUE(!s.Pressed(Key::kRight));
  t.OnKey(Key::kRight, false);
  s = t.NextTick();
  EXPECT_TRUE(s.Released(Key::kRight) && !s.Held(Key::kRight));
}

TEST(TapWithinOneTickIsStillAPress) {
  InputTracker t;
  t.OnKey(Key::kJump, true);
  t.OnKey(Key::kJump, false);
  const auto s = t.NextTick();
  EXPECT_TRUE(s.Pressed(Key::kJump) && s.Released(Key::kJump) && !s.Held(Key::kJump));
}

TEST(ReleaseWithoutPressIsIgnored) {
  InputTracker t;
  t.OnKey(Key::kLeft, false);
  EXPECT_EQ(t.NextTick().released, 0u);
}

TEST(Ps2DecodesExtendedAndBreakCodes) {
  ps2::Decoder d;
  ps2::ScanEvent e;
  EXPECT_TRUE(!d.Feed(0xe0, &e));
  EXPECT_TRUE(d.Feed(0x4d, &e));
  EXPECT_EQ(e.code, 0xe04d);
  EXPECT_TRUE(e.down);
  EXPECT_TRUE(!d.Feed(0xe0, &e));
  EXPECT_TRUE(d.Feed(0xcd, &e));
  EXPECT_EQ(e.code, 0xe04d);
  EXPECT_TRUE(!e.down);
  EXPECT_TRUE(d.Feed(0x39, &e));  // space, not extended
  EXPECT_EQ(e.code, 0x39);
}

TEST(Ps2SkipsPauseSequenceAndAcks) {
  ps2::Decoder d;
  ps2::ScanEvent e;
  int events = 0;
  for (uint8_t b : {0xe1, 0x1d, 0x45, 0xe1, 0x9d, 0xc5, 0xfa}) {
    events += d.Feed(b, &e) ? 1 : 0;
  }
  EXPECT_EQ(events, 0);
  EXPECT_TRUE(d.Feed(0x01, &e));
  EXPECT_EQ(e.code, 0x01);
}

TEST(Ps2MapsGameKeys) {
  Key k;
  EXPECT_TRUE(ps2::MapKey(0xe04b, &k) && k == Key::kLeft);
  EXPECT_TRUE(ps2::MapKey(0xe048, &k) && k == Key::kJump);  // up arrow
  EXPECT_TRUE(!ps2::MapKey(0x2c, &k));                       // Z: unused
  EXPECT_TRUE(ps2::MapKey(0x39, &k) && k == Key::kJump);
  EXPECT_TRUE(ps2::MapKey(0x01, &k) && k == Key::kEscape);
  EXPECT_TRUE(!ps2::MapKey(0x1e, &k));  // A
}
