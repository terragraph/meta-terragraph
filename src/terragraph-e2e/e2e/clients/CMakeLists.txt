# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-clients)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(THRIFT_SYS_INC fbzmq/service/if/gen-cpp2)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)
include_directories(${THRIFT_SYS_INC})

find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(FOLLY folly PATHS)
find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(FBZMQ fbzmq)

add_library(e2e-clients
  BaseCounterMonitor.cpp
  MinionClient.cpp
  StatCache.cpp
)

target_link_libraries(e2e-clients
  ${E2E-IF}
  ${E2E-COMMON}
  ${FBZMQ}
  ${FOLLY}
  ${GLOG}
  ${GFLAGS}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  -lpthread
)

install(TARGETS e2e-clients DESTINATION lib)

install(FILES
  BaseCounterMonitor.h
  MinionClient.h
  StatCache.h
  DESTINATION include/e2e/clients
)
