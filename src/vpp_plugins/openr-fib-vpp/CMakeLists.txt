# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(openr-fib-vpp)

find_package(VPP)
find_package(Boost
  COMPONENTS
    system
  REQUIRED
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

include_directories(${CMAKE_BINARY_DIR})

find_library(OPENRLIB openrlib)

find_library(FOLLY folly PATHS)
find_library(LIBFMT fmt)
find_library(GFLAGS gflags)
find_library(GLOG glog)
find_library(SODIUM sodium)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(ASYNC async PATHS)
find_library(CONCURRENCY concurrency PATHS)
find_library(FB303 fb303)
find_library(FB303_THRIFT_CPP fb303_thrift_cpp)


add_executable(fib_vpp
  VppClient.cpp
  VppFibAgent.cpp
  VppFibHandler.cpp
)

target_include_directories(fib_vpp
  PRIVATE
  ${CMAKE_BINARY_DIR}
)

target_link_libraries(fib_vpp
  ${Boost_LIBRARIES}
  ${OPENRLIB}
  ${GLOG}
  ${GFLAGS}
  ${FOLLY}
  ${LIBFMT}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${ASYNC}
  ${CONCURRENCY}
  ${SODIUM}
  ${FB303}
  ${FB303_THRIFT_CPP}
  -lpthread
  vapiclient
)

install(TARGETS
  fib_vpp
  DESTINATION sbin
)
