// GDB remote protocol stub, reached over the debug link (platform/devlink; `tools/devlink.py gdb` turns it into the
// usual TCP port for gdb / VS Code on the development PC).
//
// The machine stops inside the exception handler: on a breakpoint (int3 written into the code), after a single step
// (trap flag), on a stop request (the main loop executes int3), or on a crash while a debugger is attached. While
// stopped, interrupts are off (no timer, no sound refills: the sound is silenced) and the stub keeps polling the
// network for the debugger's packets until it says continue or step. Limits: one CPU, no breakpoints in the network
// code (the stub needs it while stopped; such a stop becomes a normal crash report), registers beyond the general
// ones, rip, eflags and the segment selectors are not available.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "arch/x86_64/interrupts.h"

namespace platform {

// Called first for every CPU exception. Returns true if the stub handled it (the machine was stopped and the
// debugger resumed it), false for a crash nobody is debugging.
bool GdbStubOnException(x86::InterruptFrame& frame);

// The debug link's GDB commands. HandlePacket gets one packet's payload (between '$' and '#') and writes the reply
// payload; returns its length. Only "?" and the attach queries work while the game runs.
size_t GdbStubHandlePacket(const char* packet, size_t length, char* reply, size_t reply_size);
// Stop reply ("S05") if stopped, otherwise returns 0 (still running).
size_t GdbStubWait(char* reply, size_t reply_size);
// Ctrl+C in gdb: stop at the next main loop iteration. Also marks a debugger as attached.
void GdbStubRequestStop();
// From the main loop (PollDevlink): executes the requested stop.
void GdbStubPoll();

}  // namespace platform
