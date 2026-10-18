#include <cstdint>

#include "drivers/hid_keyboard.h"
#include "drivers/usb_descriptors.h"
#include "test.h"

using platform::Key;

namespace {

// A typical keyboard: interface 0 boot keyboard (endpoint 0x81, 8 bytes, 10 ms), interface 1 media keys (0x82).
constexpr uint8_t kKeyboardConfig[] = {
    9, 2,    59,   0,    2, 1, 0,    0xa0, 50,  // configuration: total 59, 2 interfaces, value 1
    9, 4,    0,    0,    1, 3, 1,    1,    0,   // interface 0: HID, boot, keyboard
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, 65,   0,   // HID descriptor
    7, 5,    0x81, 3,    8, 0, 10,              // endpoint 0x81 interrupt, 8 bytes, 10 ms
    9, 4,    1,    0,    1, 3, 0,    0,    0,   // interface 1: HID, no boot protocol
    9, 0x21, 0x11, 0x01, 0, 1, 0x22, 50,   0,  7, 5, 0x82, 3, 4, 0, 10,
};

}  // namespace

TEST(ParsesDeviceDescriptor) {
  const uint8_t d[18] = {18, 1, 0x10, 0x01, 0, 0, 0, 8, 0x6d, 0x04, 0x1c, 0xc3, 0x00, 0x01, 1, 2, 0, 1};
  usb::DeviceInfo info;
  EXPECT_TRUE(usb::ParseDevice(d, sizeof(d), &info));
  EXPECT_EQ(info.usb_version, 0x0110);
  EXPECT_EQ(info.max_packet0, 8);
  EXPECT_EQ(info.vendor_id, 0x046d);
  EXPECT_EQ(info.product_id, 0xc31c);
  EXPECT_EQ(info.configurations, 1);
  EXPECT_TRUE(!usb::ParseDevice(d, 8, &info));
}

TEST(ParsesConfigurationAndFindsBootKeyboard) {
  usb::Configuration config;
  EXPECT_TRUE(usb::ParseConfiguration(kKeyboardConfig, 9, &config));  // header only: tells the full length
  EXPECT_EQ(config.total_length, 59);
  EXPECT_EQ(config.interface_count, 0);

  EXPECT_TRUE(usb::ParseConfiguration(kKeyboardConfig, sizeof(kKeyboardConfig), &config));
  EXPECT_EQ(config.value, 1);
  EXPECT_EQ(config.interface_count, 2);
  EXPECT_EQ(config.interfaces[1].endpoints[0].address, 0x82);
  const usb::Interface* interface = nullptr;
  const usb::Endpoint* ep = usb::FindBootKeyboard(config, &interface);
  EXPECT_TRUE(ep != nullptr && interface != nullptr);
  EXPECT_EQ(ep->address, 0x81);
  EXPECT_EQ(ep->max_packet, 8);
  EXPECT_EQ(ep->interval, 10);
  EXPECT_EQ(interface->number, 0);

  // Truncated in the middle of a descriptor: what is complete is kept.
  EXPECT_TRUE(usb::ParseConfiguration(kKeyboardConfig, 30, &config));
  EXPECT_EQ(config.interface_count, 1);
  EXPECT_EQ(config.interfaces[0].endpoint_count, 0);
}

TEST(BootKeyboardReportsBecomeKeyEvents) {
  hid::BootKeyboard keyboard;
  platform::InputTracker tracker;
  const uint8_t right[8] = {0, 0, 0x4f, 0, 0, 0, 0, 0};
  EXPECT_EQ(keyboard.Feed(right, tracker), 0x4f);
  platform::InputState s = tracker.NextTick();
  EXPECT_TRUE(s.Pressed(Key::kRight) && s.Held(Key::kRight));

  const uint8_t right_space[8] = {0, 0, 0x4f, 0x2c, 0, 0, 0, 0};
  keyboard.Feed(right_space, tracker);
  s = tracker.NextTick();
  EXPECT_TRUE(s.Pressed(Key::kJump) && s.Held(Key::kRight) && !s.Pressed(Key::kRight));

  const uint8_t space[8] = {0, 0, 0x2c, 0, 0, 0, 0, 0};  // right released, space moved to the first slot
  keyboard.Feed(space, tracker);
  s = tracker.NextTick();
  EXPECT_TRUE(s.Released(Key::kRight) && s.Held(Key::kJump) && !s.Pressed(Key::kJump));

  const uint8_t phantom[8] = {0, 0, 1, 1, 1, 1, 1, 1};  // too many keys: no change
  keyboard.Feed(phantom, tracker);
  s = tracker.NextTick();
  EXPECT_TRUE(s.Held(Key::kJump) && s.released == 0);

  const uint8_t none[8] = {};
  keyboard.Feed(none, tracker);
  s = tracker.NextTick();
  EXPECT_TRUE(s.Released(Key::kJump) && s.held == 0);
}

TEST(CtrlAltDeleteFromModifierBits) {
  hid::BootKeyboard keyboard;
  platform::InputTracker tracker;
  const uint8_t ctrl_alt[8] = {0x01 | 0x40, 0, 0, 0, 0, 0, 0, 0};  // left Ctrl, right Alt
  keyboard.Feed(ctrl_alt, tracker);
  EXPECT_TRUE(!tracker.NextTick().RebootRequested());
  const uint8_t ctrl_alt_del[8] = {0x01 | 0x40, 0, 0x4c, 0, 0, 0, 0, 0};
  keyboard.Feed(ctrl_alt_del, tracker);
  EXPECT_TRUE(tracker.NextTick().RebootRequested());
  const uint8_t del_only[8] = {0, 0, 0x4c, 0, 0, 0, 0, 0};
  keyboard.Feed(del_only, tracker);
  platform::InputState s = tracker.NextTick();
  EXPECT_TRUE(!s.Held(Key::kCtrl) && !s.Held(Key::kAlt));
}
