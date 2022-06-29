# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

cmake_minimum_required(VERSION 3.12)
project(e2e-if)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

if (ASAN)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address")
  set(CMAKE_CXX_LINK_FLAGS "${CMAKE_CXX_LINK_FLAGS} -fsanitize=address")
endif()

find_path(THRIFT_SYS_INC openr/if/gen-cpp2)
include_directories(${THRIFT_SYS_INC})

find_package(Python COMPONENTS Development)
find_library(THRIFT thrift-core PATHS)
find_library(THRIFTCPP2 thriftcpp2 PATHS)
find_library(THRIFTPROTOCOL thriftprotocol PATHS)
find_library(TRANSPORT transport PATHS)

# fbthrift
find_program(THRIFT1 thrift1)
find_path(THRIFTINCLUDE thrift/templates)
set(THRIFTTEMPLATES ${THRIFTINCLUDE}/thrift/templates)
set(THRIFTC ${THRIFT1} -I ${PROJECT_SOURCE_DIR} -I ${THRIFT_SYS_INC}
  --gen py:)
# Pass deprecated_public_fields to the fbthrift cpp2 generator. The flag
# disables new `_ref()` accessor semantics for fbthrift cpp2 datastructures that
# we do not use. We also pass no_metadata to remove unnecessary cpp2 logic we
# don't use and contributes unnecessary symbols to object files.
set(THRIFTC2
  ${THRIFT1} -I ${PROJECT_SOURCE_DIR} -I ${THRIFT_SYS_INC}
  --gen mstch_cpp2:deprecated_public_fields,no_metadata,include_prefix=e2e/if,)

# apache thrift (because fbthrift does have lua generator)
find_program(ATHRIFT thrift)
set(ATHRIFTC ${ATHRIFT} -I ${PROJECT_SOURCE_DIR} -I ${THRIFT_SYS_INC} --gen lua)

set(THRIFT_DIR ${CMAKE_BINARY_DIR}/thrift/e2e/if)
file(MAKE_DIRECTORY ${THRIFT_DIR})
include_directories(${CMAKE_BINARY_DIR}/thrift)

add_custom_command(
  OUTPUT
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_constants.h
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_data.cpp
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_data.h
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_types.cpp
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/BWAllocation_types.h
    ${THRIFT_DIR}/gen-cpp2/Controller_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/Controller_constants.h
    ${THRIFT_DIR}/gen-cpp2/Controller_data.cpp
    ${THRIFT_DIR}/gen-cpp2/Controller_data.h
    ${THRIFT_DIR}/gen-cpp2/Controller_types.cpp
    ${THRIFT_DIR}/gen-cpp2/Controller_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/Controller_types.h
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_constants.h
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_data.cpp
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_data.h
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_types.cpp
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/DriverMessage_types.h
    ${THRIFT_DIR}/gen-cpp2/Event_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/Event_constants.h
    ${THRIFT_DIR}/gen-cpp2/Event_data.cpp
    ${THRIFT_DIR}/gen-cpp2/Event_data.h
    ${THRIFT_DIR}/gen-cpp2/Event_types.cpp
    ${THRIFT_DIR}/gen-cpp2/Event_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/Event_types.h
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_constants.h
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_data.cpp
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_data.h
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_types.cpp
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/FwOptParams_types.h
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_constants.h
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_data.cpp
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_data.h
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_types.cpp
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/NodeConfig_types.h
    ${THRIFT_DIR}/gen-cpp2/PassThru_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/PassThru_constants.h
    ${THRIFT_DIR}/gen-cpp2/PassThru_data.cpp
    ${THRIFT_DIR}/gen-cpp2/PassThru_data.h
    ${THRIFT_DIR}/gen-cpp2/PassThru_types.cpp
    ${THRIFT_DIR}/gen-cpp2/PassThru_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/PassThru_types.h
    ${THRIFT_DIR}/gen-cpp2/Topology_constants.cpp
    ${THRIFT_DIR}/gen-cpp2/Topology_constants.h
    ${THRIFT_DIR}/gen-cpp2/Topology_data.cpp
    ${THRIFT_DIR}/gen-cpp2/Topology_data.h
    ${THRIFT_DIR}/gen-cpp2/Topology_types.cpp
    ${THRIFT_DIR}/gen-cpp2/Topology_types_custom_protocol.h
    ${THRIFT_DIR}/gen-cpp2/Topology_types.h
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/ttypes.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/constants.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/__init__.py
    ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/ttypes.py
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Controller_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Controller_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.BWAllocation_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.BWAllocation_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.PassThru_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.PassThru_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.FwOptParams_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.FwOptParams_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.DriverMessage_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.DriverMessage_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Event_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Event_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Topology_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.Topology_ttypes.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.NodeConfig_constants.lua
    ${THRIFT_DIR}/gen-lua/terragraph_thrift.NodeConfig_ttypes.lua
  COMMAND
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/BWAllocation.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Controller.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/DriverMessage.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Event.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/PassThru.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/FwOptParams.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Topology.thrift \;
    ${THRIFTC2}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/NodeConfig.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Controller.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/BWAllocation.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/DriverMessage.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Event.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/PassThru.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/FwOptParams.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Topology.thrift \;
    ${THRIFTC}json -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/NodeConfig.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Controller.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/BWAllocation.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/DriverMessage.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Event.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/PassThru.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/FwOptParams.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/Topology.thrift \;
    ${ATHRIFTC} -o ${THRIFT_DIR} ${PROJECT_SOURCE_DIR}/NodeConfig.thrift \;
)

# Install thrift interfaces

add_library(e2e-if
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_data.cpp
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_types.cpp
  ${THRIFT_DIR}/gen-cpp2/Controller_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/Controller_data.cpp
  ${THRIFT_DIR}/gen-cpp2/Controller_types.cpp
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_data.cpp
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_types.cpp
  ${THRIFT_DIR}/gen-cpp2/Event_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/Event_data.cpp
  ${THRIFT_DIR}/gen-cpp2/Event_types.cpp
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_data.cpp
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_types.cpp
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_data.cpp
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_types.cpp
  ${THRIFT_DIR}/gen-cpp2/PassThru_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/PassThru_data.cpp
  ${THRIFT_DIR}/gen-cpp2/PassThru_types.cpp
  ${THRIFT_DIR}/gen-cpp2/Topology_constants.cpp
  ${THRIFT_DIR}/gen-cpp2/Topology_data.cpp
  ${THRIFT_DIR}/gen-cpp2/Topology_types.cpp
)

target_link_libraries(e2e-if
  ${THRIFT}
  ${THRIFTCPP2}
  ${THRIFTPROTOCOL}
  ${TRANSPORT}
  -lpthread
)

install(TARGETS e2e-if DESTINATION lib)

install(FILES
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_constants.h
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_data.h
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/BWAllocation_types.h
  ${THRIFT_DIR}/gen-cpp2/Controller_constants.h
  ${THRIFT_DIR}/gen-cpp2/Controller_data.h
  ${THRIFT_DIR}/gen-cpp2/Controller_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/Controller_types.h
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_constants.h
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_data.h
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/DriverMessage_types.h
  ${THRIFT_DIR}/gen-cpp2/Event_constants.h
  ${THRIFT_DIR}/gen-cpp2/Event_data.h
  ${THRIFT_DIR}/gen-cpp2/Event_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/Event_types.h
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_constants.h
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_data.h
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/FwOptParams_types.h
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_constants.h
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_data.h
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/NodeConfig_types.h
  ${THRIFT_DIR}/gen-cpp2/PassThru_constants.h
  ${THRIFT_DIR}/gen-cpp2/PassThru_data.h
  ${THRIFT_DIR}/gen-cpp2/PassThru_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/PassThru_types.h
  ${THRIFT_DIR}/gen-cpp2/Topology_constants.h
  ${THRIFT_DIR}/gen-cpp2/Topology_data.h
  ${THRIFT_DIR}/gen-cpp2/Topology_types_custom_protocol.h
  ${THRIFT_DIR}/gen-cpp2/Topology_types.h
  DESTINATION include/e2e/if/gen-cpp2
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
  ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/BWAllocation/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/BWAllocation/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Controller/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/Controller/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/DriverMessage/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/DriverMessage/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Event/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/Event/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/FwOptParams/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/FwOptParams/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/NodeConfig/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/NodeConfig/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/PassThru/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/PassThru/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/constants.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/__init__.py
  ${THRIFT_DIR}/gen-py/terragraph_thrift/Topology/ttypes.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/Topology/
)

install(FILES
  ${THRIFT_DIR}/gen-py/terragraph_thrift/__init__.py
  DESTINATION ${Python_SITEDIR}/terragraph_thrift/
)

#
# Install lua code to "terragraph_thrift" subdirectory
#
set(Lua_LIBDIR lib/lua/5.2/terragraph_thrift)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Controller_constants.lua
  RENAME Controller_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Controller_ttypes.lua
  RENAME Controller_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.BWAllocation_constants.lua
  RENAME BWAllocation_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.BWAllocation_ttypes.lua
  RENAME BWAllocation_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.PassThru_constants.lua
  RENAME PassThru_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.PassThru_ttypes.lua
  RENAME PassThru_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.FwOptParams_constants.lua
  RENAME FwOptParams_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.FwOptParams_ttypes.lua
  RENAME FwOptParams_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.DriverMessage_constants.lua
  RENAME DriverMessage_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.DriverMessage_ttypes.lua
  RENAME DriverMessage_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Event_constants.lua
  RENAME Event_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Event_ttypes.lua
  RENAME Event_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Topology_constants.lua
  RENAME Topology_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.Topology_ttypes.lua
  RENAME Topology_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.NodeConfig_constants.lua
  RENAME NodeConfig_constants.lua DESTINATION ${Lua_LIBDIR}/
)
install(FILES
  ${THRIFT_DIR}/gen-lua/terragraph_thrift.NodeConfig_ttypes.lua
  RENAME NodeConfig_ttypes.lua DESTINATION ${Lua_LIBDIR}/
)
