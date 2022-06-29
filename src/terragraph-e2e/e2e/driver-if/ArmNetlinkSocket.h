/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseNetlinkSocket.h"
#include "NetlinkMessage.h"

namespace facebook {
namespace terragraph {

/**
 * Simple wrapper over netlink for ARM architectures.
 *
 * This class will throw std::runtime_error upon encountering any netlink error.
 */
class ArmNetlinkSocket final : public BaseNetlinkSocket {
 public:
  /** Constructor. This allocates the underlying netlink socket. */
  ArmNetlinkSocket();

  /** Destructor. This frees the underlying netlink socket. */
  ~ArmNetlinkSocket();

  // ---- override methods ----

  void connect() override;

  // Owners' responsibilty to poll on our socket
  int getSocketFd() const override;

  // Daemon polling on socket claims data is ready for reading
  // It will call this API to retrieve the message
  std::optional<DriverNlMessage> getMessage() override;

  void sendMessage(const DriverNlMessage& message) override;

 private:
  /** \{ */
  ArmNetlinkSocket(const ArmNetlinkSocket&) = delete;
  ArmNetlinkSocket& operator=(const ArmNetlinkSocket&) = delete;
  /** \} */

  /**
   * The netlink socket callback, invoked automatically by libnl.
   *
   * This will put the netlink message into driverNlMessage_.
   *
   * NOTE: This is static and works for single-threaded use only!
   */
  static int recvFunc_(struct nl_msg* msg, void* arg) noexcept;

  /** The last unread netlink message created via recvFunc_(). */
  static DriverNlMessage driverNlMessage_;

  /** The netlink family identifier. */
  int socketFamilyId_{-1};

  /** The underlying netlink socket. */
  struct nl_sock* socket_{nullptr};
};
} // namespace terragraph
} // namespace facebook
