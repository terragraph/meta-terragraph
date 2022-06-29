# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.

include_directories(${CMAKE_SOURCE_DIR})

# for generated API headers:
include_directories(${CMAKE_BINARY_DIR})

add_vpp_plugin(esmc
  SOURCES
  esmc.c
  node.c

  MULTIARCH_SOURCES
  node.c

  API_FILES
  esmc.api

  COMPONENT vpp-plugin-esmc
)
