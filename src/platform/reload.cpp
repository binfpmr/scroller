#include "platform/reload.h"

#include "arch/x86_64/cpu.h"
#include "arch/x86_64/msr.h"
#include "arch/x86_64/paging.h"
#include "drivers/debug_log.h"
#include "lib/mem.h"
#include "lib/pe.h"
#include "platform/network.h"
#include "platform/reset.h"
#include "platform/sound.h"
#include "platform/usb.h"

extern "C" char __image_base[];  // efi.ld

namespace platform {
namespace {

// Found in the uploaded file by its magic; describes what handoff the image expects.
struct ReloadAbi {
  char magic[24];
  uint32_t version;
  uint32_t handoff_size;
};
static_assert(sizeof(ReloadAbi) == 32);

// The only copy of the magic in the image (searched for in uploaded files).
[[gnu::used]] constexpr ReloadAbi kAbi = {
    .magic = "SCROLLER-RELOAD-ABI-01", .version = kReloadVersion, .handoff_size = sizeof(ReloadHandoff)};

constexpr uint64_t kHandoffMagic = 0x464f'444e'4148'4c52;  // "RLHANDOF"

uint64_t g_entry = 0;
ReloadHandoff g_handoff;  // in the running image: the new one copies it before it touches any memory we use

bool FindAbi(const uint8_t* file, size_t size, ReloadAbi* abi) {
  for (size_t i = 0; i + sizeof(ReloadAbi) <= size; ++i) {
    if (file[i] == kAbi.magic[0] && __builtin_memcmp(file + i, kAbi.magic, sizeof(kAbi.magic)) == 0) {
      lib::Copy(abi, file + i, sizeof(ReloadAbi));
      return true;
    }
  }
  return false;
}

}  // namespace

bool PrepareReload(const BootInfo& boot, const uint8_t* file, size_t size, const char** error) {
  if (!boot.owns_machine || boot.reload_slot_size == 0 || boot.page_table_memory == nullptr) {
    *error = "this boot cannot hot reload (no slots, or the firmware still runs)";
    return false;
  }
  ReloadAbi abi;
  if (!FindAbi(file, size, &abi)) {
    *error = "no reload marker in the image (too old a build?)";
    return false;
  }
  if (abi.version != kReloadVersion || abi.handoff_size != sizeof(ReloadHandoff)) {
    *error = "incompatible build (BootInfo or the handoff changed): reboot instead";
    return false;
  }
  // The slot that does not hold the running image.
  const uint64_t running = reinterpret_cast<uint64_t>(__image_base);
  const uint64_t slot = running >= boot.reload_slots[0] && running < boot.reload_slots[0] + boot.reload_slot_size
                            ? boot.reload_slots[1]
                            : boot.reload_slots[0];
  lib::PeImage image;
  // The slot's pages may be mapped read-only (firmware memory protection): write with CR0.WP clear.
  uint64_t flags;
  asm volatile("pushfq; popq %0; cli" : "=r"(flags)::"memory");
  const uint64_t cr0 = x86::ReadCr0();
  x86::WriteCr0(cr0 & ~(1ull << 16));
  const bool loaded = lib::LoadPe(file, size, reinterpret_cast<uint8_t*>(slot), boot.reload_slot_size, &image, error);
  x86::WriteCr0(cr0);
  if ((flags & (1u << 9)) != 0) {
    asm volatile("sti" ::: "memory");
  }
  if (!loaded) {
    return false;
  }
  if (!x86::AllowExecute(slot, image.size_of_image)) {
    *error = "slot not mapped";
    return false;
  }
  g_entry = slot + image.entry_rva;
  Log("reload: %llu-byte image loaded at %p, entry %p\n", static_cast<unsigned long long>(image.size_of_image),
      reinterpret_cast<void*>(slot), reinterpret_cast<void*>(g_entry));
  return true;
}

void ExecuteReload(const BootInfo& boot) {
  x86::DisableInterrupts();
  StopUsb();
  StopNetwork();
  StopSound();
  g_handoff = {.magic = kHandoffMagic, .version = kReloadVersion, .size = sizeof(ReloadHandoff), .boot = boot};
  if (lib::Arena* tables = x86::PageTableArena()) {
    g_handoff.boot.page_table_used = tables->used();
  }
  Log("reload: starting the new image\n");
  // Microsoft x64 calling convention like the firmware's call: %rcx = ImageHandle, %rdx = SystemTable, 16-byte
  // aligned stack before the call, which pushes the return address.
  asm volatile(
      "mov %0, %%rsp\n"
      "and $-16, %%rsp\n"
      "call *%%rax\n"
      "ud2\n"
      :
      : "r"(boot.stack_top), "c"(kReloadHandle), "d"(&g_handoff), "a"(g_entry)
      : "memory");
  __builtin_unreachable();
}

bool AcceptReload(const void* handle, const void* system_table, BootInfo* boot) {
  if (reinterpret_cast<uint64_t>(handle) != kReloadHandle) {
    return false;
  }
  const auto* handoff = static_cast<const ReloadHandoff*>(system_table);
  if (handoff->magic != kHandoffMagic || handoff->version != kReloadVersion || handoff->size != sizeof(ReloadHandoff)) {
    Log("reload: handoff not understood, rebooting\n");
    Reboot(nullptr);  // the runtime services pointer is inside the handoff we cannot read
  }
  *boot = handoff->boot;
  return true;
}

}  // namespace platform
