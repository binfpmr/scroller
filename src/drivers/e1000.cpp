#include "drivers/e1000.h"

#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "drivers/mmio.h"
#include "lib/mem.h"

namespace e1000 {
namespace {

constexpr uint32_t kCtrl = 0x0000, kStatus = 0x0008, kEerd = 0x0014, kImc = 0x00d8, kRctl = 0x0100, kTctl = 0x0400,
                   kTipg = 0x0410;
constexpr uint32_t kRdbal = 0x2800, kRdbah = 0x2804, kRdlen = 0x2808, kRdh = 0x2810, kRdt = 0x2818;
constexpr uint32_t kTdbal = 0x3800, kTdbah = 0x3804, kTdlen = 0x3808, kTdh = 0x3810, kTdt = 0x3818;
constexpr uint32_t kMta = 0x5200, kRal = 0x5400, kRah = 0x5404;

constexpr uint32_t kCtrlSlu = 1u << 6, kCtrlAsde = 1u << 5, kCtrlRst = 1u << 26;
constexpr uint32_t kRctlEn = 1u << 1, kRctlBam = 1u << 15, kRctlSecrc = 1u << 26;  // buffer size 00 = 2048
constexpr uint32_t kTctlEn = 1u << 1, kTctlPsp = 1u << 3;
constexpr uint8_t kStatusDd = 1u << 0, kStatusEop = 1u << 1;
constexpr uint8_t kCmdEop = 1u << 0, kCmdIfcs = 1u << 1, kCmdRs = 1u << 3;

constexpr uint16_t kDeviceIds[] = {
    0x100e,  // 82540EM (QEMU "e1000")
    0x100f,  // 82545EM
    0x1004,  // 82543GC
    0x1015,  // 82540EM LOM
    0x1026,  // 82545GM
    0x107c,  // 82541PI
};

uint32_t Read(volatile uint8_t* m, uint32_t offset) { return mmio::Read<uint32_t>(m, offset); }
void Write(volatile uint8_t* m, uint32_t offset, uint32_t value) { mmio::Write<uint32_t>(m, offset, value); }

}  // namespace

bool Supports(const pci::Device& device) {
  if (device.vendor_id != 0x8086) {
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
  const uint64_t bar = pci::MemoryBar(device, 0);
  if (bar == 0 || (!x86::IsMapped(bar) && !x86::MapUncached(arena, bar, 0x20000))) {
    Log("e1000: BAR0 unusable\n");
    return false;
  }
  pci::EnableDevice(device);
  mmio_ = reinterpret_cast<volatile uint8_t*>(bar);

  Write(mmio_, kImc, 0xffffffff);  // polled: no interrupts
  Write(mmio_, kCtrl, Read(mmio_, kCtrl) | kCtrlRst);
  mmio::DelayUs(1000);
  for (int i = 0; i < 1000 && (Read(mmio_, kCtrl) & kCtrlRst) != 0; ++i) {
    mmio::DelayUs(10);
  }
  Write(mmio_, kImc, 0xffffffff);
  Write(mmio_, kCtrl, (Read(mmio_, kCtrl) | kCtrlSlu | kCtrlAsde));

  // The reset loads the MAC from the EEPROM into receive address 0; read the EEPROM directly if it did not.
  uint32_t ral = Read(mmio_, kRal);
  uint32_t rah = Read(mmio_, kRah);
  if ((rah & (1u << 31)) == 0) {
    uint16_t words[3] = {};
    for (uint32_t w = 0; w < 3; ++w) {
      Write(mmio_, kEerd, 1u | (w << 8));
      uint32_t v = 0;
      for (int i = 0; i < 10000 && ((v = Read(mmio_, kEerd)) & (1u << 4)) == 0; ++i) {
        mmio::DelayUs(1);
      }
      words[w] = static_cast<uint16_t>(v >> 16);
    }
    ral = words[0] | uint32_t{words[1]} << 16;
    rah = words[2] | (1u << 31);
    Write(mmio_, kRal, ral);
    Write(mmio_, kRah, rah);
  }
  for (int i = 0; i < 4; ++i) {
    mac_.b[i] = static_cast<uint8_t>(ral >> (8 * i));
  }
  mac_.b[4] = static_cast<uint8_t>(rah);
  mac_.b[5] = static_cast<uint8_t>(rah >> 8);
  for (uint32_t i = 0; i < 128; ++i) {
    Write(mmio_, kMta + 4 * i, 0);
  }

  // Rings (128-byte aligned in practice; the hardware needs 16) and buffers, identity-mapped arena memory.
  rx_ = static_cast<volatile Descriptor*>(arena.Allocate(sizeof(Descriptor) * nic::kRxRing, 128));
  tx_ = static_cast<volatile Descriptor*>(arena.Allocate(sizeof(Descriptor) * nic::kTxRing, 128));
  rx_buffers_ = static_cast<uint8_t*>(arena.Allocate(nic::kBufferSize * nic::kRxRing, 4096));
  tx_buffers_ = static_cast<uint8_t*>(arena.Allocate(nic::kBufferSize * nic::kTxRing, 4096));
  if (rx_ == nullptr || tx_ == nullptr || rx_buffers_ == nullptr || tx_buffers_ == nullptr) {
    Log("e1000: out of memory\n");
    return false;
  }
  lib::Fill(const_cast<Descriptor*>(rx_), 0, sizeof(Descriptor) * nic::kRxRing);
  lib::Fill(const_cast<Descriptor*>(tx_), 0, sizeof(Descriptor) * nic::kTxRing);
  for (int i = 0; i < nic::kRxRing; ++i) {
    rx_[i].address = reinterpret_cast<uint64_t>(rx_buffers_ + i * nic::kBufferSize);
  }
  for (int i = 0; i < nic::kTxRing; ++i) {
    tx_[i].address = reinterpret_cast<uint64_t>(tx_buffers_ + i * nic::kBufferSize);
    tx_[i].status = kStatusDd;  // free
  }

  const uint64_t rx_address = reinterpret_cast<uint64_t>(rx_);
  Write(mmio_, kRdbal, static_cast<uint32_t>(rx_address));
  Write(mmio_, kRdbah, static_cast<uint32_t>(rx_address >> 32));
  Write(mmio_, kRdlen, sizeof(Descriptor) * nic::kRxRing);
  Write(mmio_, kRdh, 0);
  Write(mmio_, kRdt, nic::kRxRing - 1);  // all but one descriptor belong to the hardware
  Write(mmio_, kRctl, kRctlEn | kRctlBam | kRctlSecrc);

  const uint64_t tx_address = reinterpret_cast<uint64_t>(tx_);
  Write(mmio_, kTdbal, static_cast<uint32_t>(tx_address));
  Write(mmio_, kTdbah, static_cast<uint32_t>(tx_address >> 32));
  Write(mmio_, kTdlen, sizeof(Descriptor) * nic::kTxRing);
  Write(mmio_, kTdh, 0);
  Write(mmio_, kTdt, 0);
  Write(mmio_, kTipg, 10 | 10 << 10 | 10 << 20);
  Write(mmio_, kTctl, kTctlEn | kTctlPsp | (0x10 << 4) | (0x40 << 12));  // collision threshold, distance

  Log("e1000: %04x:%04x MAC %02x:%02x:%02x:%02x:%02x:%02x\n", device.vendor_id, device.device_id, mac_.b[0], mac_.b[1],
      mac_.b[2], mac_.b[3], mac_.b[4], mac_.b[5]);
  return true;
}

bool Driver::Send(const uint8_t* frame, size_t length) {
  volatile Descriptor& d = tx_[tx_next_];
  if (length > nic::kBufferSize || (d.status & kStatusDd) == 0) {
    return false;  // ring full
  }
  lib::Copy(tx_buffers_ + tx_next_ * nic::kBufferSize, frame, length);
  d.length = static_cast<uint16_t>(length);
  d.command = kCmdEop | kCmdIfcs | kCmdRs;
  d.status = 0;
  tx_next_ = (tx_next_ + 1) % nic::kTxRing;
  asm volatile("" ::: "memory");
  Write(mmio_, kTdt, static_cast<uint32_t>(tx_next_));
  return true;
}

size_t Driver::Receive(const uint8_t** frame) {
  if (rx_held_ >= 0) {
    rx_[rx_held_].status = 0;
    asm volatile("" ::: "memory");
    Write(mmio_, kRdt, static_cast<uint32_t>(rx_held_));
    rx_held_ = -1;
  }
  for (;;) {
    volatile Descriptor& d = rx_[rx_next_];
    if ((d.status & kStatusDd) == 0) {
      return 0;
    }
    asm volatile("" ::: "memory");  // descriptor before buffer
    const int index = rx_next_;
    rx_next_ = (rx_next_ + 1) % nic::kRxRing;
    rx_held_ = index;
    if ((d.status & kStatusEop) == 0 || d.checksum_start != 0 || d.length == 0) {
      // Multi-descriptor frame (cannot happen with 2 KiB buffers) or a receive error: skip.
      rx_[rx_held_].status = 0;
      Write(mmio_, kRdt, static_cast<uint32_t>(rx_held_));
      rx_held_ = -1;
      continue;
    }
    *frame = rx_buffers_ + index * nic::kBufferSize;
    return d.length;
  }
}

int Driver::LinkSpeed() {
  const uint32_t status = Read(mmio_, kStatus);
  if ((status & 2) == 0) {
    return 0;
  }
  static constexpr int kSpeeds[] = {10, 100, 1000, 1000};
  return kSpeeds[(status >> 6) & 3];
}

}  // namespace e1000
