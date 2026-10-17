#include "lib/pack.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "test.h"

namespace {

// Builds a pack with the real tool from a temporary directory.
std::string BuildPack(const std::string& dir) {
  const std::string out = dir + "/out.pak";
  const std::string cmd =
      "python3 " + std::string(SCROLLER_SOURCE_DIR) + "/tools/pack_assets.py " + dir + "/assets " + out;
  if (std::system(cmd.c_str()) != 0) {
    test::Fail(__FILE__, __LINE__, "pack_assets.py failed");
  }
  std::ifstream file(out, std::ios::binary);
  std::stringstream data;
  data << file.rdbuf();
  return data.str();
}

}  // namespace

TEST(PackToolRoundTrip) {
  char dir_template[] = "/tmp/scroller_pack_XXXXXX";
  const std::string dir = mkdtemp(dir_template);
  std::system(("mkdir -p " + dir + "/assets/levels").c_str());
  std::ofstream(dir + "/assets/levels/a.txt") << "P#";
  std::ofstream(dir + "/assets/b.txt") << "hello world";
  const std::string pack_data = BuildPack(dir);
  std::system(("rm -rf " + dir).c_str());

  lib::Pack pack;
  const char* error = nullptr;
  EXPECT_TRUE(pack.Open(pack_data.data(), pack_data.size(), &error));
  EXPECT_EQ(pack.count(), 2);
  lib::Blob blob;
  EXPECT_TRUE(pack.Find("levels/a.txt", &blob));
  EXPECT_EQ(std::string(blob.data, blob.size), "P#");
  EXPECT_TRUE(pack.Find("b.txt", &blob));
  EXPECT_EQ(std::string(blob.data, blob.size), "hello world");
  EXPECT_EQ(static_cast<size_t>(blob.data - pack_data.data()) % 16, 0u);  // blobs are 16-byte aligned
  EXPECT_TRUE(!pack.Find("levels/a", &blob));
  EXPECT_TRUE(!pack.Find("missing.txt", &blob));
}

TEST(PackRejectsCorruptData) {
  lib::Pack pack;
  const char* error = nullptr;
  EXPECT_TRUE(!pack.Open(nullptr, 0, &error));
  std::string bad = "SCRPAK02";
  bad.resize(16, '\0');
  EXPECT_TRUE(!pack.Open(bad.data(), bad.size(), &error));
  std::string truncated = "SCRPAK01";
  truncated += std::string("\x05\0\0\0\0\0\0\0", 8);  // 5 entries, no directory
  EXPECT_TRUE(!pack.Open(truncated.data(), truncated.size(), &error));
  EXPECT_STREQ(error, "asset pack directory truncated");
  std::string out_of_bounds = "SCRPAK01";
  out_of_bounds += std::string("\x01\0\0\0\0\0\0\0", 8);
  out_of_bounds += std::string(56, '\0').replace(0, 1, "x");
  out_of_bounds += std::string("\x00\x10\0\0\x10\0\0\0", 8);  // offset 4096, size 16: beyond the end
  EXPECT_TRUE(!pack.Open(out_of_bounds.data(), out_of_bounds.size(), &error));
  EXPECT_STREQ(error, "asset pack entry out of bounds");
}
