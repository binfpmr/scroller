// USB descriptor parsing (pure logic, host-tested): the device descriptor's interesting fields and a
// configuration descriptor's interfaces with their endpoints.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace usb {

struct DeviceInfo {
  uint16_t usb_version = 0;  // bcdUSB
  uint8_t device_class = 0;
  uint8_t max_packet0 = 0;
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;
  uint8_t configurations = 0;
};

// False if the 18 bytes are not a device descriptor.
bool ParseDevice(const uint8_t* data, size_t length, DeviceInfo* info);

struct Endpoint {
  uint8_t address = 0;     // bit 7: IN
  uint8_t attributes = 0;  // bits 1:0: 0 control, 1 isochronous, 2 bulk, 3 interrupt
  uint16_t max_packet = 0;
  uint8_t interval = 0;
};

struct Interface {
  uint8_t number = 0;
  uint8_t alternate = 0;
  uint8_t interface_class = 0;  // 3 = HID, 8 = mass storage, 9 = hub
  uint8_t subclass = 0;         // HID: 1 = boot interface
  uint8_t protocol = 0;         // HID boot: 1 = keyboard, 2 = mouse
  int endpoint_count = 0;
  Endpoint endpoints[4];
};

struct Configuration {
  uint8_t value = 0;  // for SET_CONFIGURATION
  uint16_t total_length = 0;
  int interface_count = 0;
  Interface interfaces[8];
};

// Parses as much of the configuration descriptor (with its interface and endpoint descriptors) as `length` holds;
// with only the first 9 bytes, total_length tells how much to read. Interfaces and endpoints beyond the array sizes
// are skipped. False if it is not a configuration descriptor.
bool ParseConfiguration(const uint8_t* data, size_t length, Configuration* config);

// The first interrupt IN endpoint of a HID boot keyboard interface, or null.
const Endpoint* FindBootKeyboard(const Configuration& config, const Interface** interface);

}  // namespace usb
