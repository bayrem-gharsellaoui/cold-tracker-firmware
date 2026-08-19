# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cold-tracker)

include(${ZEPHYR_BASE}/samples/net/common/common.cmake)

target_include_directories(app PRIVATE
  include
)

target_sources(app PRIVATE
  src/main.c
  src/coldtracker_network.c
)
