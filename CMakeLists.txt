# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

list(APPEND SNIPPET_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(cold-tracker)

if (CONFIG_USB_DEVICE_STACK_NEXT)
  include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
endif()

target_include_directories(app PRIVATE
  include
)

target_sources(app PRIVATE
  src/main.cpp
  src/coldtracker_sensing.cpp
  src/coldtracker_storage.cpp
  src/coldtracker_faults.cpp
)

target_sources_ifdef(CONFIG_NETWORKING app PRIVATE
  src/coldtracker_network.cpp
  src/network_backend.cpp
  src/coldtracker_time.cpp
  src/coldtracker_telemetry.cpp
)

target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE
  src/coldtracker_ota.cpp
)

if(NOT CONFIG_BOARD_NATIVE_SIM)
  target_sources(app PRIVATE
    # src/heap_trap.cpp
  )
endif()
