# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(led-agent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

# Remove unused functions, e.g. template instantiations. This reduces size 3x.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wl,--gc-sections")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_library(FOLLY folly PATHS)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)

add_executable(led-agent
  Main.cpp
  LedManager.cpp
  StatsSubscriber.cpp
)

target_link_libraries(led-agent
  ${FBZMQ}
  ${MONITORCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${FOLLY}
  ${GLOG}
  ${GFLAGS}
  -lpthread
)

install(TARGETS led-agent DESTINATION sbin)
