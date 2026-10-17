#include "gfx/font.h"

#include <vector>

#include "test.h"

TEST(TextWidthCountsLongestLine) {
  EXPECT_EQ(gfx::TextWidth(""), 0);
  EXPECT_EQ(gfx::TextWidth("A"), 5);
  EXPECT_EQ(gfx::TextWidth("FPS 60"), 6 * 6 - 1);
  EXPECT_EQ(gfx::TextWidth("AB\nABCD\nA"), 4 * 6 - 1);
}

TEST(DrawTextSetsGlyphPixelsAndClips) {
  std::vector<gfx::Color> pixels(20 * 10, 0);
  gfx::Surface s{pixels.data(), 20, 10};
  gfx::DrawText(s, 1, 1, "1", 7);
  // '1' row 0 is 00100: only the middle column is lit.
  EXPECT_EQ(s.Row(1)[3], 7u);
  EXPECT_EQ(s.Row(1)[1], 0u);
  // Row 6 is 01110.
  EXPECT_EQ(s.Row(7)[2], 7u);
  EXPECT_EQ(s.Row(7)[4], 7u);
  EXPECT_EQ(s.Row(7)[5], 0u);
  gfx::DrawText(s, 17, 8, "WWW", 9);  // partly outside: must not write out of bounds
  gfx::DrawText(s, -3, -3, "w?", 9);  // lower case and negative positions
}
