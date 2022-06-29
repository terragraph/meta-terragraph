/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/zmq/Zmq.h>

namespace facebook {
namespace terragraph {

/**
 * This class implements a handler for the ZeroMQ Authentication Protocol (ZAP).
 *
 * This is currently being used only for retrieving client connection
 * information (IP addresses), and not for any actual security.
 *
 * Reference: http://rfc.zeromq.org/spec:27/ZAP
 */
class ZapHandler final : public fbzmq::ZmqEventLoop {
 public:
  /**
   * Constructor.
   *
   * This will bind all sockets.
   *
   * @param zmqContext the ZMQ context
   */
  explicit ZapHandler(fbzmq::Context& zmqContext);

 private:
  /**
   * Returns a metadata blob consisting of the given key/value pair in ZMTP 3.0
   * format. The key must be at most 255 characters long.
   */
  std::string authMetaBlob(const std::string& key, const std::string& value);

  /**
   * The ZMQ `REP` socket bound to the ZAP inproc channel.
   *
   * New ZMQ connection requests are automatically sent to this socket, and
   * corresponding replies indicate whether to accept or reject the connection.
   */
  fbzmq::Socket<ZMQ_REP, fbzmq::ZMQ_SERVER> authSock_;
};

} // namespace terragraph
} // namespace facebook
