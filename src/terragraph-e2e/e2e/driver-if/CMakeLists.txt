# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-driver-if)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")
# The backwards compatible antWgtCode definition triggers
# array bounds checking in newer GCC
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-array-bounds")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

option(WITH_GPSD "Compile in gpsd support" ON)

find_path(NL_DRIVER_IF_INC nl-driver-if)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${NL_DRIVER_IF_INC})

find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_package(Boost
  COMPONENTS
    filesystem
    system
  REQUIRED)
find_library(FOLLY folly PATHS)
find_library(ZMQ zmq)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)
find_library(OPENRLIB openrlib)
find_library(FB-FW-IF fb-fw-if)
find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(CURL curl)
find_library(SODIUM sodium)
find_library(NL3 nl-3)
find_library(NLGENL3 nl-genl-3)
if(WITH_GPSD)
  find_library(GPSD gps)
endif()

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

set(E2E_DRIVER_IF_SOURCES
  DriverIfUtil.cpp
  BaseDriverIf.cpp
  ArmDriverIf.cpp
  X86DriverIf.cpp
  ArmNetlinkSocket.cpp
  X86NetlinkSocket.cpp
  NetlinkMessage.cpp
  FbTgFwParam.cpp
  PassThru.cpp
  PTPClockHelper.cpp
)
if(WITH_GPSD)
  list(APPEND E2E_DRIVER_IF_SOURCES GpsdClient.cpp)
endif()

add_library(e2e-driver-if ${E2E_DRIVER_IF_SOURCES})

if(WITH_GPSD)
  target_compile_definitions(e2e-driver-if PRIVATE WITH_GPSD)
endif()

target_include_directories(e2e-driver-if PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(e2e-driver-if
  ${E2E-COMMON}
  ${E2E-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${MONITORCPP2}
  ${GLOG}
  ${GFLAGS}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${Boost_LIBRARIES}
  ${FOLLY}
  ${FB-FW-IF}
  ${ZMQ}
  ${SODIUM}
  ${CURL}
  ${NLGENL3}
  ${NL3}
  ${GPSD}
  -lpthread
)

install(TARGETS e2e-driver-if DESTINATION lib)

install(FILES
  DriverIfUtil.h
  ArmDriverIf.h
  BaseDriverIf.h
  X86DriverIf.h
  BaseNetlinkSocket.h
  ArmNetlinkSocket.h
  X86NetlinkSocket.h
  NetlinkMessage.h
  DriverNlMessage.h
  PTPClockHelper.h
  GpsdClient.h
  DESTINATION include/e2e/driver-if
)

add_executable(driver_if_daemon
  DriverIfDaemon.cpp
)

target_include_directories(driver_if_daemon PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

target_link_libraries(driver_if_daemon
  e2e-driver-if
)

install(TARGETS driver_if_daemon DESTINATION sbin)

option(BUILD_TESTS "BUILD_TESTS" ON)
if (BUILD_TESTS)
  # driver if test
  enable_testing()

  find_library(GMOCK gmock)
  find_library(GMOCK_MAIN gmock_main)
  find_library(GTEST gtest)

  add_executable(pass_thru_test tests/PassThruTest.cpp)
  target_link_libraries(pass_thru_test
    e2e-driver-if
    ${GTEST}
    -lpthread
  )

  add_executable(fw_param_test tests/FbTgFwParamTest.cpp)
  target_link_libraries(fw_param_test
    e2e-driver-if
    ${FB-FW-IF}
    ${GTEST}
    -lpthread
  )

  add_executable(driver_if_test tests/DriverIfTest.cpp)
  target_include_directories(driver_if_test PRIVATE
    ${LIBNL3-HEADERS}/libnl3
  )
  target_link_libraries(driver_if_test
    e2e-driver-if
    ${GTEST}
    -lpthread
  )

  add_test(PassThruTest pass_thru_test)
  add_test(FbTgFwParamTest fw_param_test)
  add_test(DriverIfTest driver_if_test)

  install(TARGETS pass_thru_test DESTINATION sbin/tests/e2e)
  install(TARGETS fw_param_test DESTINATION sbin/tests/e2e)
  install(TARGETS driver_if_test DESTINATION sbin/tests/e2e)
endif ()
