# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-common)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)

find_library(E2E-IF e2e-if)
find_library(FOLLY folly PATHS)
find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(FBZMQ fbzmq)
find_library(OPENRLIB openrlib)
find_library(CURL curl)

add_library(e2e-common
  CompressionUtil.cpp
  ConfigMetadata.cpp
  ConfigUtil.cpp
  Consts.cpp
  CurlUtil.cpp
  EventClient.cpp
  ExceptionHandler.cpp
  GpsClock.cpp
  IpUtil.cpp
  JsonUtils.cpp
  MacUtils.cpp
  Md5Utils.cpp
  NodeConfigWrapper.cpp
  NodeInfoWrapper.cpp
  OpenrUtils.cpp
  Progress.cpp
  SysUtils.cpp
  TestUtils.cpp
  TimeUtils.cpp
  UpgradeUtils.cpp
  UuidUtils.cpp
  SimpleGraph.cpp
  WatchdogUtils.cpp
)

target_link_libraries(e2e-common
  ${E2E-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${FOLLY}
  ${GLOG}
  ${GFLAGS}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${CURL}
  -lboost_filesystem
  -lboost_system
  -lpthread
  -lcrypto
  -ldouble-conversion
)

install(TARGETS e2e-common DESTINATION lib)

install(FILES
  CompressionUtil.h
  ConfigMetadata.h
  ConfigUtil.h
  Consts.h
  CurlUtil.h
  E2EConfigWrapper.h
  EventClient.h
  ExceptionHandler.h
  GpsClock.h
  IpUtil.h
  JsonUtils.h
  MacUtils.h
  Md5Utils.h
  NodeConfigWrapper.h
  NodeInfoWrapper.h
  OpenrUtils.h
  Progress.h
  SysUtils.h
  TestUtils.h
  TimeUtils.h
  UpgradeUtils.h
  UuidUtils.h
  SimpleGraph.h
  WatchdogUtils.h
  DESTINATION include/e2e/common
)

option(BUILD_TESTS "BUILD_TESTS" ON)
if (BUILD_TESTS)
  # unit tests
  enable_testing()

  find_library(GMOCK gmock)
  find_library(GMOCK_MAIN gmock_main)
  find_library(GTEST gtest)

  function(link_all_test_libs TARGET)
      target_link_libraries(${TARGET} ${GMOCK} ${GMOCK_MAIN} ${GTEST} e2e-common)
  endfunction(link_all_test_libs)

  add_executable(config_util_test tests/ConfigUtilTest.cpp)
  link_all_test_libs(config_util_test)

  add_executable(json_utils_test tests/JsonUtilsTest.cpp)
  link_all_test_libs(json_utils_test)

  add_executable(openr_utils_test tests/OpenrUtilsTest.cpp)
  link_all_test_libs(openr_utils_test)

  add_executable(ip_util_test tests/IpUtilTest.cpp)
  link_all_test_libs(ip_util_test)

  add_test(ConfigUtilTest config_util_test)
  add_test(JsonUtilsTest json_utils_test)
  add_test(OpenrUtilsTest openr_utils_test)
  add_test(IpUtilTest ip_util_test)

  install(TARGETS
    config_util_test
    json_utils_test
    openr_utils_test
    ip_util_test
    DESTINATION sbin/tests/e2e)
endif ()
