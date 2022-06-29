/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <optional>

#include "DriverNlMessage.h"

namespace facebook {
namespace terragraph {

/**
 * Virtual base class for netlink socket implementations.
 */
class BaseNetlinkSocket {
 public:
  virtual ~BaseNetlinkSocket() {}

  /** Connect the netlink socket. */
  virtual void connect() = 0;

  /** Return the file descriptor of the backing netlink socket. */
  virtual int getSocketFd() const = 0;

  /** Read a message from the socket, returning std::nullopt upon error. */
  virtual std::optional<DriverNlMessage> getMessage() = 0;

  /** Send a message on the socket. */
  virtual void sendMessage(const DriverNlMessage& message) = 0;
};

} // namespace terragraph
} // namespace facebook
