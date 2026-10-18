#include "lib/pe.h"

#include <cstring>
#include <vector>

#include "test.h"

namespace {

void Put16(std::vector<uint8_t>& f, size_t at, uint16_t v) { std::memcpy(f.data() + at, &v, 2); }
void Put32(std::vector<uint8_t>& f, size_t at, uint32_t v) { std::memcpy(f.data() + at, &v, 4); }

// A minimal PE32+: headers in the first 0x200 bytes, .text (RVA 0x1000, 0x10 bytes raw at 0x200, virtual 0x20) and
// .data (RVA 0x2000, raw 0x10 at 0x210, virtual 0x1000: zero-filled tail). SizeOfImage 0x3000, entry 0x1004.
std::vector<uint8_t> MakePe() {
  std::vector<uint8_t> f(0x220, 0);
  f[0] = 'M';
  f[1] = 'Z';
  Put32(f, 0x3c, 0x80);
  Put32(f, 0x80, 0x00004550);
  const size_t coff = 0x84;
  Put16(f, coff, 0x8664);
  Put16(f, coff + 2, 2);
  Put16(f, coff + 16, 240);
  const size_t opt = coff + 20;
  Put16(f, opt, 0x20b);
  Put32(f, opt + 16, 0x1004);
  Put32(f, opt + 56, 0x3000);
  Put32(f, opt + 60, 0x200);
  const size_t table = opt + 240;
  std::memcpy(f.data() + table, ".text", 5);
  Put32(f, table + 8, 0x20);
  Put32(f, table + 12, 0x1000);
  Put32(f, table + 16, 0x10);
  Put32(f, table + 20, 0x200);
  std::memcpy(f.data() + table + 40, ".data", 5);
  Put32(f, table + 48, 0x1000);
  Put32(f, table + 52, 0x2000);
  Put32(f, table + 56, 0x10);
  Put32(f, table + 60, 0x210);
  for (int i = 0; i < 0x20; ++i) {
    f[0x200 + i] = static_cast<uint8_t>(0xa0 + i);
  }
  return f;
}

}  // namespace

TEST(LoadsSectionsAtTheirRvas) {
  const std::vector<uint8_t> file = MakePe();
  std::vector<uint8_t> dest(0x4000, 0xee);
  lib::PeImage image;
  const char* error = nullptr;
  EXPECT_TRUE(lib::LoadPe(file.data(), file.size(), dest.data(), dest.size(), &image, &error));
  EXPECT_EQ(image.size_of_image, 0x3000u);
  EXPECT_EQ(image.entry_rva, 0x1004u);
  EXPECT_TRUE(dest[0] == 'M' && dest[1] == 'Z');
  EXPECT_EQ(dest[0x1000], 0xa0);
  EXPECT_EQ(dest[0x100f], 0xaf);
  EXPECT_EQ(dest[0x1010], 0);  // virtual size beyond the raw data: zero
  EXPECT_EQ(dest[0x2000], 0xb0);
  EXPECT_EQ(dest[0x2fff], 0);
  EXPECT_EQ(dest[0x3000], 0xee);  // nothing written past SizeOfImage
}

TEST(RejectsBrokenImages) {
  lib::PeImage image;
  const char* error = nullptr;
  std::vector<uint8_t> file = MakePe();
  std::vector<uint8_t> small(0x1000);
  EXPECT_TRUE(!lib::LoadPe(file.data(), file.size(), small.data(), small.size(), &image, &error));  // slot too small
  file[0] = 'X';
  EXPECT_TRUE(!lib::ParsePe(file.data(), file.size(), &image, &error));
  file = MakePe();
  Put32(file, 0x84 + 20 + 240 + 60, 0x10000);  // .data raw data past the end of the file
  EXPECT_TRUE(!lib::ParsePe(file.data(), file.size(), &image, &error));
  file = MakePe();
  EXPECT_TRUE(!lib::ParsePe(file.data(), 0x100, &image, &error));  // truncated
}

TEST(Crc32MatchesZlib) {
  const char* text = "123456789";
  EXPECT_EQ(lib::Crc32(reinterpret_cast<const uint8_t*>(text), 9), 0xcbf43926u);
  EXPECT_EQ(lib::Crc32(nullptr, 0), 0u);
}
