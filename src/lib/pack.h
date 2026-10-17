// Asset pack: one file holding named blobs, built by tools/pack_assets.py and loaded from the boot volume.
//
// Layout (little endian):
//   "SCRPAK01"  u32 count  u32 reserved
//   count x { char name[56] (NUL-terminated, e.g. "levels/test.txt"); u32 offset; u32 size }  (offset from file start)
//   blob data
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace lib {

struct Blob {
  const char* data = nullptr;
  size_t size = 0;
};

class Pack {
 public:
  static constexpr int kNameSize = 56;

  // Validates the header and that every entry lies inside the data. On error returns false with a static message.
  bool Open(const void* data, size_t size, const char** error);
  // Looks up a blob by name.
  bool Find(const char* name, Blob* blob) const;
  int count() const { return count_; }

 private:
  const uint8_t* data_ = nullptr;
  size_t size_ = 0;
  int count_ = 0;
};

}  // namespace lib
