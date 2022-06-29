/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseNetlinkSocket.h"

namespace facebook {
namespace terragraph {

/**
 * Mock netlink socket for X86 architectures, using eventfd.
 *
 * This is mainly used for unit tests.
 */
class X86NetlinkSocket final : public BaseNetlinkSocket {
 public:
  X86NetlinkSocket();

  // ---- override methods ----

  void connect() override;
  int getSocketFd() const override;
  std::optional<DriverNlMessage> getMessage() override;
  void sendMessage(const DriverNlMessage& message) override;

 private:
  /** Send a signal on driverEventFd_, notifying the listener. */
  void signalDataReady() const;

  /** Local eventfd for signaling the driver response. */
  int driverEventFd_{-1};

  /** The driver response. */
  DriverNlMessage driverResp_;
};

} // namespace terragraph
} // namespace facebook
