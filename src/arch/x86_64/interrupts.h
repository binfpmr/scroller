// Our own GDT (with a TSS for the interrupt stack table) and IDT. Only after ExitBootServices.
#pragma once

#include <stdint.h>

#include "lib/arena.h"

namespace x86 {

// What isr_common (interrupt_stubs.S) leaves on the stack.
struct InterruptFrame {
  uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
  uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
  uint64_t vector, error_code;
  uint64_t rip, cs, rflags, rsp, ss;  // pushed by the CPU
};

using InterruptHandler = void (*)(InterruptFrame& frame);

// CPU exceptions (vectors 0..31) go here. Returning resumes at frame.rip with the frame's registers (the gdb stub
// does that after a breakpoint or a single step); for a fault that would just fault again.
using ExceptionHandler = void (*)(InterruptFrame& frame);

// Loads a GDT (code 0x08, data 0x10, TSS 0x18) and an IDT with all 256 vectors. Double fault, NMI and machine check
// run on their own stack (IST), so a stack overflow is still reported. Interrupts stay disabled.
bool InitInterrupts(lib::Arena& arena, ExceptionHandler on_exception);

// Handler for an external or software interrupt vector (32..255). The handler sends any EOI itself.
void SetInterruptHandler(uint8_t vector, InterruptHandler handler);

const char* ExceptionName(uint64_t vector);

inline void EnableInterrupts() { asm volatile("sti" ::: "memory"); }
inline uint64_t ReadCr2() {
  uint64_t v;
  asm volatile("mov %%cr2, %0" : "=r"(v));
  return v;
}

}  // namespace x86
