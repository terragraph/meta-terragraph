# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

include_directories(${CMAKE_SOURCE_DIR})

# for generated API headers:
include_directories(${CMAKE_BINARY_DIR})

add_vpp_plugin(tgcfg
  SOURCES
  tgcfg_api.c
  tgcfg_cli.c
  tgcfg_main.c
  tgcfg_node.c
  tgcfg.h

  API_FILES
  tgcfg.api

  INSTALL_HEADERS
  tgcfg.h
  tgcfg_msg_enum.h
)
