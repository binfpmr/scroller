#pragma once

#include "gfx/framebuffer.h"
#include "uefi/uefi.h"

namespace boot {

// Finds GOP, switches to the best mode for the 320x240 game screen (gfx::ChooseVideoMode) and describes its
// framebuffer. Must run before ExitBootServices.
bool SetUpVideo(EFI_BOOT_SERVICES* bs, gfx::Framebuffer* framebuffer);

}  // namespace boot
