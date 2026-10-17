#include "gfx/sprite.h"

#include "gfx/draw.h"

namespace gfx {
namespace {

// A view of one line of the text, without the line break.
struct Line {
  const char* begin;
  const char* end;
};

class LineReader {
 public:
  LineReader(const char* text, size_t length) : pos_(text), end_(text + length) {}

  bool Next(Line* line) {
    if (pos_ >= end_) {
      return false;
    }
    const char* start = pos_;
    while (pos_ < end_ && *pos_ != '\n') {
      ++pos_;
    }
    const char* stop = pos_;
    if (pos_ < end_) {
      ++pos_;  // skip '\n'
    }
    while (stop > start && (stop[-1] == '\r' || stop[-1] == ' ' || stop[-1] == '\t')) {
      --stop;
    }
    *line = {start, stop};
    ++number_;
    return true;
  }
  int number() const { return number_; }

 private:
  const char* pos_;
  const char* end_;
  int number_ = 0;
};

const char* SkipSpaces(const char* p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t')) {
    ++p;
  }
  return p;
}

// Matches `word` followed by a space or the end of the line; returns the position after it or null.
const char* MatchWord(const char* p, const char* end, const char* word) {
  while (*word != '\0') {
    if (p >= end || *p != *word) {
      return nullptr;
    }
    ++p;
    ++word;
  }
  return (p == end || *p == ' ' || *p == '\t') ? p : nullptr;
}

bool ParseInt(const char** p, const char* end, int* value) {
  const char* q = SkipSpaces(*p, end);
  int v = 0;
  const char* start = q;
  while (q < end && *q >= '0' && *q <= '9' && v < 100000) {
    v = v * 10 + (*q - '0');
    ++q;
  }
  if (q == start) {
    return false;
  }
  *value = v;
  *p = q;
  return true;
}

int HexDigit(char c) {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

}  // namespace

bool SpriteSheet::Parse(const char* text, size_t length, lib::Arena& arena, const char** error, int* error_line) {
  Color palette[128];
  bool defined[128] = {};
  defined[static_cast<int>('.')] = true;
  palette[static_cast<int>('.')] = kTransparent;
  bool in_palette = false;
  int width = 0;
  int height = 0;
  count_ = 0;

  LineReader reader(text, length);
  Line line;
  auto fail = [&](const char* message) {
    *error = message;
    *error_line = reader.number();
    return false;
  };

  while (reader.Next(&line)) {
    const char* p = SkipSpaces(line.begin, line.end);
    if (p == line.end || *p == '#') {
      continue;
    }
    if (const char* q = MatchWord(p, line.end, "palette")) {
      if (SkipSpaces(q, line.end) != line.end) {
        return fail("unexpected text after 'palette'");
      }
      in_palette = true;
      continue;
    }
    if (const char* q = MatchWord(p, line.end, "size")) {
      in_palette = false;
      if (!ParseInt(&q, line.end, &width) || !ParseInt(&q, line.end, &height) || SkipSpaces(q, line.end) != line.end ||
          width < 1 || height < 1 || width > 256 || height > 256) {
        return fail("expected 'size <width> <height>' (1..256)");
      }
      continue;
    }
    if (const char* q = MatchWord(p, line.end, "frame")) {
      in_palette = false;
      q = SkipSpaces(q, line.end);
      const int name_length = static_cast<int>(line.end - q);
      if (name_length < 1 || name_length > kMaxNameLength) {
        return fail("frame name missing or too long");
      }
      if (width == 0) {
        return fail("'frame' before 'size'");
      }
      if (count_ == kMaxFrames) {
        return fail("too many frames");
      }
      Frame& frame = frames_[count_];
      for (int i = 0; i < name_length; ++i) {
        frame.name[i] = q[i];
      }
      frame.name[name_length] = '\0';
      if (Find(frame.name) != nullptr) {
        return fail("duplicate frame name");
      }
      Color* pixels = arena.AllocateArray<Color>(static_cast<size_t>(width) * height);
      if (pixels == nullptr) {
        return fail("out of memory for sprite pixels");
      }
      for (int y = 0; y < height; ++y) {
        if (!reader.Next(&line)) {
          return fail("frame has fewer rows than its size");
        }
        if (line.end - line.begin != width) {
          return fail("frame row length differs from the frame width");
        }
        for (int x = 0; x < width; ++x) {
          const unsigned char c = static_cast<unsigned char>(line.begin[x]);
          if (c >= 128 || !defined[c]) {
            return fail("character not in the palette");
          }
          pixels[y * width + x] = palette[c];
        }
      }
      frame.sprite = {width, height, pixels};
      ++count_;
      continue;
    }
    if (in_palette) {
      // "<char> <rrggbb>"
      const unsigned char c = static_cast<unsigned char>(*p);
      const char* q = SkipSpaces(p + 1, line.end);
      if (c >= 128 || c == '.' || c == ' ' || q == p + 1 || line.end - q != 6) {
        return fail("expected palette entry '<char> <rrggbb>' ('.' is reserved for transparent)");
      }
      Color color = 0;
      for (int i = 0; i < 6; ++i) {
        const int digit = HexDigit(q[i]);
        if (digit < 0) {
          return fail("bad hex colour");
        }
        color = (color << 4) | static_cast<Color>(digit);
      }
      palette[c] = color;
      defined[c] = true;
      continue;
    }
    return fail("unknown directive (expected palette, size or frame)");
  }
  if (count_ == 0) {
    *error = "sprite sheet has no frames";
    *error_line = reader.number();
    return false;
  }
  return true;
}

const Sprite* SpriteSheet::Find(const char* name) const {
  for (int i = 0; i < count_; ++i) {
    const char* a = frames_[i].name;
    const char* b = name;
    while (*a != '\0' && *a == *b) {
      ++a;
      ++b;
    }
    if (*a == '\0' && *b == '\0') {
      return &frames_[i].sprite;
    }
  }
  return nullptr;
}

void DrawSprite(Surface& s, const Sprite& sprite, int x, int y, bool flip_x) {
  const Rect r = ClipRect(s, x, y, sprite.width, sprite.height);
  for (int dy = r.y; dy < r.y + r.height; ++dy) {
    const Color* src = sprite.pixels + (dy - y) * sprite.width;
    Color* dst = s.Row(dy);
    for (int dx = r.x; dx < r.x + r.width; ++dx) {
      const int sx = flip_x ? sprite.width - 1 - (dx - x) : dx - x;
      const Color c = src[sx];
      if ((c & kTransparent) == 0) {
        dst[dx] = c;
      }
    }
  }
}

}  // namespace gfx
