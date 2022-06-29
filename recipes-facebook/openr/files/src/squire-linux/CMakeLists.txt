# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(openr-squire-linux)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR})
find_library(E2E-COMMON e2e-common)
find_library(E2E-IF e2e-if)
find_library(FBZMQ fbzmq)
find_library(ZMQ zmq)
find_library(SODIUM sodium)
find_library(NL3 nl-3)
find_library(NLROUTE3 nl-route-3)
find_library(NLGENL3 nl-genl-3)

find_package(Boost
  COMPONENTS
    context
    filesystem
    system
  REQUIRED)
find_library(FOLLY folly PATHS)
find_library(LIBFMT fmt)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(OPENRLIB openrlib)

find_path(LIBNL3-HEADERS libnl3/netlink/netlink.h)

add_library(squire-handler
  SquireHandler.cpp
  NetlinkSocket.cpp
)

target_include_directories(squire-handler
  PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(squire-handler
  ${E2E-COMMON}
  ${E2E-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${ZMQ}
  ${SODIUM}
  ${FOLLY}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${NLGENL3}
  ${NLROUTE3}
  ${NL3}
  ${Boost_LIBRARIES}
  ${LIBFMT}
  -lpthread
  -lcrypto
)

install(TARGETS squire-handler DESTINATION lib)

install(FILES
  SquireHandler.h
  DESTINATION include/openr/squire
)

add_executable(squire_linux
  SquireLinux.cpp
)

target_include_directories(squire_linux
  PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(squire_linux
  squire-handler
  ${OPENRLIB}
  ${FBZMQ}
  ${ZMQ}
  ${SODIUM}
  ${GLOG}
  ${GFLAGS}
  ${FOLLY}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  -lpthread
  ${NLGENL3}
  ${NLROUTE3}
  ${NL3}
  -lcrypto
)

install(TARGETS
  squire_linux
  DESTINATION sbin
)
