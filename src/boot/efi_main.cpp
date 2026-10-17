// UEFI loader phase. M0: log, draw a test pattern into the current GOP mode, idle.

#include "arch/x86_64/io.h"
#include "drivers/debug_log.h"
#include "uefi/uefi.h"

// Cleared by the debugger (tools/gdb/scroller.py) to let a debug run continue. C linkage keeps the symbol name simple.
extern "C" {
volatile int scroller_debugger_wait = 1;
}

namespace {

// tools/qemu.py sets the QEMU debugcon readback value to this in debug mode; real hardware reads 0xFF here.
constexpr uint8_t kDebugconReadbackDebug = 0xdb;

void ConPrint(EFI_SYSTEM_TABLE* st, const char* text) {
  CHAR16 buf[128];
  size_t n = 0;
  for (; text[n] != '\0' && n + 1 < sizeof(buf) / sizeof(buf[0]); ++n) {
    buf[n] = static_cast<CHAR16>(text[n]);
  }
  buf[n] = 0;
  st->ConOut->OutputString(st->ConOut, buf);
}

void WaitForDebuggerIfRequested() {
  if (x86::Inb(0xe9) != kDebugconReadbackDebug) {
    return;
  }
  Log("waiting for debugger\n");
  while (scroller_debugger_wait) {
    x86::Pause();
  }
  Log("debugger attached\n");
}

uint32_t PackPixel(EFI_GRAPHICS_PIXEL_FORMAT format, uint8_t r, uint8_t g, uint8_t b) {
  if (format == PixelRedGreenBlueReserved8BitPerColor) {
    return r | (g << 8) | (b << 16);
  }
  return b | (g << 8) | (r << 16);
}

// Dark blue background with red, green, blue and white bars across the top quarter, so a screenshot shows whether
// the pixel format is handled correctly.
void DrawTestPattern(const EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE& mode) {
  const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION& info = *mode.Info;
  auto* fb = reinterpret_cast<volatile uint32_t*>(mode.FrameBufferBase);
  const uint32_t width = info.HorizontalResolution;
  const uint32_t height = info.VerticalResolution;
  const uint32_t bars[4] = {
      PackPixel(info.PixelFormat, 0xff, 0, 0),
      PackPixel(info.PixelFormat, 0, 0xff, 0),
      PackPixel(info.PixelFormat, 0, 0, 0xff),
      PackPixel(info.PixelFormat, 0xff, 0xff, 0xff),
  };
  const uint32_t background = PackPixel(info.PixelFormat, 0x10, 0x20, 0x50);
  for (uint32_t y = 0; y < height; ++y) {
    volatile uint32_t* row = fb + static_cast<size_t>(y) * info.PixelsPerScanLine;
    for (uint32_t x = 0; x < width; ++x) {
      row[x] = y < height / 4 ? bars[x * 4 / width] : background;
    }
  }
}

}  // namespace

extern "C" EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE* st) {
  debug_log::Init();
  Log("scroller: efi_main\n");

  EFI_BOOT_SERVICES* bs = st->BootServices;
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image = nullptr;
  if (!EFI_ERROR(bs->HandleProtocol(image, &EFI_LOADED_IMAGE_PROTOCOL_GUID, reinterpret_cast<void**>(&loaded_image)))) {
    Log("image base %p size %llu\n", loaded_image->ImageBase, static_cast<unsigned long long>(loaded_image->ImageSize));
  }
  WaitForDebuggerIfRequested();

  // The firmware watchdog would reset the machine after 5 minutes in boot services.
  bs->SetWatchdogTimer(0, 0, 0, nullptr);

  st->ConOut->ClearScreen(st->ConOut);
  ConPrint(st, "scroller\r\n");

  EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = nullptr;
  EFI_STATUS status = bs->LocateProtocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, nullptr, reinterpret_cast<void**>(&gop));
  if (EFI_ERROR(status)) {
    Log("no GOP: status %llx\n", static_cast<unsigned long long>(status));
    ConPrint(st, "No graphics output protocol.\r\n");
    return status;
  }
  const EFI_GRAPHICS_OUTPUT_MODE_INFORMATION& info = *gop->Mode->Info;
  Log("GOP mode %u/%u: %ux%u format %u stride %u fb %p\n", gop->Mode->Mode, gop->Mode->MaxMode,
      info.HorizontalResolution, info.VerticalResolution, static_cast<unsigned>(info.PixelFormat),
      info.PixelsPerScanLine, reinterpret_cast<void*>(gop->Mode->FrameBufferBase));
  if (info.PixelFormat != PixelRedGreenBlueReserved8BitPerColor &&
      info.PixelFormat != PixelBlueGreenRedReserved8BitPerColor) {
    Log("unsupported pixel format\n");
    ConPrint(st, "Unsupported pixel format.\r\n");
    return EFI_UNSUPPORTED;
  }

  DrawTestPattern(*gop->Mode);
  Log("boot ok\n");

  for (;;) {
    x86::Halt();
  }
}
