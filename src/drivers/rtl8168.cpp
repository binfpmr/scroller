#include "drivers/rtl8168.h"

#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "drivers/mmio.h"
#include "lib/mem.h"

namespace rtl8168 {
namespace {

// Register offsets (names as in Linux r8169).
constexpr uint32_t kMac0 = 0x00, kMar0 = 0x08, kTxDescLow = 0x20, kTxDescHigh = 0x24, kChipCmd = 0x37, kTxPoll = 0x38,
                   kIntrMask = 0x3c, kIntrStatus = 0x3e, kTxConfig = 0x40, kRxConfig = 0x44, kCfg9346 = 0x50,
                   kPhyStatus = 0x6c, kRxMaxSize = 0xda, kCPlusCmd = 0xe0, kRxDescLow = 0xe4, kRxDescHigh = 0xe8,
                   kMaxTxPacketSize = 0xec;

constexpr uint8_t kCmdReset = 0x10, kCmdRxEnable = 0x08, kCmdTxEnable = 0x04;
constexpr uint8_t kTxPollNormal = 0x40;
constexpr uint8_t kCfg9346Unlock = 0xc0, kCfg9346Lock = 0x00;
// Accept broadcast, multicast and our own address; no FIFO threshold, unlimited DMA burst, 128-int, multi-request.
constexpr uint32_t kRxConfigValue = 0x08 | 0x04 | 0x02 | (7u << 8) | (1u << 11) | (1u << 14) | (1u << 15);
constexpr uint32_t kTxConfigValue = (3u << 24) | (7u << 8);  // standard inter-frame gap, unlimited DMA burst
constexpr uint16_t kCPlusRxVlan = 1u << 6, kCPlusRxChecksum = 1u << 5;

constexpr uint32_t kDescOwn = 1u << 31, kDescRingEnd = 1u << 30, kDescFirst = 1u << 29, kDescLast = 1u << 28;
constexpr uint32_t kRxError = 1u << 21;  // error summary
constexpr uint32_t kRxMaxFrame = 1536;

constexpr uint16_t kDeviceIds[] = {0x8168, 0x8161, 0x8169, 0x8136};

}  // namespace

bool Supports(const pci::Device& device) {
  if (device.vendor_id != 0x10ec) {
    return false;
  }
  for (uint16_t id : kDeviceIds) {
    if (device.device_id == id) {
      return true;
    }
  }
  return false;
}

bool Driver::Init(const pci::Device& device, lib::Arena& arena) {
  // BAR0 is I/O ports, BAR2 (64-bit) the same registers as memory.
  const uint64_t bar = pci::MemoryBar(device, 2);
  if (bar == 0 || (!x86::IsMapped(bar) && !x86::MapUncached(arena, bar, 0x1000))) {
    Log("rtl8168: BAR2 unusable\n");
    return false;
  }
  pci::EnableDevice(device);
  mmio_ = reinterpret_cast<volatile uint8_t*>(bar);

  mmio::Write<uint16_t>(mmio_, kIntrMask, 0);
  mmio::Write<uint8_t>(mmio_, kChipCmd, kCmdReset);
  for (int i = 0; i < 1000 && (mmio::Read<uint8_t>(mmio_, kChipCmd) & kCmdReset) != 0; ++i) {
    mmio::DelayUs(100);
  }
  mmio::Write<uint16_t>(mmio_, kIntrStatus, 0xffff);
  for (int i = 0; i < 6; ++i) {
    mac_.b[i] = mmio::Read<uint8_t>(mmio_, kMac0 + i);
  }

  rx_ = static_cast<volatile Descriptor*>(arena.Allocate(sizeof(Descriptor) * nic::kRxRing, 256));
  tx_ = static_cast<volatile Descriptor*>(arena.Allocate(sizeof(Descriptor) * nic::kTxRing, 256));
  rx_buffers_ = static_cast<uint8_t*>(arena.Allocate(nic::kBufferSize * nic::kRxRing, 4096));
  tx_buffers_ = static_cast<uint8_t*>(arena.Allocate(nic::kBufferSize * nic::kTxRing, 4096));
  if (rx_ == nullptr || tx_ == nullptr || rx_buffers_ == nullptr || tx_buffers_ == nullptr) {
    Log("rtl8168: out of memory\n");
    return false;
  }
  for (int i = 0; i < nic::kRxRing; ++i) {
    rx_[i].address = reinterpret_cast<uint64_t>(rx_buffers_ + i * nic::kBufferSize);
    rx_[i].opts2 = 0;
    GiveRx(i);
  }
  for (int i = 0; i < nic::kTxRing; ++i) {
    tx_[i].address = reinterpret_cast<uint64_t>(tx_buffers_ + i * nic::kBufferSize);
    tx_[i].opts2 = 0;
    tx_[i].opts1 = i == nic::kTxRing - 1 ? kDescRingEnd : 0;
  }

  mmio::Write<uint8_t>(mmio_, kCfg9346, kCfg9346Unlock);
  const uint16_t cplus = mmio::Read<uint16_t>(mmio_, kCPlusCmd);
  mmio::Write<uint16_t>(mmio_, kCPlusCmd, cplus & ~(kCPlusRxVlan | kCPlusRxChecksum));  // frames exactly as received
  mmio::Write<uint16_t>(mmio_, kRxMaxSize, kRxMaxFrame);
  mmio::Write<uint8_t>(mmio_, kMaxTxPacketSize, 0x3f);  // in 128-byte units
  const uint64_t tx_address = reinterpret_cast<uint64_t>(tx_);
  const uint64_t rx_address = reinterpret_cast<uint64_t>(rx_);
  mmio::Write<uint32_t>(mmio_, kTxDescHigh, static_cast<uint32_t>(tx_address >> 32));
  mmio::Write<uint32_t>(mmio_, kTxDescLow, static_cast<uint32_t>(tx_address));
  mmio::Write<uint32_t>(mmio_, kRxDescHigh, static_cast<uint32_t>(rx_address >> 32));
  mmio::Write<uint32_t>(mmio_, kRxDescLow, static_cast<uint32_t>(rx_address));
  mmio::Write<uint8_t>(mmio_, kChipCmd, kCmdRxEnable | kCmdTxEnable);
  mmio::Write<uint32_t>(mmio_, kRxConfig, kRxConfigValue);
  mmio::Write<uint32_t>(mmio_, kTxConfig, kTxConfigValue);
  mmio::Write<uint32_t>(mmio_, kMar0, 0);  // no multicast groups
  mmio::Write<uint32_t>(mmio_, kMar0 + 4, 0);
  mmio::Write<uint8_t>(mmio_, kCfg9346, kCfg9346Lock);

  const uint32_t version = mmio::Read<uint32_t>(mmio_, kTxConfig);  // chip version in bits 30:20 (read-only)
  Log("rtl8168: %04x:%04x version %03x MAC %02x:%02x:%02x:%02x:%02x:%02x PHY %02x\n", device.vendor_id,
      device.device_id, (version >> 20) & 0x7cf, mac_.b[0], mac_.b[1], mac_.b[2], mac_.b[3], mac_.b[4], mac_.b[5],
      mmio::Read<uint8_t>(mmio_, kPhyStatus));
  return true;
}

void Driver::GiveRx(int index) {
  rx_[index].opts1 = kDescOwn | (index == nic::kRxRing - 1 ? kDescRingEnd : 0) | nic::kBufferSize;
}

bool Driver::Send(const uint8_t* frame, size_t length) {
  volatile Descriptor& d = tx_[tx_next_];
  if (length > nic::kBufferSize || (d.opts1 & kDescOwn) != 0) {
    return false;  // ring full
  }
  lib::Copy(tx_buffers_ + tx_next_ * nic::kBufferSize, frame, length);
  asm volatile("" ::: "memory");
  d.opts1 = kDescOwn | kDescFirst | kDescLast | (tx_next_ == nic::kTxRing - 1 ? kDescRingEnd : 0) |
            static_cast<uint32_t>(length);
  tx_next_ = (tx_next_ + 1) % nic::kTxRing;
  asm volatile("" ::: "memory");
  mmio::Write<uint8_t>(mmio_, kTxPoll, kTxPollNormal);
  return true;
}

size_t Driver::Receive(const uint8_t** frame) {
  if (rx_held_ >= 0) {
    GiveRx(rx_held_);
    rx_held_ = -1;
  }
  for (;;) {
    const int index = rx_next_;
    const uint32_t opts1 = rx_[index].opts1;
    if ((opts1 & kDescOwn) != 0) {
      return 0;
    }
    asm volatile("" ::: "memory");  // descriptor before buffer
    rx_next_ = (rx_next_ + 1) % nic::kRxRing;
    const size_t length = opts1 & 0x3fff;
    if ((opts1 & (kDescFirst | kDescLast)) != (kDescFirst | kDescLast) || (opts1 & kRxError) != 0 || length <= 4) {
      GiveRx(index);  // error or a frame spread over several buffers (not expected): skip
      continue;
    }
    rx_held_ = index;
    *frame = rx_buffers_ + index * nic::kBufferSize;
    return length - 4;  // without the FCS
  }
}

int Driver::LinkSpeed() {
  const uint8_t phy = mmio::Read<uint8_t>(mmio_, kPhyStatus);
  if ((phy & 0x02) == 0) {
    return 0;
  }
  return (phy & 0x10) != 0 ? 1000 : (phy & 0x08) != 0 ? 100 : 10;
}

}  // namespace rtl8168
