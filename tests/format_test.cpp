#include "lib/format.h"

#include <string>

#include "test.h"

namespace {

template <typename... Args>
std::string Fmt(const char* fmt, Args... args) {
  char buf[128];
  lib::Format(buf, sizeof(buf), fmt, args...);
  return buf;
}

}  // namespace

TEST(PlainText) { EXPECT_EQ(Fmt("hello"), "hello"); }

TEST(SignedDecimal) {
  EXPECT_EQ(Fmt("%d", 0), "0");
  EXPECT_EQ(Fmt("%d", -42), "-42");
  EXPECT_EQ(Fmt("%i", 2147483647), "2147483647");
  EXPECT_EQ(Fmt("%d", -2147483647 - 1), "-2147483648");
  EXPECT_EQ(Fmt("%lld", -9223372036854775807ll - 1), "-9223372036854775808");
  EXPECT_EQ(Fmt("%ld", -5l), "-5");
}

TEST(UnsignedAndHex) {
  EXPECT_EQ(Fmt("%u", 4294967295u), "4294967295");
  EXPECT_EQ(Fmt("%x", 0xbeefu), "beef");
  EXPECT_EQ(Fmt("%X", 0xbeefu), "BEEF");
  EXPECT_EQ(Fmt("%llx", 0xffffffffffffffffull), "ffffffffffffffff");
  EXPECT_EQ(Fmt("%zu", static_cast<size_t>(123)), "123");
}

TEST(WidthAndFlags) {
  EXPECT_EQ(Fmt("%5d|", 42), "   42|");
  EXPECT_EQ(Fmt("%-5d|", 42), "42   |");
  EXPECT_EQ(Fmt("%05d|", -42), "-0042|");
  EXPECT_EQ(Fmt("%08x", 0xabcu), "00000abc");
  EXPECT_EQ(Fmt("%6s|", "ab"), "    ab|");
  EXPECT_EQ(Fmt("%-6s|", "ab"), "ab    |");
}

TEST(PointerStringCharPercent) {
  EXPECT_EQ(Fmt("%p", reinterpret_cast<void*>(0x1234)), "0x0000000000001234");
  EXPECT_EQ(Fmt("%s", static_cast<const char*>(nullptr)), "(null)");
  EXPECT_EQ(Fmt("%c%c", 'o', 'k'), "ok");
  EXPECT_EQ(Fmt("100%%"), "100%");
}

TEST(TruncatesAndReturnsFullLength) {
  char buf[6];
  EXPECT_EQ(lib::Format(buf, sizeof(buf), "%s", "abcdefgh"), 8u);
  EXPECT_EQ(std::string(buf), "abcde");
  EXPECT_EQ(lib::Format(buf, sizeof(buf), "%d", 12345), 5u);
  EXPECT_EQ(std::string(buf), "12345");
}

TEST(ZeroSizeBufferIsUntouched) {
  char c = 'x';
  EXPECT_EQ(lib::Format(&c, 0, "abc"), 3u);
  EXPECT_EQ(c, 'x');
}
