/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NetlinkMessage.h"
#include "DriverNlMessage.h"

#include <exception>
#include <new>
#include <stdexcept>
#include <system_error>

namespace facebook {
namespace terragraph {

NetlinkMessage::NetlinkMessage() {
  msg_ = nlmsg_alloc();
  if (msg_ == nullptr) {
    throw std::bad_alloc();
  }
}

NetlinkMessage::~NetlinkMessage() {
  if (msg_) {
    nlmsg_free(msg_);
  }
}
} // namespace terragraph
} // namespace facebook
