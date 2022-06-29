# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(api)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

include_directories(${CMAKE_SOURCE_DIR})
include_directories($ENV{STAGING_INCDIR}/)
include_directories($ENV{STAGING_INCDIR}/fbzmq/service/if/gen-cpp2)
include_directories($ENV{STAGING_INCDIR}/openr/if/gen-cpp2)

set(cpu ${CMAKE_SYSTEM_PROCESSOR})

find_library(API-IF api-if)
find_library(E2E-COMMON e2e-common)
find_library(E2E-IF e2e-if)
find_library(STATS-COMMON stats-common)
find_library(STATS-IF stats-if)
find_library(OPENRLIB openrlib)
find_library(PROXYGEN-HTTP-SERVER proxygenhttpserver)
find_library(PROXYGEN proxygen)
find_library(WANGLE wangle)
find_library(FBZMQ fbzmq)
find_library(ZMQ zmq)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)
find_library(ASYNC async PATHS)
find_library(FIZZ fizz)
find_library(FOLLY folly PATHS)
find_library(SODIUM sodium)
find_library(ZSTD zstd)
find_library(LIBZ z)
find_library(GLOG glog)
find_library(GFLAGS gflags)

# Build api service

add_library(api_service_lib
  service/ApiClient.cpp
  service/AuditLogger.cpp
  service/RequestHandler.cpp
  service/Service.cpp
  service/StaticHandler.cpp
  service/StreamApiClient.cpp
  service/Streamer.cpp
  service/StreamRequestHandler.cpp
)

target_link_libraries(api_service_lib
  ${API-IF}
  ${E2E-COMMON}
  ${E2E-IF}
  ${STATS-COMMON}
  ${STATS-IF}
  ${OPENRLIB}
  ${PROXYGEN-HTTP-SERVER}
  ${PROXYGEN}
  ${WANGLE}
  ${FBZMQ}
  ${ZMQ}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  ${ASYNC}
  ${FIZZ}
  ${FOLLY}
  ${SODIUM}
  ${ZSTD}
  ${LIBZ}
  ${GLOG}
  ${GFLAGS}
  -lboost_filesystem
  -lboost_iostreams
  -lboost_system
  -lpthread
  -lcrypto
)

add_executable(api_service
  service/Service.cpp
)

target_link_libraries(api_service
  api_service_lib
)

install(TARGETS api_service DESTINATION sbin)
