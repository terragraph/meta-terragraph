# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(THRIFT_SYS_INC openr/if/gen-cpp2)

include_directories(${CMAKE_SOURCE_DIR}/..)
include_directories(${THRIFT_SYS_INC})

find_library(OPENRLIB openrlib)
find_library(CURL curl)
find_library(E2E-DRIVER-IF e2e-driver-if)
find_library(E2E-MINION e2e-minion)
find_library(FB-FW-IF fb-fw-if)
find_library(FOLLY folly PATHS)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(PROTOCOL protocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(FBZMQ fbzmq)
find_library(ZMQ zmq)
find_library(SODIUM sodium)
find_library(NL3 nl-3)
find_library(NLGENL3 nl-genl-3)

option(BUILD_LOCAL "BUILD_LOCAL" ON)
if (BUILD_LOCAL)
  # these items are built directly from the tree
  set(E2E-COMMON e2e-common)
  set(E2E-CONTROLLER e2e-controller)
  set(E2E-IF e2e-if)
  add_subdirectory(if)
  add_subdirectory(common)
  add_subdirectory(controller)
else()
  # these items built separately and found in install locations
  find_library(E2E-COMMON e2e-common)
  find_library(E2E-CONTROLLER e2e-controller)
  find_library(E2E-IF e2e-if)
endif()
