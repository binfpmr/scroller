cmake_minimum_required(VERSION 3.28)
project(scroller LANGUAGES C CXX ASM)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

set(SCROLLER_TARGET "uefi" CACHE STRING "What to build: uefi (bootable image) or host (unit tests)")
set_property(CACHE SCROLLER_TARGET PROPERTY STRINGS uefi host)

# Platform-independent code, built both for the target and for the host tests.
set(SCROLLER_CORE_SOURCES
  src/lib/format.cpp
)

if(SCROLLER_TARGET STREQUAL "host")
  include(cmake/host.cmake)
elseif(SCROLLER_TARGET STREQUAL "uefi")
  include(cmake/uefi.cmake)
else()
  message(FATAL_ERROR "Unknown SCROLLER_TARGET '${SCROLLER_TARGET}'")
endif()
