# Bootable UEFI build: freestanding static-PIE ELF -> PE (scroller.efi) -> disk images.

find_program(SCROLLER_OBJCOPY objcopy REQUIRED)
find_program(SCROLLER_READELF readelf REQUIRED)

set(SCROLLER_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/src/boot/efi.ld)

add_compile_options(
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffreestanding>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fpie>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fvisibility=hidden>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-plt>
  $<$<COMPILE_LANGUAGE:C,CXX>:-ffunction-sections>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fdata-sections>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-protector>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-check>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-stack-clash-protection>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fcf-protection=none>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-asynchronous-unwind-tables>
  $<$<COMPILE_LANGUAGE:C,CXX>:-fno-unwind-tables>
  $<$<COMPILE_LANGUAGE:C,CXX>:-mno-red-zone>
  $<$<COMPILE_LANGUAGE:C,CXX>:-U_FORTIFY_SOURCE>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wall>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Wextra>
  $<$<COMPILE_LANGUAGE:C,CXX>:-Werror>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-exceptions>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-rtti>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-threadsafe-statics>
  $<$<COMPILE_LANGUAGE:CXX>:-fno-use-cxa-atexit>
)

add_executable(scroller
  src/boot/crt0.S
  src/boot/efi_main.cpp
  src/drivers/debug_log.cpp
  src/lib/mem.c
  ${SCROLLER_CORE_SOURCES}
)
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
