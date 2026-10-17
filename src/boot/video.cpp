#include "boot/video.h"

#include "drivers/debug_log.h"
#include "gfx/viewport.h"
#include "lib/format.h"

namespace boot {
namespace {

constexpr int kMaxModes = 64;

bool UsableFormat(EFI_GRAPHICS_PIXEL_FORMAT format) {
  return format == PixelRedGreenBlueReserved8BitPerColor || format == PixelBlueGreenRedReserved8BitPerColor;
}

// All modes, several per log line ("*" marks pixel formats we cannot use).
void LogModes(const gfx::VideoMode* modes, int count) {
  char line[96];
  size_t used = 0;
  for (int i = 0; i < count; ++i) {
    used += lib::Format(line + used, sizeof(line) - used, " %ux%u%s", modes[i].width, modes[i].height,
                        modes[i].usable ? "" : "*");
    if (used > 70 || i == count - 1) {
      Log("video: modes%s\n", line);
      used = 0;
    }
  }
}

}  // namespace

bool SetUpVideo(EFI_BOOT_SERVICES* bs, gfx::Framebuffer* framebuffer) {
  EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
  EFI_STATUS status = bs->LocateProtocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nullptr, reinterpret_cast<void**>(&gop));
  if (EFI_ERROR(status)) {
    Log("video: no GOP (status %llx)\n", static_cast<unsigned long long>(status));
    return false;
  }

  gfx::VideoMode modes[kMaxModes];
  const int count = gop->Mode->MaxMode < kMaxModes ? static_cast<int>(gop->Mode->MaxMode) : kMaxModes;
  for (int i = 0; i < count; ++i) {
    UINTN size = 0;
    EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* info = nullptr;
    if (EFI_ERROR(gop->QueryMode(gop, static_cast<UINT32>(i), &size, &info))) {
      continue;
    }
    modes[i] = {.width = info->HorizontalResolution,
                .height = info->VerticalResolution,
                .usable = UsableFormat(info->PixelFormat)};
    bs->FreePool(info);
  }

  LogModes(modes, count);
  const int current = static_cast<int>(gop->Mode->Mode);
  const int chosen = gfx::ChooseVideoMode(modes, count, current);
  if (chosen < 0) {
    Log("video: no usable mode among %d\n", count);
    return false;
  }
  if (chosen != current) {
    status = gop->SetMode(gop, static_cast<UINT32>(chosen));
    if (EFI_ERROR(status)) {
      Log("video: SetMode(%d) failed (status %llx), keeping mode %d\n", chosen, static_cast<unsigned long long>(status),
          current);
      if (!modes[current].usable) {
        return false;
      }
    }
  }

  const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION& info = *gop->Mode->Info;
  framebuffer->base = reinterpret_cast<uint32_t*>(gop->Mode->FrameBufferBase);
  framebuffer->width = info.HorizontalResolution;
  framebuffer->height = info.VerticalResolution;
  framebuffer->stride = info.PixelsPerScanLine;
  framebuffer->order =
      info.PixelFormat == PixelRedGreenBlueReserved8BitPerColor ? gfx::PixelOrder::kRgbx : gfx::PixelOrder::kBgrx;
  Log("video: mode %u of %d (was %d): %ux%u %s stride %u fb %p\n", gop->Mode->Mode, count, current, framebuffer->width,
      framebuffer->height, framebuffer->order == gfx::PixelOrder::kRgbx ? "RGBX" : "BGRX", framebuffer->stride,
      static_cast<void*>(framebuffer->base));
  return true;
}

}  // namespace boot
