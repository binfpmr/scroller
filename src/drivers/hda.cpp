#include "drivers/hda.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "lib/format.h"
#include "lib/mem.h"

namespace hda {
namespace {

// Controller registers.
constexpr uint32_t kGcap = 0x00;
constexpr uint32_t kGctl = 0x08;
constexpr uint32_t kStatests = 0x0e;
constexpr uint32_t kCorbLBase = 0x40, kCorbUBase = 0x44, kCorbWp = 0x48, kCorbRp = 0x4a, kCorbCtl = 0x4c,
                   kCorbSize = 0x4e;
constexpr uint32_t kRirbLBase = 0x50, kRirbUBase = 0x54, kRirbWp = 0x58, kRintCnt = 0x5a, kRirbCtl = 0x5c,
                   kRirbSts = 0x5d, kRirbSize = 0x5e;
// Stream descriptor registers (relative to the descriptor).
constexpr uint32_t kSdCtl = 0x00, kSdLpib = 0x04, kSdCbl = 0x08, kSdLvi = 0x0c, kSdFmt = 0x12, kSdBdpl = 0x18,
                   kSdBdpu = 0x1c;

constexpr uint8_t kStreamTag = 1;
constexpr uint16_t kFormat48k16Stereo = 0x0011;  // 48 kHz base, 16 bit, 2 channels
constexpr int kBdlEntries = 4;

// Widget types (audio widget capabilities bits 23:20).
constexpr int kWidgetOutput = 0x0, kWidgetMixer = 0x2, kWidgetSelector = 0x3, kWidgetPin = 0x4;

uint32_t Verb12(int codec, int nid, uint32_t verb, uint32_t payload) {
  return (uint32_t(codec) << 28) | (uint32_t(nid) << 20) | (verb << 8) | (payload & 0xff);
}
uint32_t Verb4(int codec, int nid, uint32_t verb, uint32_t payload) {
  return (uint32_t(codec) << 28) | (uint32_t(nid) << 20) | (verb << 16) | (payload & 0xffff);
}

void Delay(uint64_t microseconds) {
  // Before the TSC frequency is known here, port 0x80 writes take ~1 us each.
  for (uint64_t i = 0; i < microseconds; ++i) {
    x86::Outb(0x80, 0);
  }
}

template <typename T>
T Reg(volatile uint8_t* mmio, uint32_t offset) {
  return *reinterpret_cast<volatile T*>(mmio + offset);
}
template <typename T>
void SetReg(volatile uint8_t* mmio, uint32_t offset, T value) {
  *reinterpret_cast<volatile T*>(mmio + offset) = value;
}

}  // namespace

uint32_t Controller::Command(uint32_t verb) {
  if (command_failed_) {
    return 0xffffffff;  // a codec that stopped answering would make every further command wait for the timeout
  }
  const uint16_t wp = static_cast<uint16_t>((Reg<uint16_t>(mmio_, kCorbWp) + 1) % corb_entries_);
  corb_[wp] = verb;
  SetReg<uint16_t>(mmio_, kCorbWp, wp);
  for (int i = 0; i < 20000; ++i) {
    const uint16_t rirb_wp = Reg<uint16_t>(mmio_, kRirbWp) & 0xff;
    if (rirb_wp != rirb_read_) {
      rirb_read_ = static_cast<uint16_t>((rirb_read_ + 1) % rirb_entries_);
      // Clear the response flag: the controller stops taking commands once RINTCNT responses are unacknowledged.
      SetReg<uint8_t>(mmio_, kRirbSts, 0x05);
      return static_cast<uint32_t>(rirb_[rirb_read_]);
    }
    Delay(1);
  }
  Log("hda: command %08x timed out\n", verb);
  command_failed_ = true;
  return 0xffffffff;
}

bool Controller::Init(const pci::Device& device, lib::Arena& arena) {
  const uint64_t bar = pci::MemoryBar(device, 0);
  if (bar == 0 || (!x86::IsMapped(bar) && !x86::MapUncached(bar, 0x4000))) {
    Log("hda: BAR0 unusable\n");
    return false;
  }
  // Memory space + bus master.
  const uint32_t command = pci::Read32(device.bus, device.device, device.function, 0x04);
  pci::Write32(device.bus, device.device, device.function, 0x04, (command & 0xffff) | 0x6);
  mmio_ = reinterpret_cast<volatile uint8_t*>(bar);

  const uint16_t gcap = Reg<uint16_t>(mmio_, kGcap);
  const int input_streams = (gcap >> 8) & 0xf;
  const int output_streams = (gcap >> 12) & 0xf;
  if (output_streams == 0) {
    Log("hda: no output streams\n");
    return false;
  }
  stream_offset_ = 0x80 + 0x20 * input_streams;  // first output stream descriptor

  // Controller reset: CRST low, then high; codecs announce themselves in STATESTS.
  SetReg<uint32_t>(mmio_, kGctl, Reg<uint32_t>(mmio_, kGctl) & ~1u);
  for (int i = 0; i < 1000 && (Reg<uint32_t>(mmio_, kGctl) & 1) != 0; ++i) {
    Delay(10);
  }
  SetReg<uint32_t>(mmio_, kGctl, Reg<uint32_t>(mmio_, kGctl) | 1u);
  for (int i = 0; i < 1000 && (Reg<uint32_t>(mmio_, kGctl) & 1) == 0; ++i) {
    Delay(10);
  }
  Delay(1000);
  const uint16_t codecs = Reg<uint16_t>(mmio_, kStatests);
  Log("hda: %04x:%04x, %d in / %d out streams, codecs %x\n", device.vendor_id, device.device_id, input_streams,
      output_streams, codecs);

  // Command rings (CORB 1 KiB, RIRB 2 KiB, both 128-byte aligned; 256 entries if supported).
  corb_ = static_cast<uint32_t*>(arena.Allocate(1024, 128));
  rirb_ = static_cast<uint64_t*>(arena.Allocate(2048, 128));
  const int bdl_bytes = kBdlEntries * 16;
  auto* bdl = static_cast<uint32_t*>(arena.Allocate(bdl_bytes, 128));
  buffer_ = static_cast<int16_t*>(arena.Allocate(kBufferFrames * 4, 128));
  if (corb_ == nullptr || rirb_ == nullptr || bdl == nullptr || buffer_ == nullptr) {
    return false;
  }
  lib::Fill(corb_, 0, 1024);
  lib::Fill(rirb_, 0, 2048);
  lib::Fill(buffer_, 0, kBufferFrames * 4);

  SetReg<uint8_t>(mmio_, kCorbCtl, 0);
  SetReg<uint8_t>(mmio_, kRirbCtl, 0);
  const uint8_t corb_size_caps = Reg<uint8_t>(mmio_, kCorbSize) >> 4;
  const uint8_t rirb_size_caps = Reg<uint8_t>(mmio_, kRirbSize) >> 4;
  corb_entries_ = (corb_size_caps & 4) ? 256 : (corb_size_caps & 2) ? 16 : 2;
  rirb_entries_ = (rirb_size_caps & 4) ? 256 : (rirb_size_caps & 2) ? 16 : 2;
  SetReg<uint8_t>(mmio_, kCorbSize, corb_entries_ == 256 ? 2 : corb_entries_ == 16 ? 1 : 0);
  SetReg<uint8_t>(mmio_, kRirbSize, rirb_entries_ == 256 ? 2 : rirb_entries_ == 16 ? 1 : 0);
  const uint64_t corb_phys = reinterpret_cast<uint64_t>(corb_);
  const uint64_t rirb_phys = reinterpret_cast<uint64_t>(rirb_);
  SetReg<uint32_t>(mmio_, kCorbLBase, static_cast<uint32_t>(corb_phys));
  SetReg<uint32_t>(mmio_, kCorbUBase, static_cast<uint32_t>(corb_phys >> 32));
  SetReg<uint32_t>(mmio_, kRirbLBase, static_cast<uint32_t>(rirb_phys));
  SetReg<uint32_t>(mmio_, kRirbUBase, static_cast<uint32_t>(rirb_phys >> 32));
  SetReg<uint16_t>(mmio_, kCorbRp, 0x8000);  // reset the read pointer
  for (int i = 0; i < 1000 && (Reg<uint16_t>(mmio_, kCorbRp) & 0x8000) == 0; ++i) {
    Delay(1);
  }
  SetReg<uint16_t>(mmio_, kCorbRp, 0);
  for (int i = 0; i < 1000 && (Reg<uint16_t>(mmio_, kCorbRp) & 0x8000) != 0; ++i) {
    Delay(1);
  }
  SetReg<uint16_t>(mmio_, kCorbWp, 0);
  SetReg<uint16_t>(mmio_, kRirbWp, 0x8000);  // reset the write pointer
  SetReg<uint16_t>(mmio_, kRintCnt, 0xff);
  rirb_read_ = 0;
  SetReg<uint8_t>(mmio_, kCorbCtl, 0x2);  // run
  SetReg<uint8_t>(mmio_, kRirbCtl, 0x2);

  bool codec_ok = false;
  for (int codec = 0; codec < 15 && !codec_ok; ++codec) {
    if ((codecs & (1 << codec)) != 0) {
      command_failed_ = false;
      codec_ok = SetUpCodec(codec);
    }
  }
  if (!codec_ok) {
    Log("hda: no codec with an output path\n");
    return false;
  }

  // Output stream: reset, buffer descriptor list of kBdlEntries equal parts, format, stream tag.
  volatile uint8_t* sd = mmio_ + stream_offset_;
  SetReg<uint8_t>(sd, kSdCtl, 0x01);  // stream reset
  for (int i = 0; i < 1000 && (Reg<uint8_t>(sd, kSdCtl) & 1) == 0; ++i) {
    Delay(1);
  }
  SetReg<uint8_t>(sd, kSdCtl, 0x00);
  for (int i = 0; i < 1000 && (Reg<uint8_t>(sd, kSdCtl) & 1) != 0; ++i) {
    Delay(1);
  }
  const uint32_t part_bytes = kBufferFrames * 4 / kBdlEntries;
  for (int i = 0; i < kBdlEntries; ++i) {
    const uint64_t address = reinterpret_cast<uint64_t>(buffer_) + i * part_bytes;
    bdl[i * 4 + 0] = static_cast<uint32_t>(address);
    bdl[i * 4 + 1] = static_cast<uint32_t>(address >> 32);
    bdl[i * 4 + 2] = part_bytes;
    bdl[i * 4 + 3] = 0;  // no interrupt on completion
  }
  const uint64_t bdl_phys = reinterpret_cast<uint64_t>(bdl);
  SetReg<uint32_t>(sd, kSdBdpl, static_cast<uint32_t>(bdl_phys));
  SetReg<uint32_t>(sd, kSdBdpu, static_cast<uint32_t>(bdl_phys >> 32));
  SetReg<uint32_t>(sd, kSdCbl, kBufferFrames * 4);
  SetReg<uint16_t>(sd, kSdLvi, kBdlEntries - 1);
  SetReg<uint16_t>(sd, kSdFmt, kFormat48k16Stereo);
  SetReg<uint8_t>(sd, kSdCtl + 2, static_cast<uint8_t>(kStreamTag << 4));  // stream number in bits 23:20
  return true;
}

bool Controller::SetUpCodec(int codec) {
  auto param = [&](int nid, uint32_t id) { return Command(Verb12(codec, nid, 0xf00, id)); };
  const uint32_t vendor = param(0, 0x00);
  if (vendor == 0xffffffff || vendor == 0) {
    return false;
  }
  // Find the audio function group.
  const uint32_t root_nodes = param(0, 0x04);
  int afg = -1;
  for (int n = (root_nodes >> 16) & 0xff, end = n + (root_nodes & 0xff); n < end; ++n) {
    if ((param(n, 0x05) & 0xff) == 0x01) {
      afg = n;
    }
  }
  if (afg < 0) {
    Log("hda: codec %d (%08x) has no audio function group\n", codec, vendor);
    return false;
  }
  Command(Verb12(codec, afg, 0x705, 0x00));  // power state D0
  const uint32_t nodes = param(afg, 0x04);
  const int first = (nodes >> 16) & 0xff;
  const int count = nodes & 0xff;
  constexpr int kMaxNodes = 128;
  if (first + count > kMaxNodes) {
    return false;
  }
  uint32_t caps[kMaxNodes] = {};
  for (int n = first; n < first + count; ++n) {
    caps[n] = param(n, 0x09);
  }
  auto type = [&](int n) { return static_cast<int>((caps[n] >> 20) & 0xf); };
  // Connection list of node n (short form only; up to 16 entries).
  auto connections = [&](int n, uint8_t* out) {
    if ((caps[n] & (1 << 8)) == 0) {
      return 0;
    }
    const uint32_t length_info = param(n, 0x0e);
    if ((length_info & 0x80) != 0) {
      return 0;  // long form: not handled
    }
    const int length = static_cast<int>(length_info & 0x7f) < 16 ? static_cast<int>(length_info & 0x7f) : 16;
    for (int i = 0; i < length; i += 4) {
      const uint32_t entries = Command(Verb12(codec, n, 0xf02, i));
      for (int k = 0; k < 4 && i + k < length; ++k) {
        out[i + k] = static_cast<uint8_t>(entries >> (8 * k)) & 0x7f;
      }
    }
    return length;
  };
  // Depth-first search from a pin to a DAC; records the path (pin first) and the connection index taken at each step.
  int path[6];
  int via[6];
  auto find_dac = [&](auto& self, int n, int depth) -> int {
    path[depth] = n;
    if (type(n) == kWidgetOutput) {
      return depth + 1;
    }
    if (depth == 5) {
      return 0;
    }
    uint8_t conn[16];
    const int len = connections(n, conn);
    for (int i = 0; i < len; ++i) {
      const int next = conn[i];
      if (next < first || next >= first + count) {
        continue;
      }
      const int t = type(next);
      if (t == kWidgetOutput || t == kWidgetMixer || t == kWidgetSelector) {
        via[depth] = i;
        if (const int length = self(self, next, depth + 1)) {
          return length;
        }
      }
    }
    return 0;
  };

  int configured = 0;
  int first_dac = -1;
  char pins[32] = "";
  size_t pins_length = 0;
  for (int n = first; n < first + count; ++n) {
    if (type(n) != kWidgetPin) {
      continue;
    }
    const uint32_t pin_caps = param(n, 0x0c);
    const uint32_t config = Command(Verb12(codec, n, 0xf1c, 0));
    const int connectivity = (config >> 30) & 3;  // 1: not connected
    const int device = (config >> 20) & 0xf;      // 0 line out, 1 speaker, 2 headphone
    if ((pin_caps & (1 << 4)) == 0 || connectivity == 1 || device > 2) {
      continue;
    }
    const int length = find_dac(find_dac, n, 0);
    if (length == 0) {
      continue;
    }
    // Configure the path: select connections, power up, unmute amplifiers at their 0 dB step.
    for (int i = 0; i < length; ++i) {
      const int node = path[i];
      Command(Verb12(codec, node, 0x705, 0x00));
      if (i + 1 < length && type(node) != kWidgetMixer) {
        Command(Verb12(codec, node, 0x701, via[i]));  // connection select
      }
      if ((caps[node] & (1 << 2)) != 0) {  // output amplifier
        const uint32_t amp = (caps[node] & (1 << 3)) ? param(node, 0x12) : param(afg, 0x12);
        Command(Verb4(codec, node, 0x3, 0xb000 | (amp & 0x7f)));  // output, left+right, unmuted, 0 dB
      }
      if (i + 1 < length && (caps[node] & (1 << 1)) != 0) {  // input amplifier on the input we use
        const uint32_t amp = (caps[node] & (1 << 3)) ? param(node, 0x0d) : param(afg, 0x0d);
        Command(Verb4(codec, node, 0x3, 0x7000 | (via[i] << 8) | (amp & 0x7f)));
      }
    }
    const int dac = path[length - 1];
    Command(Verb4(codec, dac, 0x2, kFormat48k16Stereo));          // converter format
    Command(Verb12(codec, dac, 0x706, kStreamTag << 4));          // stream tag, channel 0
    Command(Verb12(codec, n, 0x707, device == 2 ? 0xc0 : 0x40));  // pin out enable (+ headphone amp)
    if ((pin_caps & (1 << 16)) != 0) {
      Command(Verb12(codec, n, 0x70c, 0x02));  // EAPD: external amplifier on (laptop speakers)
    }
    Log("hda: codec %d pin %d (%s) -> DAC %d, %d nodes\n", codec, n,
        device == 1   ? "speaker"
        : device == 2 ? "headphone"
                      : "line out",
        dac, length);
    pins_length += lib::Format(pins + pins_length, pins_length < sizeof(pins) ? sizeof(pins) - pins_length : 0, "%s%s",
                               configured > 0 ? "+" : "",
                               device == 1   ? "SPK"
                               : device == 2 ? "HP"
                                             : "LINE");
    if (first_dac < 0) {
      first_dac = dac;
    }
    ++configured;
  }
  if (configured == 0) {
    return false;
  }
  lib::Format(description_, sizeof(description_), "HDA %04x:%04x %s", vendor >> 16, vendor & 0xffff, pins);
  return true;
}

void Controller::Start() {
  last_position_ = 0;
  wraps_ = 0;
  SetReg<uint8_t>(mmio_ + stream_offset_, kSdCtl, 0x02);  // run
}

uint64_t Controller::FramesPlayed() {
  const uint32_t position = Reg<uint32_t>(mmio_ + stream_offset_, kSdLpib) / 4;  // bytes -> frames
  if (position < last_position_) {
    ++wraps_;
  }
  last_position_ = position;
  return wraps_ * kBufferFrames + position;
}

}  // namespace hda
