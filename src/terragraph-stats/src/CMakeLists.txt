# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(stats)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(THRIFT_SYS_INC fbzmq/service/if/gen-cpp2)

include_directories(${CMAKE_SOURCE_DIR})
include_directories(${THRIFT_SYS_INC})

set(cpu ${CMAKE_SYSTEM_PROCESSOR})

find_library(GLOG glog)
find_library(GFLAGS gflags)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(ASYNC async PATHS)
find_library(RPCMETADATA rpcmetadata PATHS)
find_package(Boost
  COMPONENTS
    context
    filesystem
    system
  REQUIRED)
find_library(FOLLY folly PATHS)
find_library(ZMQ zmq)
find_library(SODIUM sodium)
find_library(FBZMQ fbzmq)
find_library(MONITORCPP2 monitor_cpp2)
find_library(OPENRLIB openrlib)
find_library(E2E-IF e2e-if)
find_library(E2E-COMMON e2e-common)
find_library(E2E-CLIENTS e2e-clients)
find_library(STATS-IF stats-if)
find_library(STATS-COMMON stats-common)
find_library(CURL curl)
find_library(LMSENSORS sensors)
find_library(SIGAR sigar)
find_library(RDKAFKA rdkafka)
find_library(CPPKAFKA cppkafka)

# Build stats agent

add_library(stats_agent_lib
  agent/LocalStatsFetcher.cpp
  agent/NetworkCounters.cpp
  agent/ProcessCounters.cpp
  agent/BasePublisher.cpp
  agent/GraphPublisher.cpp
  agent/KafkaPublisher.cpp
  agent/NmsPublisher.cpp
  agent/OpenrCounters.cpp
  agent/SensorCounters.cpp
  agent/SharedObjects.cpp
  agent/SystemCounters.cpp
  agent/VppCounters.cpp
  agent/ZmqCounterUtils.h
  agent/InputListener.cpp
)

target_link_libraries(stats_agent_lib
  ${E2E-CLIENTS}
  ${E2E-IF}
  ${E2E-COMMON}
  ${STATS-COMMON}
  ${STATS-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${MONITORCPP2}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${ASYNC}
  ${RPCMETADATA}
  ${Boost_LIBRARIES}
  ${FOLLY}
  ${CURL}
  ${SIGAR}
  ${LMSENSORS}
  ${ZMQ}
  ${GLOG}
  ${GFLAGS}
  ${SODIUM}
  ${RDKAFKA}
  ${CPPKAFKA}
  -lpthread
  -lcrypto
)

add_executable(stats_agent
  agent/Stats.cpp
)

target_link_libraries(stats_agent
  stats_agent_lib
)

install(TARGETS stats_agent DESTINATION sbin)

# Build nms aggregator

add_library(nms_aggregator_lib
  aggregator/AggrApp.cpp
  aggregator/Broker.cpp
  aggregator/ConfigApp.cpp
  aggregator/SharedObjects.cpp
  aggregator/StatsApp.cpp
  aggregator/StatusApp.cpp
)

target_link_libraries(nms_aggregator_lib
  ${STATS-COMMON}
  ${STATS-IF}
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
  ${ASYNC}
  ${ZMQ}
  ${SODIUM}
  ${FOLLY}
  ${CURL}
  -lpthread
)

add_executable(nms_aggregator
  aggregator/Aggregator.cpp
)

target_link_libraries(nms_aggregator
  nms_aggregator_lib
)

install(TARGETS nms_aggregator DESTINATION sbin)

# Build LogTail

add_library(logtail_lib
  logtail/EventParser.cpp
  logtail/LogPublisher.cpp
  logtail/LogTailer.cpp
  logtail/TailAgent.cpp
)

target_link_libraries(logtail_lib
  ${E2E-COMMON}
  ${E2E-IF}
  ${STATS-COMMON}
  ${STATS-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${MONITORCPP2}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${Boost_LIBRARIES}
  ${FOLLY}
  ${CURL}
  ${ZMQ}
  ${SODIUM}
  ${GLOG}
  ${GFLAGS}
  -lpthread
  -lcrypto
)

add_executable(logtail
  logtail/Main.cpp
)

target_link_libraries(logtail
  logtail_lib
)

install(TARGETS logtail DESTINATION sbin)

# snmp agent
add_library(snmp_agent_lib
  snmp/Access.cpp
  snmp/Agent.cpp
  snmp/MinionClient.cpp
  snmp/StatsSubscriber.cpp
  snmp/StatCache.cpp
)

target_link_libraries(snmp_agent_lib
  ${E2E-IF}
  ${E2E-COMMON}
  ${STATS-IF}
  ${OPENRLIB}
  ${FBZMQ}
  ${MONITORCPP2}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${ASYNC}
  ${FOLLY}
  ${ZMQ}
  ${GLOG}
  ${GFLAGS}
  -lnetsnmp
  -lnetsnmpagent
  -lpthread
)

add_executable(snmp_agent
  snmp/SNMPAgent.cpp
)

target_link_libraries(snmp_agent
  snmp_agent_lib
)

install(TARGETS snmp_agent DESTINATION sbin)
# NMS Tests

enable_testing()

find_library(GTEST gtest)

add_library(test_util
  tests/AgentFixture.cpp
)
target_link_libraries(test_util
  nms_aggregator_lib
  stats_agent_lib
  ${GTEST}
)
target_include_directories(test_util PRIVATE
  ${LIBNL3-HEADERS}/libnl3
)

add_executable(agent_nms_publisher_test
  agent/NmsPublisher.cpp
  tests/AgentNmsPublisherTest.cpp
)
target_link_libraries(agent_nms_publisher_test test_util)

add_test(AgentNmsPublisherTest agent_nms_publisher_test)

install(TARGETS agent_nms_publisher_test DESTINATION sbin/tests/nms)
