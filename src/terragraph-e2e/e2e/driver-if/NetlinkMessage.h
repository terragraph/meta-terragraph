/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

extern "C" {
#include <netlink/errno.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/genl.h>
#include <netlink/msg.h>
#include <netlink/netlink.h>
#include <netlink/socket.h>

#include <nl-driver-if/fb_tgd_nlsdn_common.h>
}

namespace facebook {
namespace terragraph {

/**
 * Thin wrapper around nl_msg.
 */
class NetlinkMessage {
 public:
  /** Constructor. This allocates the underlying nl_msg struct. */
  NetlinkMessage();

  /** Destructor. This frees the underlying nl_msg struct. */
  ~NetlinkMessage();

  /** \{ */
  NetlinkMessage(const NetlinkMessage&) = delete;
  NetlinkMessage& operator=(const NetlinkMessage&) = delete;
  /** \} */

  /** The underlying netlink message. */
  struct nl_msg* msg_;
};

} // namespace terragraph
} // namespace facebook
