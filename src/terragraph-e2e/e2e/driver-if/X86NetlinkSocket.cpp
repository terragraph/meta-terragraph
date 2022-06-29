/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "X86NetlinkSocket.h"

#include <glog/logging.h>

#include <sys/eventfd.h>
#include <unistd.h>

namespace facebook {
namespace terragraph {

X86NetlinkSocket::X86NetlinkSocket() {
  // Create event-fd for driver response
  if ((driverEventFd_ = eventfd(0 /* init-value */, EFD_CLOEXEC /* flags */)) <
      0) {
    LOG(FATAL) << "Failed to create an eventfd.";
  }
}

void
X86NetlinkSocket::connect() {}

std::optional<DriverNlMessage>
X86NetlinkSocket::getMessage() {
  uint64_t buf;
  auto bytesRead = read(driverEventFd_, static_cast<void*>(&buf), sizeof(buf));
  CHECK_EQ(sizeof(buf), bytesRead);

  return {driverResp_};
}

DriverNlMessage
respond(const DriverNlMessage& req) {
  DriverNlMessage resp;

  switch (req.mType) {
    case DriverNlMessageType::NODE_INIT:
      resp.mType = DriverNlMessageType::NODE_INIT_NOTIFY;
      resp.u8Attrs.emplace_back(TGD_NLSDN_ATTR_SUCCESS, TG_IOCTL_SUCCESS);
      resp.u64Attrs.emplace_back(TGD_NLSDN_ATTR_MACADDR, 0 /* mac address */);
      break;
    case DriverNlMessageType::DRVR_REQ:
      resp.mType = DriverNlMessageType::DRVR_RSP;
      resp.u8vlaAttrs = req.u8vlaAttrs;
      resp.u8Attrs.emplace_back(TGD_NLSDN_ATTR_SUCCESS, TG_IOCTL_SUCCESS);
      break;
    case DriverNlMessageType::PASSTHRU_SB:
      // skip driver responses for southbound messages destined for firmware
      resp.mType = req.mType;
      resp.u8Attrs = req.u8Attrs;
      resp.u8Attrs.emplace_back(TGD_NLSDN_ATTR_SUCCESS, 0);
      break;
    default:
      LOG(ERROR) << "Expected message type";
  }

  return resp;
}

void
X86NetlinkSocket::sendMessage(const DriverNlMessage& msg) {
  driverResp_ = respond(msg);

  // just to trigger fd reception, not sending real data, which is driverResp_
  signalDataReady();
}

void
X86NetlinkSocket::signalDataReady() const {
  uint64_t buf{1};
  auto bytesWritten =
      write(driverEventFd_, static_cast<void*>(&buf), sizeof(buf));
  CHECK_EQ(sizeof(buf), bytesWritten);
}

int
X86NetlinkSocket::getSocketFd() const {
  return driverEventFd_;
}

} // namespace terragraph
} // namespace facebook
