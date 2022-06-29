# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(api-if)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)

find_program(THRIFT1 thrift1)
find_path(THRIFTINCLUDE thrift/templates)
set(THRIFTTEMPLATES ${THRIFTINCLUDE}/thrift/templates)
set(THRIFTC2
  ${THRIFT1} -I ${CMAKE_SOURCE_DIR}
  --gen mstch_cpp2:include_prefix=api/if,optionals,)
set(THRIFT_DIR ${CMAKE_BINARY_DIR}/thrift/api/if)
file(MAKE_DIRECTORY ${THRIFT_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)

add_custom_command(
  OUTPUT
    ${THRIFT_DIR}/gen-cpp2/Permissions_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/Permissions_constants.h
    ${THRIFT_DIR}/gen-cpp2/Permissions_data.cpp
    ${THRIFT_DIR}/gen-cpp2/Permissions_data.h
    ${THRIFT_DIR}/gen-cpp2/Permissions_types.cpp
    ${THRIFT_DIR}/gen-cpp2/Permissions_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/Permissions_types.h
  COMMAND
    ${THRIFTC2}json -I $ENV{STAGING_INCDIR}/
                    -o ${THRIFT_DIR} ${CMAKE_SOURCE_DIR}/Permissions.thrift \;
)

# Install thrift interfaces

add_library(api-if
  ${THRIFT_DIR}/gen-cpp2/Permissions_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/Permissions_data.cpp
  ${THRIFT_DIR}/gen-cpp2/Permissions_types.cpp
)

target_link_libraries(api-if
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  -lpthread
)

install(TARGETS api-if DESTINATION lib)

install(FILES
  ${THRIFT_DIR}/gen-cpp2/Permissions_constants.h
  ${THRIFT_DIR}/gen-cpp2/Permissions_data.h
  ${THRIFT_DIR}/gen-cpp2/Permissions_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/Permissions_types.h
  DESTINATION include/api/if/gen-cpp2
)
