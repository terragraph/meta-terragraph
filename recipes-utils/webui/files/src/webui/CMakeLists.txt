# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(webui)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Werror")

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ffunction-sections -fdata-sections")
set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -Wl,--gc-sections")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(THRIFT_SYS_INC fbzmq/service/if/gen-cpp2)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${THRIFT_SYS_INC})

# Find uSockets and uWebSockets which both install in subdirectories.
find_path(USOCKETS_INC uSockets)
include_directories(${USOCKETS_INC}/uSockets)
find_path(UWEBSOCKETS_INC uWebSockets)
include_directories(${UWEBSOCKETS_INC}/uWebSockets)

find_library(GFLAGS gflags)
find_library(GLOG glog)
find_package(Boost
  COMPONENTS
    context
    filesystem
    system
  REQUIRED)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(FOLLY folly PATHS)
find_library(ZMQ zmq)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)
find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(E2E-CLIENTS e2e-clients)
find_library(STATS-IF stats-if)
find_library(STATS-COMMON stats-common)
find_library(OPENRLIB openrlib)
find_library(SSL ssl)
find_library(Z z)
# uSockets does not produce a shared object, use static lib
find_library(USOCKETS uSockets.a)

add_library(webui_lib
  Consts.cpp
  StatsSubscriber.cpp
  WebRoute.cpp
  WebUI.cpp
)

target_link_libraries(webui_lib
  ${E2E-CLIENTS}
  ${E2E-IF}
  ${E2E-COMMON}
  ${STATS-COMMON}
  ${STATS-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${MONITORCPP2}
  ${Boost_LIBRARIES}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${ASYNC}
  ${FOLLY}
  ${ZMQ}
  ${GLOG}
  ${GFLAGS}
  ${USOCKETS}
  ${SSL}
  ${Z}
  -lpthread
  -lcrypto
)

add_executable(webui
  WebUI.cpp
)

target_link_libraries(webui webui_lib)

install(TARGETS webui DESTINATION sbin)
