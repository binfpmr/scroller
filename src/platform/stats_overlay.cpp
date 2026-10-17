#include "platform/stats_overlay.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/msr.h"
#include "drivers/debug_log.h"
#include "gfx/draw.h"
#include "gfx/font.h"
#include "lib/format.h"
#include "scroller_version.h"

namespace platform {
namespace {

// Hundredths of a millisecond per frame, for "%llu.%02llu MS".
uint64_t CentiMillis(uint64_t cycles, uint64_t frames, uint64_t tsc_hz) {
  return frames == 0 ? 0 : cycles * 100000 / tsc_hz / frames;
}

}  // namespace

StatsOverlay::StatsOverlay(uint64_t tsc_hz)
    : tsc_hz_(tsc_hz),
      period_start_(x86::Rdtsc()),
      has_aperf_mperf_((x86::Cpuid(0).eax >= 6) && (x86::Cpuid(6).ecx & 1) != 0) {
  if (has_aperf_mperf_) {
    last_aperf_ = x86::Rdmsr(x86::kMsrAperf);
    last_mperf_ = x86::Rdmsr(x86::kMsrMperf);
  }
}

void StatsOverlay::AddFrame(uint64_t render_cycles, uint64_t present_cycles) {
  ++frames_;
  render_cycles_ += render_cycles;
  present_cycles_ += present_cycles;
}

void StatsOverlay::Update(uint64_t now, const char* wc) {
  if (now - period_start_ < tsc_hz_) {
    return;
  }
  const uint64_t elapsed = now - period_start_;
  const uint64_t fps10 = frames_ * tsc_hz_ * 10 / elapsed;
  const uint64_t render = CentiMillis(render_cycles_, frames_, tsc_hz_);
  const uint64_t present = CentiMillis(present_cycles_, frames_, tsc_hz_);
  const uint64_t tsc_mhz = tsc_hz_ / 1000000;
  uint64_t cpu_mhz = 0;
  if (has_aperf_mperf_) {
    const uint64_t aperf = x86::Rdmsr(x86::kMsrAperf);
    const uint64_t mperf = x86::Rdmsr(x86::kMsrMperf);
    const uint64_t da = aperf - last_aperf_;
    const uint64_t dm = mperf - last_mperf_;
    cpu_mhz = dm == 0 ? 0 : tsc_mhz * da / dm;
    last_aperf_ = aperf;
    last_mperf_ = mperf;
  }

  char cpu[16] = "?";
  if (has_aperf_mperf_) {
    lib::Format(cpu, sizeof(cpu), "%llu", static_cast<unsigned long long>(cpu_mhz));
  }
  lib::Format(text_, sizeof(text_),
              "FPS %llu.%llu  RENDER %llu.%02llu MS  PRESENT %llu.%02llu MS\n"
              "CPU %s MHZ  TSC %llu MHZ  WC %s  VER %s",
              static_cast<unsigned long long>(fps10 / 10), static_cast<unsigned long long>(fps10 % 10),
              static_cast<unsigned long long>(render / 100), static_cast<unsigned long long>(render % 100),
              static_cast<unsigned long long>(present / 100), static_cast<unsigned long long>(present % 100), cpu,
              static_cast<unsigned long long>(tsc_mhz), wc, kScrollerVersion);
  if (updates_++ % 5 == 0) {
    Log("stats: %s\n", text_);
  }

  period_start_ = now;
  frames_ = 0;
  render_cycles_ = 0;
  present_cycles_ = 0;
}

void StatsOverlay::SetInfo(const char* info) { lib::Format(info_, sizeof(info_), "%s", info); }

void StatsOverlay::Draw(gfx::Surface& screen) const {
  if (!visible || text_[0] == '\0') {
    return;
  }
  const int width = lib::Max(gfx::TextWidth(text_), gfx::TextWidth(info_)) + 4;
  int lines = 2;
  for (const char* c = info_; *c != '\0'; ++c) {
    lines += (c == info_ || *c == '\n') ? 1 : 0;
  }
  gfx::FillRect(screen, 0, 0, width, lines * gfx::kLineHeight + 2, gfx::Rgb(0, 0, 0));
  gfx::DrawText(screen, 2, 2, text_, gfx::Rgb(255, 255, 255));
  gfx::DrawText(screen, 2, 2 + 2 * gfx::kLineHeight, info_, gfx::Rgb(180, 220, 255));
}

}  // namespace platform
