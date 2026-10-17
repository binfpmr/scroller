// Minimal UEFI definitions: only what scroller uses. Names follow the UEFI specification (2.10) so they can be looked
// up directly there; that is why this header does not follow the project naming style.
//
// Table layouts must match the spec exactly. Entries we don't call are kept as `void*` placeholders.
#pragma once

#include <stddef.h>
#include <stdint.h>

#define EFIAPI __attribute__((ms_abi))

using UINT8 = uint8_t;
using UINT16 = uint16_t;
using UINT32 = uint32_t;
using UINT64 = uint64_t;
using INT32 = int32_t;
using UINTN = uint64_t;
using BOOLEAN = uint8_t;
using CHAR16 = char16_t;
using EFI_STATUS = UINTN;
using EFI_HANDLE = void*;
using EFI_EVENT = void*;
using EFI_PHYSICAL_ADDRESS = UINT64;
using EFI_VIRTUAL_ADDRESS = UINT64;

inline constexpr EFI_STATUS EFI_SUCCESS = 0;
inline constexpr EFI_STATUS kEfiErrorBit = 0x8000000000000000ull;
inline constexpr EFI_STATUS EFI_LOAD_ERROR = kEfiErrorBit | 1;
inline constexpr EFI_STATUS EFI_INVALID_PARAMETER = kEfiErrorBit | 2;
inline constexpr EFI_STATUS EFI_UNSUPPORTED = kEfiErrorBit | 3;
inline constexpr EFI_STATUS EFI_BUFFER_TOO_SMALL = kEfiErrorBit | 5;
inline constexpr EFI_STATUS EFI_NOT_READY = kEfiErrorBit | 6;
inline constexpr EFI_STATUS EFI_NOT_FOUND = kEfiErrorBit | 14;

constexpr bool EFI_ERROR(EFI_STATUS status) { return (status & kEfiErrorBit) != 0; }

struct EFI_GUID {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8 Data4[8];
};

struct EFI_TABLE_HEADER {
  UINT64 Signature;
  UINT32 Revision;
  UINT32 HeaderSize;
  UINT32 CRC32;
  UINT32 Reserved;
};

// --- Simple Text Input Protocol (spec 12.3) ---

struct EFI_INPUT_KEY {
  UINT16 ScanCode;
  CHAR16 UnicodeChar;
};

// EFI scan codes (spec table "EFI Scan Codes for EFI_SIMPLE_TEXT_INPUT_PROTOCOL").
inline constexpr UINT16 SCAN_UP = 0x01;
inline constexpr UINT16 SCAN_DOWN = 0x02;
inline constexpr UINT16 SCAN_RIGHT = 0x03;
inline constexpr UINT16 SCAN_LEFT = 0x04;
inline constexpr UINT16 SCAN_F1 = 0x0b;
inline constexpr UINT16 SCAN_F2 = 0x0c;
inline constexpr UINT16 SCAN_ESC = 0x17;

struct EFI_SIMPLE_TEXT_INPUT_PROTOCOL {
  void* Reset;
  EFI_STATUS(EFIAPI* ReadKeyStroke)(EFI_SIMPLE_TEXT_INPUT_PROTOCOL* This, EFI_INPUT_KEY* Key);
  EFI_EVENT WaitForKey;
};

// --- Simple Text Output Protocol (spec 12.4) ---

struct EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL {
  void* Reset;
  EFI_STATUS(EFIAPI* OutputString)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This, const CHAR16* String);
  void* TestString;
  void* QueryMode;
  void* SetMode;
  void* SetAttribute;
  EFI_STATUS(EFIAPI* ClearScreen)(EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* This);
  void* SetCursorPosition;
  void* EnableCursor;
  void* Mode;
};

// --- Runtime Services (spec 4.5) ---

enum EFI_RESET_TYPE : UINT32 {
  EfiResetCold = 0,
  EfiResetWarm = 1,
  EfiResetShutdown = 2,
  EfiResetPlatformSpecific = 3,
};

struct EFI_RUNTIME_SERVICES {
  EFI_TABLE_HEADER Hdr;
  void* GetTime;
  void* SetTime;
  void* GetWakeupTime;
  void* SetWakeupTime;
  void* SetVirtualAddressMap;
  void* ConvertPointer;
  void* GetVariable;
  void* GetNextVariableName;
  void* SetVariable;
  void* GetNextHighMonotonicCount;
  void(EFIAPI* ResetSystem)(EFI_RESET_TYPE ResetType, EFI_STATUS ResetStatus, UINTN DataSize, void* ResetData);
  void* UpdateCapsule;
  void* QueryCapsuleCapabilities;
  void* QueryVariableInfo;
};

// --- Memory (spec 7.2) ---

enum EFI_ALLOCATE_TYPE : UINT32 {
  AllocateAnyPages = 0,
  AllocateMaxAddress = 1,
  AllocateAddress = 2,
};

enum EFI_MEMORY_TYPE : UINT32 {
  EfiReservedMemoryType = 0,
  EfiLoaderCode = 1,
  EfiLoaderData = 2,
  EfiBootServicesCode = 3,
  EfiBootServicesData = 4,
  EfiRuntimeServicesCode = 5,
  EfiRuntimeServicesData = 6,
  EfiConventionalMemory = 7,
};

struct EFI_MEMORY_DESCRIPTOR {
  UINT32 Type;
  EFI_PHYSICAL_ADDRESS PhysicalStart;
  EFI_VIRTUAL_ADDRESS VirtualStart;
  UINT64 NumberOfPages;
  UINT64 Attribute;
};

inline constexpr UINT64 EFI_PAGE_SIZE = 4096;

// --- Boot Services (spec 4.4) ---

struct EFI_BOOT_SERVICES {
  EFI_TABLE_HEADER Hdr;
  // Task priority
  void* RaiseTPL;
  void* RestoreTPL;
  // Memory
  EFI_STATUS(EFIAPI* AllocatePages)(EFI_ALLOCATE_TYPE Type, EFI_MEMORY_TYPE MemoryType, UINTN Pages,
                                    EFI_PHYSICAL_ADDRESS* Memory);
  void* FreePages;
  EFI_STATUS(EFIAPI* GetMemoryMap)(UINTN* MemoryMapSize, EFI_MEMORY_DESCRIPTOR* MemoryMap, UINTN* MapKey,
                                   UINTN* DescriptorSize, UINT32* DescriptorVersion);
  EFI_STATUS(EFIAPI* AllocatePool)(EFI_MEMORY_TYPE PoolType, UINTN Size, void** Buffer);
  EFI_STATUS(EFIAPI* FreePool)(void* Buffer);
  // Events and timers
  void* CreateEvent;
  void* SetTimer;
  void* WaitForEvent;
  void* SignalEvent;
  void* CloseEvent;
  void* CheckEvent;
  // Protocol handlers
  void* InstallProtocolInterface;
  void* ReinstallProtocolInterface;
  void* UninstallProtocolInterface;
  EFI_STATUS(EFIAPI* HandleProtocol)(EFI_HANDLE Handle, const EFI_GUID* Protocol, void** Interface);
  void* Reserved;
  void* RegisterProtocolNotify;
  void* LocateHandle;
  void* LocateDevicePath;
  void* InstallConfigurationTable;
  // Images
  void* LoadImage;
  void* StartImage;
  void* Exit;
  void* UnloadImage;
  EFI_STATUS(EFIAPI* ExitBootServices)(EFI_HANDLE ImageHandle, UINTN MapKey);
  // Miscellaneous
  void* GetNextMonotonicCount;
  EFI_STATUS(EFIAPI* Stall)(UINTN Microseconds);
  EFI_STATUS(EFIAPI* SetWatchdogTimer)(UINTN Timeout, UINT64 WatchdogCode, UINTN DataSize, CHAR16* WatchdogData);
  // Driver support
  void* ConnectController;
  void* DisconnectController;
  // Open and close protocol
  void* OpenProtocol;
  void* CloseProtocol;
  void* OpenProtocolInformation;
  // Library
  void* ProtocolsPerHandle;
  void* LocateHandleBuffer;
  EFI_STATUS(EFIAPI* LocateProtocol)(const EFI_GUID* Protocol, void* Registration, void** Interface);
  void* InstallMultipleProtocolInterfaces;
  void* UninstallMultipleProtocolInterfaces;
  // CRC
  void* CalculateCrc32;
  // Miscellaneous
  void* CopyMem;
  void* SetMem;
  void* CreateEventEx;
};

// --- System Table (spec 4.3) ---

struct EFI_SYSTEM_TABLE {
  EFI_TABLE_HEADER Hdr;
  CHAR16* FirmwareVendor;
  UINT32 FirmwareRevision;
  EFI_HANDLE ConsoleInHandle;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL* ConIn;
  EFI_HANDLE ConsoleOutHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;
  EFI_HANDLE StandardErrorHandle;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* StdErr;
  EFI_RUNTIME_SERVICES* RuntimeServices;
  EFI_BOOT_SERVICES* BootServices;
  UINTN NumberOfTableEntries;
  void* ConfigurationTable;
};

// --- Loaded Image Protocol (spec 9.1) ---

inline constexpr EFI_GUID EFI_LOADED_IMAGE_PROTOCOL_GUID = {
    0x5b1b31a1, 0x9562, 0x11d2, {0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}};

struct EFI_LOADED_IMAGE_PROTOCOL {
  UINT32 Revision;
  EFI_HANDLE ParentHandle;
  EFI_SYSTEM_TABLE* SystemTable;
  EFI_HANDLE DeviceHandle;
  void* FilePath;
  void* Reserved;
  UINT32 LoadOptionsSize;
  void* LoadOptions;
  void* ImageBase;
  UINT64 ImageSize;
  UINT32 ImageCodeType;
  UINT32 ImageDataType;
  void* Unload;
};

// --- Graphics Output Protocol (spec 12.9) ---

inline constexpr EFI_GUID EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID = {
    0x9042a9de, 0x23dc, 0x4a38, {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}};

enum EFI_GRAPHICS_PIXEL_FORMAT : UINT32 {
  PixelRedGreenBlueReserved8BitPerColor = 0,
  PixelBlueGreenRedReserved8BitPerColor = 1,
  PixelBitMask = 2,
  PixelBltOnly = 3,
};

struct EFI_PIXEL_BITMASK {
  UINT32 RedMask;
  UINT32 GreenMask;
  UINT32 BlueMask;
  UINT32 ReservedMask;
};

struct EFI_GRAPHICS_OUTPUT_MODE_INFORMATION {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  EFI_PIXEL_BITMASK PixelInformation;
  UINT32 PixelsPerScanLine;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION* Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
};

struct EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_STATUS(EFIAPI* QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, UINT32 ModeNumber, UINTN* SizeOfInfo,
                                EFI_GRAPHICS_OUTPUT_MODE_INFORMATION** Info);
  EFI_STATUS(EFIAPI* SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL* This, UINT32 ModeNumber);
  void* Blt;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
};
