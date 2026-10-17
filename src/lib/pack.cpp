#include "lib/pack.h"

namespace lib {
namespace {

constexpr char kMagic[8] = {'S', 'C', 'R', 'P', 'A', 'K', '0', '1'};
constexpr size_t kHeaderSize = 16;
constexpr size_t kEntrySize = Pack::kNameSize + 8;

uint32_t ReadU32(const uint8_t* p) {
  return p[0] | (uint32_t{p[1]} << 8) | (uint32_t{p[2]} << 16) | (uint32_t{p[3]} << 24);
}

bool NameEquals(const uint8_t* entry_name, const char* name) {
  for (int i = 0; i < Pack::kNameSize; ++i) {
    if (entry_name[i] != static_cast<uint8_t>(name[i])) {
      return false;
    }
    if (name[i] == '\0') {
      return true;
    }
  }
  return false;
}

}  // namespace

bool Pack::Open(const void* data, size_t size, const char** error) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  if (data == nullptr || size < kHeaderSize) {
    *error = "asset pack missing or too small";
    return false;
  }
  for (size_t i = 0; i < sizeof(kMagic); ++i) {
    if (bytes[i] != static_cast<uint8_t>(kMagic[i])) {
      *error = "not an asset pack (bad magic)";
      return false;
    }
  }
  const uint32_t count = ReadU32(bytes + 8);
  if (count > (size - kHeaderSize) / kEntrySize) {
    *error = "asset pack directory truncated";
    return false;
  }
  for (uint32_t i = 0; i < count; ++i) {
    const uint8_t* entry = bytes + kHeaderSize + i * kEntrySize;
    const uint32_t offset = ReadU32(entry + kNameSize);
    const uint32_t blob_size = ReadU32(entry + kNameSize + 4);
    if (entry[kNameSize - 1] != 0 || offset > size || blob_size > size - offset) {
      *error = "asset pack entry out of bounds";
      return false;
    }
  }
  data_ = bytes;
  size_ = size;
  count_ = static_cast<int>(count);
  return true;
}

bool Pack::Find(const char* name, Blob* blob) const {
  for (int i = 0; i < count_; ++i) {
    const uint8_t* entry = data_ + kHeaderSize + i * kEntrySize;
    if (NameEquals(entry, name)) {
      blob->data = reinterpret_cast<const char*>(data_ + ReadU32(entry + kNameSize));
      blob->size = ReadU32(entry + kNameSize + 4);
      return true;
    }
  }
  return false;
}

}  // namespace lib
