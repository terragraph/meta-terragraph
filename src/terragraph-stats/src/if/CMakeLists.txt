# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(stats-if)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

find_path(THRIFT_SYS_INC fbzmq/service/if/gen-cpp2)
include_directories(${THRIFT_SYS_INC})

find_library(FBZMQ fbzmq)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)

find_package(Python COMPONENTS Development)

# fbthrift
find_program(THRIFT1 thrift1)
find_path(THRIFTINCLUDE thrift/templates)
set(THRIFTTEMPLATES ${THRIFTINCLUDE}/thrift/templates)
set(THRIFTC ${THRIFT1} -I ${CMAKE_SOURCE_DIR} -I ${THRIFT_SYS_INC}
  --gen py:)
# Pass deprecated_public_fields to the fbthrift cpp2 generator. The flag
# disables new `_ref()` accessor semantics for fbthrift cpp2 datastructures that
# we do not use.
set(THRIFTC2
  ${THRIFT1} -I ${CMAKE_SOURCE_DIR} -I ${THRIFT_SYS_INC}
  --gen mstch_cpp2:deprecated_public_fields,include_prefix=stats/if,optionals,)

# apache thrift (because fbthrift does have lua generator)
find_program(ATHRIFT thrift)
set(ATHRIFTC ${ATHRIFT} -I ${PROJECT_SOURCE_DIR} -I ${THRIFT_SYS_INC} --gen lua)

set(THRIFT_DIR ${CMAKE_BINARY_DIR}/thrift/stats/if)
file(MAKE_DIRECTORY ${THRIFT_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)

add_custom_command(
  OUTPUT
    ${THRIFT_DIR}/gen-cpp2/Aggregator_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/Aggregator_constants.h
    ${THRIFT_DIR}/gen-cpp2/Aggregator_data.cpp
    ${THRIFT_DIR}/gen-cpp2/Aggregator_data.h
    ${THRIFT_DIR}/gen-cpp2/Aggregator_types.cpp
    ${THRIFT_DIR}/gen-cpp2/Aggregator_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/Aggregator_types.h
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/ttypes.py
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Aggregator_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Aggregator_ttypes.lua
  COMMAND
    ${THRIFTC2}json -o ${THRIFT_DIR} ${CMAKE_SOURCE_DIR}/Aggregator.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${CMAKE_SOURCE_DIR}/Aggregator.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Aggregator.thrift \;
)

# Install thrift interfaces

add_library(stats-if
  ${THRIFT_DIR}/gen-cpp2/Aggregator_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/Aggregator_data.cpp
  ${THRIFT_DIR}/gen-cpp2/Aggregator_types.cpp
)

target_link_libraries(stats-if
  ${FBZMQ}
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  -lpthread
)

install(TARGETS stats-if DESTINATION lib)

install(FILES
  ${THRIFT_DIR}/gen-cpp2/Aggregator_constants.h
  ${THRIFT_DIR}/gen-cpp2/Aggregator_data.h
  ${THRIFT_DIR}/gen-cpp2/Aggregator_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/Aggregator_types.h
  DESTINATION include/stats/if/gen-cpp2
)

#
# Python_SITELIB is an absolute path to the site-packages directory
# associated with the python interpreter we're running as part of the
# build process. (Ie, it contains build path artifacts.) But when
# installing python modules using the install(...  DESTINATION ... )
# cmake directive, we need to supply a path relative to the root of the
# image we're building. Since we can't use Python_SITELIB we'll define a
# new Python_SITEDIR variable that is relative to the image root.
#
set (Python_SITEDIR
  "lib/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages")

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Aggregator/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/Aggregator/
)

#
# Install lua code to "terragraph_thrift" subdirectory
#
set(Lua_LIBDIR lib/lua/5.2/terragraph_thrift)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Aggregator_constants.lua
  RENAME Aggregator_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Aggregator_ttypes.lua
  RENAME Aggregator_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
