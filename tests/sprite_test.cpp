#include "gfx/sprite.h"

#include <cstring>
#include <string>
#include <vector>

#include "game_test_util.h"

namespace {

struct Parsed {
  std::vector<uint8_t> storage = std::vector<uint8_t>(1 << 16);
  gfx::SpriteSheet sheet;
  const char* error = nullptr;
  int line = 0;
  bool ok = false;

  explicit Parsed(const std::string& text) {
    lib::Arena arena(storage.data(), storage.size());
    ok = sheet.Parse(text.data(), text.size(), arena, &error, &line);
  }
};

const char* kSheet =
    "# test sheet\n"
    "palette\n"
    "  R ff0000\n"
    "  g 00Ff00\n"
    "size 3 2\n"
    "frame a\n"
    "R.g\n"
    "gRR\r\n"
    "\n"
    "frame b\n"
    "...\n"
    "..R\n";

}  // namespace

TEST(ParsesFramesAndPalette) {
  Parsed p(kSheet);
  EXPECT_TRUE(p.ok);
  EXPECT_EQ(p.sheet.frame_count(), 2);
  const gfx::Sprite* a = p.sheet.Find("a");
  EXPECT_TRUE(a != nullptr && a->width == 3 && a->height == 2);
  EXPECT_EQ(a->pixels[0], 0xff0000u);
  EXPECT_EQ(a->pixels[1], gfx::kTransparent);
  EXPECT_EQ(a->pixels[2], 0x00ff00u);
  EXPECT_EQ(a->pixels[5], 0xff0000u);
  EXPECT_TRUE(p.sheet.Find("b") != nullptr && p.sheet.Find("c") == nullptr && p.sheet.Find("") == nullptr);
}

TEST(ReportsErrorsWithLineNumbers) {
  struct Case {
    const char* text;
    int line;
    const char* error;
  };
  const Case cases[] = {
      {"size 2 1\nframe x\nAB\n", 3, "character not in the palette"},
      {"palette\n R ff00\n", 2, "expected palette entry '<char> <rrggbb>' ('.' is reserved for transparent)"},
      {"frame x\n", 1, "'frame' before 'size'"},
      {"size 2 2\nframe x\n..\n", 3, "frame has fewer rows than its size"},
      {"size 2 1\nframe x\n...\n", 3, "frame row length differs from the frame width"},
      {"size 1 1\nframe x\n.\nframe x\n.\n", 4, "duplicate frame name"},
      {"hello\n", 1, "unknown directive (expected palette, size or frame)"},
      {"# nothing\n", 1, "sprite sheet has no frames"},
  };
  for (const Case& c : cases) {
    Parsed p(c.text);
    EXPECT_TRUE(!p.ok);
    EXPECT_STREQ(p.error ? p.error : "", c.error);
    EXPECT_EQ(p.line, c.line);
  }
}

TEST(DrawSpriteHandlesTransparencyFlipAndClipping) {
  Parsed p(kSheet);
  const gfx::Sprite& a = *p.sheet.Find("a");
  std::vector<gfx::Color> pixels(4 * 3, 7);
  gfx::Surface s{pixels.data(), 4, 3};
  gfx::DrawSprite(s, a, 0, 0);
  EXPECT_EQ(s.Row(0)[0], 0xff0000u);
  EXPECT_EQ(s.Row(0)[1], 7u);  // transparent pixel left the background
  EXPECT_EQ(s.Row(1)[0], 0x00ff00u);
  gfx::DrawSprite(s, a, 1, 1, /*flip_x=*/true);
  EXPECT_EQ(s.Row(1)[1], 0x00ff00u);  // flipped: 'g' of row 0 is now on the left
  EXPECT_EQ(s.Row(1)[3], 0xff0000u);
  EXPECT_EQ(s.Row(2)[1], 0xff0000u);
  EXPECT_EQ(s.Row(2)[3], 0x00ff00u);
  gfx::DrawSprite(s, a, -2, -1);  // mostly outside: only the clipped part is drawn
  gfx::DrawSprite(s, a, 3, 2);
}
