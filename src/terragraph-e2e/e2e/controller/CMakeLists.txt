# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-controller)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

include_directories(${PROJECT_SOURCE_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)

find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_package(Boost
  COMPONENTS
    context
    filesystem
    system
  REQUIRED)
find_library(FOLLY folly PATHS)
find_library(ZMQ zmq)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)
find_library(OPENRLIB openrlib)
find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(CURL curl)
find_library(SODIUM sodium)
find_library(LIBTORRENT torrent-rasterbar)
find_library(LIBFMT fmt)

add_library(e2e-controller
  BinaryStarApp.cpp
  BinaryStarFsm.cpp
  Broker.cpp
  ConfigApp.cpp
  ConfigHelper.cpp
  CtrlApp.cpp
  GraphHelper.cpp
  IgnitionApp.cpp
  IgnitionAppUtil.cpp
  ScanApp.cpp
  ScanScheduler.cpp
  SchedulerApp.cpp
  SharedObjects.cpp
  StatusApp.cpp
  TopologyApp.cpp
  TopologyBuilderApp.cpp
  TrafficApp.cpp
  UpgradeApp.cpp
  UpgradeAppUtil.cpp
  ZapHandler.cpp
  algorithms/BandwidthAllocationHelper.cpp
  algorithms/ChannelHelper.cpp
  algorithms/ControlSuperframeHelper.cpp
  algorithms/InterferenceHelper.cpp
  algorithms/GolayHelper.cpp
  algorithms/LinkGroupHelper.cpp
  algorithms/OccSolver.cpp
  algorithms/PolarityHelper.cpp
  prefix-allocators/BasePrefixAllocator.cpp
  prefix-allocators/CentralizedPrefixAllocator.cpp
  prefix-allocators/DeterministicPrefixAllocator.cpp
  prefix-allocators/PrefixZone.cpp
  topology/RoutesHelper.cpp
  topology/TopologyBuilder.cpp
  topology/TopologyWrapper.cpp
)

target_link_libraries(e2e-controller
  ${E2E-COMMON}
  ${E2E-IF}
  ${OPENRLIB}
  ${Boost_LIBRARIES}
  ${FBZMQ}
  ${MONITORCPP2}
  ${GLOG}
  ${GFLAGS}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${FOLLY}
  ${CURL}
  ${ZMQ}
  ${SODIUM}
  ${LIBTORRENT}
  ${LIBFMT}
  -lpthread
  -lcrypto
)

install(TARGETS e2e-controller DESTINATION lib)

add_executable(e2e_controller
  Controller.cpp
)

target_link_libraries(e2e_controller
  e2e-controller
)

install(TARGETS e2e_controller DESTINATION sbin)

option(BUILD_TESTS "BUILD_TESTS" ON)
if (BUILD_TESTS)
  # e2e controller test
  enable_testing()

  find_library(GMOCK gmock)
  find_library(GMOCK_MAIN gmock_main)
  find_library(GTEST gtest)

  add_library(e2e_controller_test_util
    tests/CtrlFixture.cpp
  )
  target_link_libraries(e2e_controller_test_util
    ${GMOCK}
    ${GMOCK_MAIN}
    ${GTEST}
    e2e-controller
  )

  add_executable(ignition_app_test tests/IgnitionAppTest.cpp)
  target_link_libraries(ignition_app_test e2e_controller_test_util)

  add_executable(topology_app_test tests/TopologyAppTest.cpp)
  target_link_libraries(topology_app_test e2e_controller_test_util)

  add_executable(status_app_test tests/StatusAppTest.cpp)
  target_link_libraries(status_app_test e2e_controller_test_util)

  add_executable(upgrade_app_test tests/UpgradeAppTest.cpp)
  target_link_libraries(upgrade_app_test e2e_controller_test_util)

  add_executable(ignition_app_util_test tests/IgnitionAppUtilTest.cpp)
  target_link_libraries(ignition_app_util_test e2e_controller_test_util)

  add_executable(upgrade_app_util_test tests/UpgradeAppUtilTest.cpp)
  target_link_libraries(upgrade_app_util_test e2e_controller_test_util)

  add_executable(config_app_test tests/ConfigAppTest.cpp)
  target_link_libraries(config_app_test e2e_controller_test_util)

  add_executable(tunnel_config_test tests/TunnelConfigTest.cpp)
  target_link_libraries(tunnel_config_test e2e_controller_test_util)

  add_executable(centralized_prefix_allocator_test prefix-allocators/tests/CentralizedPrefixAllocatorTest.cpp)
  target_link_libraries(centralized_prefix_allocator_test e2e_controller_test_util)

  add_executable(deterministic_prefix_allocator_test prefix-allocators/tests/DeterministicPrefixAllocatorTest.cpp)
  target_link_libraries(deterministic_prefix_allocator_test e2e_controller_test_util)

  add_executable(topology_wrapper_test topology/tests/TopologyWrapperTest.cpp)
  target_link_libraries(topology_wrapper_test e2e_controller_test_util)

  add_executable(occ_solver_test algorithms/tests/OccSolverTest.cpp)
  target_link_libraries(occ_solver_test e2e_controller_test_util)

  add_executable(polarity_helper_test algorithms/tests/PolarityHelperTest.cpp)
  target_link_libraries(polarity_helper_test e2e_controller_test_util)

  add_executable(control_superframe_helper_test algorithms/tests/ControlSuperframeHelperTest.cpp)
  target_link_libraries(control_superframe_helper_test
    e2e_controller_test_util
  )

  add_test(IgnitionAppTest ignition_app_test)
  add_test(TopologyAppTest topology_app_test)
  add_test(StatusAppTest status_app_test)
  add_test(UpgradeAppTest upgrade_app_test)
  add_test(IgnitionAppUtilTest ignition_app_util_test)
  add_test(UpgradeAppUtilTest upgrade_app_util_test)
  add_test(ConfigAppTest config_app_test)
  add_test(TunnelConfigTest tunnel_config_test)
  add_test(TopologyWrapperTest topology_wrapper_test)
  add_test(CentralizedPrefixAllocatorTest centralized_prefix_allocator_test)
  add_test(DeterministicPrefixAllocatorTest deterministic_prefix_allocator_test)
  add_test(OccSolverTest occ_solver_test)
  add_test(PolarityHelperTest polarity_helper_test)
  add_test(ControlSuperframeHelperTest control_superframe_helper_test)

  install(TARGETS
    config_app_test
    tunnel_config_test
    ignition_app_test
    ignition_app_util_test
    upgrade_app_util_test
    status_app_test
    topology_app_test
    upgrade_app_test
    topology_wrapper_test
    centralized_prefix_allocator_test
    deterministic_prefix_allocator_test
    occ_solver_test
    polarity_helper_test
    control_superframe_helper_test
    DESTINATION sbin/tests/e2e)
endif ()
