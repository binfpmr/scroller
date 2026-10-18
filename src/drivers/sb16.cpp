#include "drivers/sb16.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"

namespace sb16 {
namespace {

constexpr uint16_t kBase = 0x220;
constexpr uint16_t kMixerAddress = kBase + 0x4;
constexpr uint16_t kMixerData = kBase + 0x5;
constexpr uint16_t kReset = kBase + 0x6;
constexpr uint16_t kReadData = kBase + 0xa;
constexpr uint16_t kWrite = kBase + 0xc;       // command/data; bit 7 set = busy
constexpr uint16_t kReadStatus = kBase + 0xe;  // bit 7 set = data available; reading acks 8-bit IRQs
constexpr uint16_t kAck16 = kBase + 0xf;       // reading acks 16-bit IRQs

constexpr int kTimeout = 200000;

bool WriteDsp(uint8_t value) {
  for (int i = 0; i < kTimeout; ++i) {
    if ((x86::Inb(kWrite) & 0x80) == 0) {
      x86::Outb(kWrite, value);
      return true;
    }
  }
  return false;
}

bool ReadDsp(uint8_t* value) {
  for (int i = 0; i < kTimeout; ++i) {
    if ((x86::Inb(kReadStatus) & 0x80) != 0) {
      *value = x86::Inb(kReadData);
      return true;
    }
  }
  return false;
}

uint8_t ReadMixer(uint8_t reg) {
  x86::Outb(kMixerAddress, reg);
  return x86::Inb(kMixerData);
}

}  // namespace

bool Probe(Config* config) {
  if (x86::Inb(kReadStatus) == 0xff && x86::Inb(kWrite) == 0xff) {
    return false;  // nothing decodes these ports
  }
  x86::Outb(kReset, 1);
  for (int i = 0; i < 100; ++i) {
    x86::Inb(0x80);  // > 3 us
  }
  x86::Outb(kReset, 0);
  uint8_t value = 0;
  if (!ReadDsp(&value) || value != 0xaa) {
    return false;
  }
  uint8_t major = 0;
  uint8_t minor = 0;
  if (!WriteDsp(0xe1) || !ReadDsp(&major) || !ReadDsp(&minor) || major < 4) {
    Log("sb16: DSP version %u.%u, need 4.x\n", major, minor);
    return false;
  }
  config->version = static_cast<uint16_t>(major << 8 | minor);

  const uint8_t irq_bits = ReadMixer(0x80);
  const uint8_t dma_bits = ReadMixer(0x81);
  config->irq = (irq_bits & 0x02) ? 5 : (irq_bits & 0x04) ? 7 : (irq_bits & 0x08) ? 10 : (irq_bits & 0x01) ? 2 : 5;
  config->dma16 = (dma_bits & 0x20) ? 5 : (dma_bits & 0x40) ? 6 : (dma_bits & 0x80) ? 7 : 5;
  Log("sb16: DSP %u.%u, IRQ %u, 16-bit DMA %u\n", major, minor, config->irq, config->dma16);
  return true;
}

bool Start(const Config& config, uint64_t buffer_physical, uint32_t half_frames, uint32_t sample_rate) {
  const uint32_t words = 2 * half_frames;  // whole buffer, in 16-bit samples
  if (buffer_physical + words * 2 > (1u << 24) ||
      (buffer_physical >> 17) != ((buffer_physical + words * 2 - 1) >> 17)) {
    Log("sb16: DMA buffer %llx unusable\n", static_cast<unsigned long long>(buffer_physical));
    return false;
  }
  // Second ISA DMA controller (channels 4-7), 16-bit transfers counted in words.
  const uint8_t channel = config.dma16 & 3;
  constexpr uint16_t kPage[4] = {0x8f, 0x8b, 0x89, 0x8a};  // page registers of channels 4..7
  x86::Outb(0xd4, static_cast<uint8_t>(0x04 | channel));   // mask
  x86::Outb(0xd8, 0);                                      // clear the byte flip-flop
  x86::Outb(0xd6, static_cast<uint8_t>(0x58 | channel));   // single, auto-init, memory to device
  const uint32_t word_address = static_cast<uint32_t>(buffer_physical >> 1);
  x86::Outb(static_cast<uint16_t>(0xc0 + 4 * channel), static_cast<uint8_t>(word_address));
  x86::Outb(static_cast<uint16_t>(0xc0 + 4 * channel), static_cast<uint8_t>(word_address >> 8));
  x86::Outb(static_cast<uint16_t>(0xc2 + 4 * channel), static_cast<uint8_t>(words - 1));
  x86::Outb(static_cast<uint16_t>(0xc2 + 4 * channel), static_cast<uint8_t>((words - 1) >> 8));
  x86::Outb(kPage[channel], static_cast<uint8_t>(buffer_physical >> 16));
  x86::Outb(0xd4, channel);  // unmask

  const uint32_t block = half_frames - 1;
  const bool ok = WriteDsp(0x41) && WriteDsp(static_cast<uint8_t>(sample_rate >> 8)) &&  // output sample rate
                  WriteDsp(static_cast<uint8_t>(sample_rate)) && WriteDsp(0xd1) &&       // speaker on
                  WriteDsp(0xb6) && WriteDsp(0x10) &&  // 16-bit, DAC, auto-init, FIFO; mono signed
                  WriteDsp(static_cast<uint8_t>(block)) && WriteDsp(static_cast<uint8_t>(block >> 8));
  Log("sb16: %s, %u Hz, %u-sample halves at %llx\n", ok ? "playing" : "DSP did not accept commands", sample_rate,
      half_frames, static_cast<unsigned long long>(buffer_physical));
  return ok;
}

void AcknowledgeInterrupt() { x86::Inb(kAck16); }

void Stop() {
  WriteDsp(0xd9);  // exit 16-bit auto-init DMA
}

}  // namespace sb16
