/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "OpenrUtils.h"

#include <algorithm>
#include <folly/IPAddress.h>
#include <folly/Range.h>

namespace facebook {
namespace terragraph {

std::string
OpenrUtils::toOpenrNodeName(const std::string& nodeId) {
  std::string name = "node-" + nodeId;
  std::replace(name.begin(), name.end(), ':', '.');
  return name;
}

std::string
OpenrUtils::fromOpenrNodeName(const std::string& name) {
  std::string nodeId = name.substr(5);
  std::replace(nodeId.begin(), nodeId.end(), '.', ':');
  return nodeId;
}

std::string
OpenrUtils::binaryAddressToString(const openr::thrift::BinaryAddress& addr) {
  return folly::IPAddress::fromBinary(folly::ByteRange(
      reinterpret_cast<const unsigned char*>(addr.addr_ref().value().data()),
      addr.addr_ref().value().size())).str();
}

} // namespace terragraph
} // namespace facebook
