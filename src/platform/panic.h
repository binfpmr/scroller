// Crash report for CPU exceptions: written to the debug log and drawn on screen, then Esc reboots.
#pragma once

#include "arch/x86_64/interrupts.h"
#include "gfx/framebuffer.h"
#include "gfx/surface.h"
#include "gfx/viewport.h"
#include "platform/input.h"
#include "uefi/uefi.h"

namespace platform {

// Where to draw and how to wait for Esc; until this is called a crash is only logged.
void SetPanicContext(gfx::Surface* screen, const gfx::Framebuffer* fb, const gfx::Viewport* viewport,
                     KeyboardSource* keyboard, EFI_RUNTIME_SERVICES* runtime_services);

// x86::ExceptionHandler for InitInterrupts.
void OnException(x86::InterruptFrame& frame);

}  // namespace platform
