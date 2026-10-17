#include "boot/file.h"

#include "drivers/debug_log.h"

namespace boot {

bool LoadFile(EFI_BOOT_SERVICES* bs, EFI_HANDLE image, const CHAR16* path, void** data, size_t* size) {
  EFI_LOADED_IMAGE_PROTOCOL* loaded_image = nullptr;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = nullptr;
  EFI_FILE_PROTOCOL* root = nullptr;
  EFI_FILE_PROTOCOL* file = nullptr;
  if (EFI_ERROR(bs->HandleProtocol(image, &EFI_LOADED_IMAGE_PROTOCOL_GUID, reinterpret_cast<void**>(&loaded_image))) ||
      EFI_ERROR(bs->HandleProtocol(loaded_image->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID,
                                   reinterpret_cast<void**>(&fs))) ||
      EFI_ERROR(fs->OpenVolume(fs, &root))) {
    Log("file: cannot open the boot volume\n");
    return false;
  }
  EFI_STATUS status = root->Open(root, &file, path, EFI_FILE_MODE_READ, 0);
  root->Close(root);
  if (EFI_ERROR(status)) {
    Log("file: open failed (status %llx)\n", static_cast<unsigned long long>(status));
    return false;
  }

  UINT64 file_size = 0;
  bool ok = !EFI_ERROR(file->SetPosition(file, ~0ull)) && !EFI_ERROR(file->GetPosition(file, &file_size)) &&
            !EFI_ERROR(file->SetPosition(file, 0));
  void* buffer = nullptr;
  ok = ok && !EFI_ERROR(bs->AllocatePool(EfiLoaderData, file_size > 0 ? file_size : 1, &buffer));
  UINT64 done = 0;
  while (ok && done < file_size) {
    UINTN chunk = file_size - done;
    ok = !EFI_ERROR(file->Read(file, &chunk, static_cast<char*>(buffer) + done)) && chunk > 0;
    done += chunk;
  }
  file->Close(file);
  if (!ok) {
    Log("file: read failed after %llu of %llu bytes\n", static_cast<unsigned long long>(done),
        static_cast<unsigned long long>(file_size));
    if (buffer != nullptr) {
      bs->FreePool(buffer);
    }
    return false;
  }
  *data = buffer;
  *size = file_size;
  return true;
}

}  // namespace boot
