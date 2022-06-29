/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ZapHandler.h"

#include "e2e/common/Consts.h"

using namespace fbzmq;

namespace facebook {
namespace terragraph {

ZapHandler::ZapHandler(Context& zmqContext) : authSock_{zmqContext} {
  // Bind the auth socket to the ZAP inproc endpoint
  VLOG(1) << "Binding to '" << E2EConsts::kZmqZapEndpoint << "'";
  if (authSock_.bind(SocketUrl{E2EConsts::kZmqZapEndpoint}).hasError()) {
    LOG(FATAL) << "Error binding to '" << E2EConsts::kZmqZapEndpoint << "'";
  }

  // Read authentication requests
  addSocket(RawZmqSocketPtr{*authSock_}, ZMQ_POLLIN, [this](int) noexcept {
    Message version, requestId, domain, address, identity, mechanism;

    // Read all ZAP request fields
    const auto recvRet = authSock_.recvMultiple(
        version, requestId, domain, address, identity, mechanism);
    if (recvRet.hasError()) {
      LOG(ERROR) << "Error receiving ZAP request: " << recvRet.error();
      return;
    }

    // Create a metadata blob that will become associated with this socket
    std::string ipAddress = address.read<std::string>().value();
    std::string metadata =
        authMetaBlob(E2EConsts::kZmqIpAddressMetaProperty, ipAddress);

    VLOG(4) << "Connection request received from " << ipAddress;

    // Send ZAP reply (indicates whether to accept/reject this connection)
    const auto sendRet = authSock_.sendMultiple(
        Message::from("1.0").value(),      // version frame
        requestId,                         // echo back the request ID
        Message::from("200").value(),      // status code: 200=ACCEPT
        Message::from("").value(),         // status text (optional)
        Message::from("").value(),         // user ID (optional)
        Message::from(metadata).value());  // metadata (optional)
    if (sendRet.hasError()) {
      LOG(ERROR) << "Error sending ZAP response: " << sendRet.error();
      return;
    }
  });
}

std::string
ZapHandler::authMetaBlob(const std::string& key, const std::string& value) {
  // Format:
  //   [1 byte] Key length
  //   [X bytes] Key (1 byte per char)
  //   [4 bytes] Value length (in network byte order)
  //   [Y bytes] Value (arbitrary data)
  uint8_t l1 = (uint8_t) key.length();
  size_t l2 = value.length();
  uint8_t b[l1 + l2 + 5] = {};
  b[0] = l1;
  b[l1 + 1] = (uint8_t) (l2 >> 24 & 255);
  b[l1 + 2] = (uint8_t) (l2 >> 16 & 255);
  b[l1 + 3] = (uint8_t) (l2 >> 8 & 255);
  b[l1 + 4] = (uint8_t) (l2 & 255);
  memcpy(&b[1], key.data(), l1);
  memcpy(&b[l1 + 5], value.data(), l2);
  return std::string(reinterpret_cast<char*>(b), l1 + l2 + 5);
}

} // namespace terragraph
} // namespace facebook
