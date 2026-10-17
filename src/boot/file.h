#pragma once

#include <stddef.h>

#include "uefi/uefi.h"

namespace boot {

// Reads a whole file from the volume this image was loaded from into pool memory (EfiLoaderData, so it stays valid
// after ExitBootServices). `path` uses backslashes, e.g. u"\\scroller\\assets.pak". Logs and returns false on error.
bool LoadFile(EFI_BOOT_SERVICES* bs, EFI_HANDLE image, const CHAR16* path, void** data, size_t* size);

}  // namespace boot
