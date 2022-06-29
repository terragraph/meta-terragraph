# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(tps23861-daemon)

include_directories(${CMAKE_SOURCE_DIR})

set(cpu ${CMAKE_SYSTEM_PROCESSOR})

find_library(LIBJSONC json-c)

add_executable(tps23861-daemon
  tps23861-daemon.c
)

target_link_libraries(tps23861-daemon
  ${LIBJSONC}
)

install(TARGETS tps23861-daemon DESTINATION sbin)
