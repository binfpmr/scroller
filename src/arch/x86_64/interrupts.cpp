#include "arch/x86_64/interrupts.h"

#include "drivers/debug_log.h"

extern "C" char isr_stubs[];  // interrupt_stubs.S: 256 stubs, 16 bytes each

namespace x86 {
namespace {

constexpr uint16_t kCodeSelector = 0x08;
constexpr uint16_t kDataSelector = 0x10;
constexpr uint16_t kTssSelector = 0x18;
constexpr int kIstStackSize = 16 * 1024;

struct [[gnu::packed]] Tss {
  uint32_t reserved0;
  uint64_t rsp[3];
  uint64_t reserved1;
  uint64_t ist[7];
  uint64_t reserved2;
  uint16_t reserved3;
  uint16_t iomap_base;
};
static_assert(sizeof(Tss) == 104);

struct [[gnu::packed]] IdtGate {
  uint16_t offset_low;
  uint16_t selector;
  uint8_t ist;
  uint8_t type_attributes;
  uint16_t offset_mid;
  uint32_t offset_high;
  uint32_t reserved;
};
static_assert(sizeof(IdtGate) == 16);

struct [[gnu::packed]] DescriptorPointer {
  uint16_t limit;
  uint64_t base;
};

constexpr uint8_t kInterruptGate = 0x8e;  // present, DPL 0, 64-bit interrupt gate (clears IF on entry)

InterruptHandler g_handlers[256];
ExceptionHandler g_on_exception = nullptr;

constexpr const char* kExceptionNames[32] = {
    "#DE DIVIDE ERROR",
    "#DB DEBUG",
    "NMI",
    "#BP BREAKPOINT",
    "#OF OVERFLOW",
    "#BR BOUND RANGE",
    "#UD INVALID OPCODE",
    "#NM NO FPU",
    "#DF DOUBLE FAULT",
    "COPROCESSOR OVERRUN",
    "#TS INVALID TSS",
    "#NP SEGMENT NOT PRESENT",
    "#SS STACK FAULT",
    "#GP GENERAL PROTECTION",
    "#PF PAGE FAULT",
    "RESERVED 15",
    "#MF X87 FPU ERROR",
    "#AC ALIGNMENT CHECK",
    "#MC MACHINE CHECK",
    "#XM SIMD ERROR",
    "#VE VIRTUALIZATION",
    "#CP CONTROL PROTECTION",
    "RESERVED 22",
    "RESERVED 23",
    "RESERVED 24",
    "RESERVED 25",
    "RESERVED 26",
    "RESERVED 27",
    "#HV HYPERVISOR INJECTION",
    "#VC VMM COMMUNICATION",
    "#SX SECURITY",
    "RESERVED 31",
};

}  // namespace

const char* ExceptionName(uint64_t vector) { return vector < 32 ? kExceptionNames[vector] : "INTERRUPT"; }

void SetInterruptHandler(uint8_t vector, InterruptHandler handler) { g_handlers[vector] = handler; }

bool InitInterrupts(lib::Arena& arena, ExceptionHandler on_exception) {
  g_on_exception = on_exception;
  auto* gdt = static_cast<uint64_t*>(arena.Allocate(5 * sizeof(uint64_t), 16));
  auto* tss = static_cast<Tss*>(arena.Allocate(sizeof(Tss), 16));
  auto* idt = static_cast<IdtGate*>(arena.Allocate(256 * sizeof(IdtGate), 16));
  auto* ist_stack = static_cast<uint8_t*>(arena.Allocate(kIstStackSize, 16));
  if (gdt == nullptr || tss == nullptr || idt == nullptr || ist_stack == nullptr) {
    return false;
  }

  *tss = {};
  tss->ist[0] = reinterpret_cast<uint64_t>(ist_stack + kIstStackSize);  // IST1
  tss->iomap_base = sizeof(Tss);                                        // no I/O permission bitmap
  const uint64_t tss_base = reinterpret_cast<uint64_t>(tss);
  const uint64_t tss_limit = sizeof(Tss) - 1;
  gdt[0] = 0;
  gdt[1] = 0x00af9a000000ffffull;  // 64-bit code, DPL 0
  gdt[2] = 0x00cf92000000ffffull;  // data, DPL 0
  gdt[3] = (tss_limit & 0xffff) | ((tss_base & 0xffffff) << 16) | (0x89ull << 40) | (((tss_limit >> 16) & 0xf) << 48) |
           (((tss_base >> 24) & 0xff) << 56);  // available 64-bit TSS
  gdt[4] = tss_base >> 32;

  for (int v = 0; v < 256; ++v) {
    const uint64_t target = reinterpret_cast<uint64_t>(isr_stubs + 16 * v);
    const bool own_stack = v == 2 || v == 8 || v == 18;  // NMI, double fault, machine check
    idt[v] = {.offset_low = static_cast<uint16_t>(target),
              .selector = kCodeSelector,
              .ist = static_cast<uint8_t>(own_stack ? 1 : 0),
              .type_attributes = kInterruptGate,
              .offset_mid = static_cast<uint16_t>(target >> 16),
              .offset_high = static_cast<uint32_t>(target >> 32),
              .reserved = 0};
  }

  const DescriptorPointer gdtr = {5 * sizeof(uint64_t) - 1, reinterpret_cast<uint64_t>(gdt)};
  const DescriptorPointer idtr = {256 * sizeof(IdtGate) - 1, reinterpret_cast<uint64_t>(idt)};
  asm volatile(
      "lgdt %0\n"
      // Reload CS with a far return, then the data segment registers.
      "pushq %1\n"
      "leaq 1f(%%rip), %%rax\n"
      "pushq %%rax\n"
      "lretq\n"
      "1:\n"
      "movw %2, %%ax\n"
      "movw %%ax, %%ds\n"
      "movw %%ax, %%es\n"
      "movw %%ax, %%ss\n"
      "movw %%ax, %%fs\n"
      "movw %%ax, %%gs\n"
      "ltr %w3\n"
      "lidt %4\n"
      :
      : "m"(gdtr), "i"(kCodeSelector), "i"(kDataSelector), "r"(kTssSelector), "m"(idtr)
      : "rax", "memory");
  Log("interrupts: GDT %p, IDT %p, IST stack %p\n", static_cast<void*>(gdt), static_cast<void*>(idt),
      static_cast<void*>(ist_stack));
  return true;
}

}  // namespace x86

extern "C" void interrupt_dispatch(x86::InterruptFrame* frame) {
  if (frame->vector < 32) {
    if (x86::g_on_exception != nullptr) {
      x86::g_on_exception(*frame);
      return;
    }
    Log("exception %llu, no handler\n", static_cast<unsigned long long>(frame->vector));
    for (;;) {
      asm volatile("cli; hlt");
    }
  }
  if (x86::InterruptHandler handler = x86::g_handlers[frame->vector]) {
    handler(*frame);
    return;
  }
  static uint64_t unexpected = 0;
  if (unexpected++ < 8) {
    Log("interrupt: unexpected vector %llu\n", static_cast<unsigned long long>(frame->vector));
  }
}
