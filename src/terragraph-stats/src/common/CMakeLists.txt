# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(stats-common)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

include_directories(${CMAKE_SOURCE_DIR})

find_library(STATS-IF stats-if)
find_library(FOLLY folly PATHS)

add_library(stats-common
  CompressionUtil.cpp
  Consts.cpp
  StatInfo.cpp
)

target_link_libraries(stats-common
  ${STATS-IF}
  ${FOLLY}
)

install(TARGETS stats-common DESTINATION lib)

install(FILES
  CompressionUtil.h
  Consts.h
  StatInfo.h
  DESTINATION include/stats/common
)
