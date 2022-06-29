# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-minion)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(NL_DRIVER_IF_INC nl-driver-if)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${NL_DRIVER_IF_INC})

find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(RPCMETADATA rpcmetadata PATHS)
find_package(Boost
  COMPONENTS
    context
    filesystem
    system
  REQUIRED)
find_library(FOLLY folly PATHS)
find_library(ZMQ zmq)
find_library(OPENRLIB openrlib)
find_library(RE2 re2)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)
find_library(FB-FW-IF fb-fw-if)
find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(E2E-DRIVER-IF e2e-driver-if)
find_library(CURL curl)
find_library(SODIUM sodium)
find_library(NL3 nl-3)
find_library(NLGENL3 nl-genl-3)
find_library(GPSD gps)
find_library(LIBTORRENT torrent-rasterbar)
find_library(LIBFMT fmt)

set(cpu ${CMAKE_SYSTEM_PROCESSOR})
if (cpu STREQUAL "arm")
  add_definitions(-DARM_ARCH)
elseif (cpu STREQUAL "aarch64")
  add_definitions(-DARM_ARCH)
else ()
  add_definitions(-Dx86_ARCH)
endif ()

# Netlink Driver Interface

# We have a few header dependencies, find the path include them
find_path(LIBNL3-HEADERS libnl3/netlink/netlink.h)

add_library(e2e-minion
  BgpUtils.cpp
  Broker.cpp
  DriverApp.cpp
  IgnitionApp.cpp
  OpenrClientApp.cpp
  StatusApp.cpp
  TrafficApp.cpp
  Minion.cpp
  MinionApp.cpp
  NeighborUtils.cpp
  UpgradeApp.cpp
  ConfigApp.cpp
  UpgradeStateCache.cpp
  SharedObjects.cpp
)

target_include_directories(e2e-minion PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(e2e-minion
  ${E2E-DRIVER-IF}
  ${E2E-COMMON}
  ${E2E-IF}
  ${OPENRLIB}
  ${RE2}
  ${FBZMQ}
  ${MONITORCPP2}
  ${GLOG}
  ${GFLAGS}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${RPCMETADATA}
  ${Boost_LIBRARIES}
  ${FOLLY}
  ${FB-FW-IF}
  ${CURL}
  ${ZMQ}
  ${SODIUM}
  ${LIBTORRENT}
  ${LIBFMT}
  ${NLGENL3}
  ${NL3}
  ${GPSD}
  -lpthread
  -lcrypto
  -lre2
)

install(TARGETS e2e-minion DESTINATION lib)

add_executable(e2e_minion
  Minion.cpp
)

target_include_directories(e2e_minion PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(e2e_minion
  e2e-minion
)

install(TARGETS e2e_minion DESTINATION sbin)

option(BUILD_TESTS "BUILD_TESTS" ON)
if (BUILD_TESTS)
  # e2e minion test
  enable_testing()

  find_library(GMOCK gmock)
  find_library(GMOCK_MAIN gmock_main)
  find_library(GTEST gtest)

  add_library(e2e_minion_test_util
    tests/MinionFixture.cpp
  )

  target_link_libraries(e2e_minion_test_util
    ${GMOCK}
    ${GMOCK_MAIN}
    ${GTEST}
    e2e-minion
  )
  target_include_directories(e2e_minion_test_util PRIVATE
    ${LIBNL3-HEADERS}/libnl3
  )

  add_executable(minion_status_app_test tests/MinionStatusAppTest.cpp)
  target_link_libraries(minion_status_app_test e2e_minion_test_util)

  add_executable(minion_ignition_app_test tests/MinionIgnitionAppTest.cpp)
  target_link_libraries(minion_ignition_app_test e2e_minion_test_util)

  add_executable(minion_driver_app_test tests/MinionDriverAppTest.cpp)
  target_link_libraries(minion_driver_app_test e2e_minion_test_util)

  add_test(MinionStatusAppTest minion_status_app_test)
  add_test(MinionIgnitionAppTest minion_ignition_app_test)
  add_test(MinionDriverAppTest minion_driver_app_test)

  install(TARGETS
    minion_status_app_test
    minion_ignition_app_test
    minion_driver_app_test
    DESTINATION sbin/tests/e2e)
endif ()
