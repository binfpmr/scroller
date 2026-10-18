#include "drivers/usb_descriptors.h"

namespace usb {
namespace {

constexpr uint8_t kTypeDevice = 1, kTypeConfiguration = 2, kTypeInterface = 4, kTypeEndpoint = 5;

uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | p[1] << 8); }

}  // namespace

bool ParseDevice(const uint8_t* data, size_t length, DeviceInfo* info) {
  if (length < 18 || data[0] < 18 || data[1] != kTypeDevice) {
    return false;
  }
  *info = {.usb_version = Get16(data + 2),
           .device_class = data[4],
           .max_packet0 = data[7],
           .vendor_id = Get16(data + 8),
           .product_id = Get16(data + 10),
           .configurations = data[17]};
  return true;
}

bool ParseConfiguration(const uint8_t* data, size_t length, Configuration* config) {
  if (length < 9 || data[0] < 9 || data[1] != kTypeConfiguration) {
    return false;
  }
  *config = {};
  config->total_length = Get16(data + 2);
  config->value = data[5];
  const size_t end = length < config->total_length ? length : config->total_length;
  Interface* current = nullptr;
  for (size_t at = data[0]; at + 2 <= end;) {
    const uint8_t size = data[at];
    if (size < 2 || at + size > end) {
      break;
    }
    const uint8_t* d = data + at;
    if (d[1] == kTypeInterface && size >= 9) {
      current = nullptr;
      if (config->interface_count < static_cast<int>(sizeof(config->interfaces) / sizeof(config->interfaces[0]))) {
        current = &config->interfaces[config->interface_count++];
        *current = Interface();
        current->number = d[2];
        current->alternate = d[3];
        current->interface_class = d[5];
        current->subclass = d[6];
        current->protocol = d[7];
      }
    } else if (d[1] == kTypeEndpoint && size >= 7 && current != nullptr &&
               current->endpoint_count < static_cast<int>(sizeof(current->endpoints) / sizeof(current->endpoints[0]))) {
      current->endpoints[current->endpoint_count++] = {.address = d[2],
                                                       .attributes = d[3],
                                                       .max_packet = static_cast<uint16_t>(Get16(d + 4) & 0x7ff),
                                                       .interval = d[6]};
    }
    at += size;
  }
  return true;
}

const Endpoint* FindBootKeyboard(const Configuration& config, const Interface** interface) {
  for (int i = 0; i < config.interface_count; ++i) {
    const Interface& in = config.interfaces[i];
    if (in.interface_class != 3 || in.subclass != 1 || in.protocol != 1 || in.alternate != 0) {
      continue;
    }
    for (int e = 0; e < in.endpoint_count; ++e) {
      if ((in.endpoints[e].address & 0x80) != 0 && (in.endpoints[e].attributes & 3) == 3) {
        *interface = &in;
        return &in.endpoints[e];
      }
    }
  }
  return nullptr;
}

}  // namespace usb
