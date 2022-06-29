#
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# This source code is licensed under the MIT license found in the
# LICENSE file in the root directory of this source tree.
#

cmake_minimum_required(VERSION 3.12)
project(fb-fw-if)

include_directories(${CMAKE_SOURCE_DIR})

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Werror")

add_library(fb-fw-if
  fb_tg_fw_param_pack.c
)

target_link_libraries(fb-fw-if)

install(TARGETS fb-fw-if DESTINATION lib)

install(FILES
  fb_tg_fw_pt_if.h
  fb_tg_fw_param_pack.h
  DESTINATION include/fb-fw-if
)
