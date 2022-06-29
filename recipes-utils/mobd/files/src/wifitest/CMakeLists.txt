# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(wifitest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if ((${CMAKE_SYSTEM_NAME} MATCHES "Windows") OR
    (${CMAKE_SYSTEM_NAME} MATCHES "Darwin"))
  set(TG_PLATFORM FALSE)
else()
  set(TG_PLATFORM TRUE)
  add_compile_definitions(TG_PLATFORM)
endif()

include_directories(${CMAKE_BINARY_DIR})

find_library(GFLAGS gflags)
find_library(GLOG glog)
find_package(Protobuf REQUIRED)

include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

if (${TG_PLATFORM})
  find_library(FOLLY folly PATHS)
else()
  find_package(boost COMPONENTS system filesystem REQUIRED)
  find_package(openssl REQUIRED)
endif()

if(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
  add_compile_definitions(GLOG_NO_ABBREVIATED_SEVERITIES)
  add_compile_definitions(_WIN32_WINNT=0x0A00) # Target Windows 10
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
else()
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Werror")
endif()

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ../common/Message.proto)

set(SRC_WIFITEST main.cpp TestUtils.cpp)

set(LIB_COMMON ${GFLAGS} ${GLOG} ${Protobuf_LIBRARIES} -lmobile_common)

if (${TG_PLATFORM})
  add_executable(wifitest ${SRC_WIFITEST})
  target_link_libraries(wifitest ${LIB_COMMON} ${FOLLY} -lboost_system -lpthread -lcrypto)
else()
  add_executable(wifitest ${SRC_WIFITEST} ${SRC_COMMON})
  target_link_libraries(wifitest ${LIB_COMMON} ${Boost_LIBRARIES} OpenSSL::Crypto)
endif()

install(TARGETS wifitest DESTINATION sbin)
