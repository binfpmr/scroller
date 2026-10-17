#pragma once

#include "platform/boot_info.h"
#include "platform/input.h"

namespace platform {

// Fixed 60 Hz update loop: poll input, update, render, present. Esc reboots.
[[noreturn]] void RunGame(const BootInfo& boot, KeyboardSource& keyboard);

}  // namespace platform
