// UEFI loader phase: everything that needs firmware services happens here, then the game takes over the machine.

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/io.h"
#include "boot/uefi_keyboard.h"
#include "boot/video.h"
#include "drivers/debug_log.h"
#include "drivers/ps2_keyboard.h"
#include "platform/boot_info.h"
#include "platform/game_loop.h"
#include "uefi/uefi.h"

#ifndef SCROLLER_UEFI_INPUT
#define SCROLLER_UEFI_INPUT 0
#endif

// Cleared by the debugger (tools/gdb/scroller.py) to let a debug run continue. C linkage keeps the symbol name simple.
extern "C" {
volatile int scroller_debugger_wait = 1;
}

namespace {

// Build option (cmake -DSCROLLER_UEFI_INPUT=ON, preset uefi-input): stay in boot services and read the keyboard
// through UEFI. A plain `if` instead of #if keeps both paths compiling in every build.
constexpr bool kUefiInput = SCROLLER_UEFI_INPUT;

constexpr size_t kGameMemoryBytes = 32 * 1024 * 1024;

// tools/qemu.py sets the QEMU debugcon readback value to this in debug mode; real hardware reads 0xFF here.
constexpr uint8_t kDebugconReadbackDebug = 0xdb;

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

uint64_t CalibrateTsc(EFI_BOOT_SERVICES* bs) {
  const uint64_t start = x86::Rdtsc();
  bs->Stall(50000);
  const uint64_t hz = (x86::Rdtsc() - start) * 20;
  Log("tsc: %llu MHz, invariant %s\n", static_cast<unsigned long long>(hz / 1000000),
      x86::HasInvariantTsc() ? "yes" : "no");
  return hz;
}

bool AllocateGameMemory(EFI_BOOT_SERVICES* bs, platform::BootInfo* boot) {
  EFI_PHYSICAL_ADDRESS address = 0;
  const UINTN pages = kGameMemoryBytes / EFI_PAGE_SIZE;
  const EFI_STATUS status = bs->AllocatePages(AllocateAnyPages, EfiLoaderData, pages, &address);
  if (EFI_ERROR(status)) {
    Log("memory: AllocatePages failed (status %llx)\n", static_cast<unsigned long long>(status));
    return false;
  }
  boot->memory = reinterpret_cast<void*>(address);
  boot->memory_size = kGameMemoryBytes;
  Log("memory: %llu MiB at %p\n", static_cast<unsigned long long>(kGameMemoryBytes >> 20), boot->memory);
  return true;
}

// Gets the final memory map and leaves boot services. Between GetMemoryMap and ExitBootServices nothing may allocate
// (Log only touches I/O ports, so it is fine).
bool ExitBootServices(EFI_HANDLE image, EFI_BOOT_SERVICES* bs, platform::BootInfo* boot) {
  UINTN size = 0;
  UINTN key = 0;
  UINTN descriptor_size = 0;
  UINT32 descriptor_version = 0;
  bs->GetMemoryMap(&size, nullptr, &key, &descriptor_size, &descriptor_version);
  // Room for the map to grow: allocating the buffer itself adds entries.
  const UINTN capacity = size + 16 * descriptor_size;
  void* buffer = nullptr;
  if (EFI_ERROR(bs->AllocatePool(EfiLoaderData, capacity, &buffer))) {
    Log("exit: cannot allocate the memory map buffer\n");
    return false;
  }

  for (int attempt = 0; attempt < 4; ++attempt) {
    size = capacity;
    EFI_STATUS status = bs->GetMemoryMap(&size, static_cast<EFI_MEMORY_DESCRIPTOR*>(buffer), &key, &descriptor_size,
                                         &descriptor_version);
    if (EFI_ERROR(status)) {
      Log("exit: GetMemoryMap failed (status %llx)\n", static_cast<unsigned long long>(status));
      return false;
    }
    status = bs->ExitBootServices(image, key);
    if (!EFI_ERROR(status)) {
      boot->memory_map = static_cast<const EFI_MEMORY_DESCRIPTOR*>(buffer);
      boot->memory_map_size = size;
      boot->memory_descriptor_size = descriptor_size;
      return true;
    }
    // EFI_INVALID_PARAMETER: the map changed meanwhile; fetch it again.
  }
  Log("exit: ExitBootServices kept failing\n");
  return false;
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

  platform::BootInfo boot;
  boot.runtime_services = st->RuntimeServices;
  if (!boot::SetUpVideo(bs, &boot.framebuffer)) {
    return EFI_UNSUPPORTED;
  }
  boot.tsc_hz = CalibrateTsc(bs);
  if (!AllocateGameMemory(bs, &boot)) {
    return EFI_LOAD_ERROR;
  }

  if (kUefiInput) {
    Log("input: UEFI console (boot services stay active)\n");
    boot::UefiKeyboard keyboard(st->ConIn, boot.tsc_hz);
    platform::RunGame(boot, keyboard);
  }

  if (!ExitBootServices(image, bs, &boot)) {
    return EFI_LOAD_ERROR;
  }
  // From here on the machine is ours. Nothing needs interrupts yet: input and timing are polled.
  x86::DisableInterrupts();
  boot.owns_machine = true;
  Log("exited boot services, memory map: %llu entries\n",
      static_cast<unsigned long long>(boot.memory_map_size / boot.memory_descriptor_size));

  ps2::Keyboard keyboard;
  keyboard.Init();
  platform::RunGame(boot, keyboard);
}
