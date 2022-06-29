# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(vpp-chaperone)

find_package(VPP)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -fpermissive")

include_directories(${CMAKE_BINARY_DIR})

find_library(FOLLY folly)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(NL3 nl-3)
find_library(NLROUTE3 nl-route-3)

find_path(LIBNL3-HEADERS libnl3/netlink/netlink.h)

add_executable(vpp_chaperone
  NetUtils.cpp
  VppClient.cpp
  VppChaperone.cpp
  VppConfigManager.cpp
)

target_include_directories(vpp_chaperone
  PRIVATE
  ${LIBNL3-HEADERS}/libnl3
  ${CMAKE_BINARY_DIR}
)

target_link_libraries(vpp_chaperone
  ${FOLLY}
  ${GLOG}
  ${GFLAGS}
  ${NLROUTE3}
  ${NL3}
  -lpthread
  -lboost_filesystem
  vapiclient
)

install(TARGETS
  vpp_chaperone
  DESTINATION sbin
)
