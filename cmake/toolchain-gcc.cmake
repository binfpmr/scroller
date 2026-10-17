# Freestanding x86_64 ELF build with the host GCC and GNU binutils. No OS is assumed: cmake/uefi.cmake turns off
# everything the host defaults would pull in (libc, start files, stack protector, ...).
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER gcc)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_ASM_COMPILER gcc)

# Compiler checks must not try to link a hosted executable.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_C_FLAGS_DEBUG_INIT "-Og")
set(CMAKE_CXX_FLAGS_DEBUG_INIT "-Og")
