# Bootable UEFI build: freestanding static-PIE ELF -> PE (scroller.efi) -> disk images.

find_program(SCROLLER_OBJCOPY objcopy REQUIRED)
find_program(SCROLLER_READELF readelf REQUIRED)

set(SCROLLER_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/src/boot/efi.ld)

# Only the compiler's own freestanding headers (stdint.h, stddef.h, stdarg.h, ...) are visible: no glibc, and no
# libstdc++ either (its headers depend on glibc's).
execute_process(COMMAND ${CMAKE_C_COMPILER} -print-file-name=include
                OUTPUT_VARIABLE SCROLLER_COMPILER_INCLUDE OUTPUT_STRIP_TRAILING_WHITESPACE COMMAND_ERROR_IS_FATAL ANY)

add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-nostdinc>
  $<$<COMPILE_LANGUAGE:C,CXX>:-isystem${SCROLLER_COMPILER_INCLUDE}>
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fpie>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fvisibility=hidden>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-plt>
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffunction-sections>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fdata-sections>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-protector>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-check>
  $<$<COMPILE_LANG_AND_ID:C,GNU>:-fno-stack-clash-protection>
  $<$<COMPILE_LANG_AND_ID:CXX,GNU>:-fno-stack-clash-protection>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fcf-protection=none>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-asynchronous-unwind-tables>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-unwind-tables>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mno-red-zone>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-omit-frame-pointer>  # backtraces in crash reports
  $<$<COMPILE_LANGUAGE:C,CXX>:-U_FORTIFY_SOURCE>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Werror>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

option(SCROLLER_UEFI_INPUT "Stay in boot services and read the keyboard through UEFI (for USB-only machines)" OFF)

# Git version of the sources, shown at boot and in the F1 overlay (to tell images apart on real hardware).
# Regenerated on every build; the file only changes when the version does.
add_custom_target(scroller_version
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/version_header.py ${CMAKE_SOURCE_DIR}
          ${SCROLLER_GENERATED_DIR}/scroller_version.h
  BYPRODUCTS ${SCROLLER_GENERATED_DIR}/scroller_version.h
  COMMENT "Checking the git version"
)

# All files under assets/ go into one pack, \\scroller\\assets.pak on the ESP (see src/lib/pack.h).
file(GLOB_RECURSE SCROLLER_ASSET_FILES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/assets/*)
set(SCROLLER_PAK ${CMAKE_BINARY_DIR}/assets.pak)
add_custom_command(
  OUTPUT ${SCROLLER_PAK}
  COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tools/pack_assets.py ${CMAKE_SOURCE_DIR}/assets ${SCROLLER_PAK}
  DEPENDS ${CMAKE_SOURCE_DIR}/tools/pack_assets.py ${SCROLLER_ASSET_FILES}
  COMMENT "Packing assets"
)

add_executable(scroller
  src/arch/x86_64/interrupt_stubs.S
  src/arch/x86_64/interrupts.cpp
  src/arch/x86_64/paging.cpp
  src/arch/x86_64/write_combining.cpp
  src/boot/crt0.S
  src/boot/assets_blob.S
  src/boot/efi_main.cpp
  src/boot/uefi_keyboard.cpp
  src/boot/video.cpp
  src/drivers/debug_log.cpp
  src/drivers/e1000.cpp
  src/drivers/hda.cpp
  src/drivers/intel_display.cpp
  src/drivers/io_apic.cpp
  src/drivers/local_apic.cpp
  src/drivers/pic.cpp
  src/drivers/sb16.cpp
  src/drivers/pci.cpp
  src/drivers/ps2_keyboard.cpp
  src/drivers/rtl8168.cpp
  src/drivers/vga_retrace.cpp
  src/lib/cxx_support.cpp
  src/lib/mem.c
  src/platform/devlink.cpp
  src/platform/game_loop.cpp
  src/platform/gdb_stub.cpp
  src/platform/interrupt_controllers.cpp
  src/platform/network.cpp
  src/platform/panic.cpp
  src/platform/reset.cpp
  src/platform/sound.cpp
  src/platform/stats_overlay.cpp
  src/platform/vsync.cpp
  ${SCROLLER_CORE_SOURCES}
)
target_compile_definitions(scroller PRIVATE SCROLLER_UEFI_INPUT=$<BOOL:${SCROLLER_UEFI_INPUT}>)
add_dependencies(scroller scroller_version)
set_target_properties(scroller PROPERTIES SUFFIX ".elf" LINK_DEPENDS ${SCROLLER_LINKER_SCRIPT})
target_include_directories(scroller PRIVATE src)
target_link_options(scroller PRIVATE
  -nostdlib
  -static-pie
  -Wl,-T,${SCROLLER_LINKER_SCRIPT}
  -Wl,--gc-sections
  -Wl,-z,norelro
  -Wl,-z,noexecstack
  -Wl,-z,max-page-size=0x1000
  -Wl,--build-id=none
  -Wl,--no-warn-rwx-segments
)
# mem.c implements memset/memcpy; GCC must not turn their loops back into calls to themselves.
set_source_files_properties(src/boot/assets_blob.S PROPERTIES
  COMPILE_DEFINITIONS "SCROLLER_ASSETS_PAK=\"${SCROLLER_PAK}\""
  OBJECT_DEPENDS ${SCROLLER_PAK})
set_source_files_properties(src/lib/mem.c PROPERTIES
  COMPILE_OPTIONS $<$<C_COMPILER_ID:GNU>:-fno-tree-loop-distribute-patterns>)

set(SCROLLER_EFI ${CMAKE_BINARY_DIR}/scroller.efi)
add_custom_command(
  OUTPUT ${SCROLLER_EFI}
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/check-relocs.sh ${SCROLLER_READELF} $<TARGET_FILE:scroller>
  COMMAND ${SCROLLER_OBJCOPY} -j .text -j .rodata -j .data -j .rela -j .reloc
          -O efi-app-x86_64 --subsystem=10 $<TARGET_FILE:scroller> ${SCROLLER_EFI}
  DEPENDS scroller ${CMAKE_SOURCE_DIR}/scripts/check-relocs.sh
  COMMENT "Converting scroller.elf to scroller.efi"
)

add_custom_command(
  OUTPUT ${CMAKE_BINARY_DIR}/scroller.img ${CMAKE_BINARY_DIR}/esp.img
  COMMAND ${CMAKE_SOURCE_DIR}/scripts/build-image.sh ${SCROLLER_EFI} ${CMAKE_BINARY_DIR}
  DEPENDS ${SCROLLER_EFI} ${CMAKE_SOURCE_DIR}/scripts/build-image.sh
  COMMENT "Building disk images"
)
add_custom_target(image ALL DEPENDS ${CMAKE_BINARY_DIR}/scroller.img)
