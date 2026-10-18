#include "lib/pe.h"

#include "lib/mem.h"

namespace lib {
namespace {

constexpr uint16_t kMachineAmd64 = 0x8664;
constexpr uint16_t kPe32Plus = 0x20b;
constexpr size_t kSectionHeader = 40;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }
uint32_t Get32(const uint8_t* p) { return Get16(p) | static_cast<uint32_t>(Get16(p + 2)) << 16; }

}  // namespace

bool ParsePe(const uint8_t* file, size_t size, PeImage* image, const char** error) {
  if (size < 0x40 || file[0] != 'M' || file[1] != 'Z') {
    *error = "not an MZ/PE file";
    return false;
  }
  const uint32_t pe = Get32(file + 0x3c);
  if (pe > size - 24 || Get32(file + pe) != 0x00004550) {  // "PE\0\0"
    *error = "no PE header";
    return false;
  }
  const uint8_t* coff = file + pe + 4;
  const uint16_t sections = Get16(coff + 2);
  const uint16_t optional_size = Get16(coff + 16);
  const uint8_t* opt = coff + 20;
  if (Get16(coff) != kMachineAmd64 || optional_size < 64 ||
      static_cast<size_t>(opt - file) + optional_size + sections * kSectionHeader > size || Get16(opt) != kPe32Plus) {
    *error = "not a PE32+ x86-64 image";
    return false;
  }
  image->entry_rva = Get32(opt + 16);
  image->size_of_image = Get32(opt + 56);
  const uint32_t headers = Get32(opt + 60);
  if (headers > size || headers > image->size_of_image || image->entry_rva >= image->size_of_image) {
    *error = "bad image layout";
    return false;
  }
  const uint8_t* table = opt + optional_size;
  for (int i = 0; i < sections; ++i) {
    const uint8_t* s = table + i * kSectionHeader;
    const uint32_t virtual_size = Get32(s + 8), rva = Get32(s + 12), raw_size = Get32(s + 16), raw = Get32(s + 20);
    const uint32_t copied = raw_size < virtual_size ? raw_size : virtual_size;
    if (rva > image->size_of_image || virtual_size > image->size_of_image - rva || raw > size || copied > size - raw) {
      *error = "section outside the image or file";
      return false;
    }
  }
  return true;
}

bool LoadPe(const uint8_t* file, size_t size, uint8_t* dest, size_t dest_size, PeImage* image, const char** error) {
  if (!ParsePe(file, size, image, error)) {
    return false;
  }
  if (image->size_of_image > dest_size) {
    *error = "image larger than the slot";
    return false;
  }
  const uint32_t pe = Get32(file + 0x3c);
  const uint8_t* coff = file + pe + 4;
  const uint8_t* opt = coff + 20;
  const uint8_t* table = opt + Get16(coff + 16);
  Fill(dest, 0, image->size_of_image);
  Copy(dest, file, Get32(opt + 60));
  for (int i = 0; i < Get16(coff + 2); ++i) {
    const uint8_t* s = table + i * kSectionHeader;
    const uint32_t virtual_size = Get32(s + 8), rva = Get32(s + 12), raw_size = Get32(s + 16), raw = Get32(s + 20);
    Copy(dest + rva, file + raw, raw_size < virtual_size ? raw_size : virtual_size);
  }
  return true;
}

uint32_t Crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
    }
  }
  return ~crc;
}

}  // namespace lib
