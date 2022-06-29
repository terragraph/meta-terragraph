# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(udp-ping-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

include_directories(${CMAKE_SOURCE_DIR})

find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(FOLLY folly PATHS)

add_library(udp-ping-server
  TargetThread.cpp
)

target_link_libraries(udp-ping-server
  ${GLOG}
  ${GFLAGS}
  ${FOLLY}
  -lpthread
)

install(TARGETS udp-ping-server DESTINATION lib)

add_executable(udp_ping_server
  Main.cpp
)

target_link_libraries(udp_ping_server
  udp-ping-server
)

install(TARGETS udp_ping_server DESTINATION sbin)
