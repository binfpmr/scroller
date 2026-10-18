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

find_package(Python3 REQUIRED COMPONENTS Interpreter)

# Generated sources (git version) go here.
set(SCROLLER_GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
include_directories(${SCROLLER_GENERATED_DIR})

# Platform-independent code, built both for the target and for the host tests.
set(SCROLLER_CORE_SOURCES
  src/audio/engine.cpp
  src/audio/song.cpp
  src/audio/synth.cpp
  src/drivers/acpi.cpp
  src/drivers/hid_keyboard.cpp
  src/drivers/ps2_decoder.cpp
  src/drivers/usb_descriptors.cpp
  src/game/camera.cpp
  src/game/enemy.cpp
  src/game/game.cpp
  src/game/level.cpp
  src/game/player.cpp
  src/game/render.cpp
  src/gfx/draw.cpp
  src/gfx/font.cpp
  src/gfx/present.cpp
  src/gfx/sprite.cpp
  src/gfx/viewport.cpp
  src/lib/format.cpp
  src/lib/pack.cpp
  src/lib/pe.cpp
  src/net/stack.cpp
  src/net/tftp.cpp
  src/platform/input.cpp
)

if(SCROLLER_TARGET STREQUAL "host")
  include(cmake/host.cmake)
elseif(SCROLLER_TARGET STREQUAL "uefi")
  include(cmake/uefi.cmake)
else()
  message(FATAL_ERROR "Unknown SCROLLER_TARGET '${SCROLLER_TARGET}'")
endif()
