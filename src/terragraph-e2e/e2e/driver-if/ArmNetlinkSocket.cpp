/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ArmNetlinkSocket.h"

#include <glog/logging.h>

#include <folly/Format.h>
#include <folly/MacAddress.h>

#include <new>
#include <stdexcept>

namespace facebook {
namespace terragraph {

TGD_NLSDN_POLICY_DEFN();

DriverNlMessage ArmNetlinkSocket::driverNlMessage_;

ArmNetlinkSocket::ArmNetlinkSocket() {
  VLOG(1) << "Creating netlink socket to talk to driver\n";
  socket_ = nl_socket_alloc();
  if (socket_ == nullptr) {
    throw std::runtime_error("Failed to create netlink socket");
  }
}

void
ArmNetlinkSocket::connect() {
  VLOG(1) << "Connecting netlink socket to talk to driver\n";

  int err = nl_connect(socket_, NETLINK_GENERIC);
  if (err != 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to connect socket. Error: {}", nl_geterror(err)));
  }

  err = nl_socket_set_buffer_size(socket_, 1024 * 1014, 0);
  if (err != 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to change socket buffer size. Error: {}", nl_geterror(err)));
  }

  err = nl_socket_modify_cb(
      socket_, NL_CB_VALID, NL_CB_CUSTOM, recvFunc_, nullptr);
  if (err != 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to modify socket callback. Error: {}", nl_geterror(err)));
  }

  err = nl_socket_set_nonblocking(socket_);
  if (err != 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to set socket as non blocking. Error: {}", nl_geterror(err)));
  }

  err = nl_socket_add_membership(
      socket_,
      genl_ctrl_resolve_grp(
          socket_, TGD_NLSDN_GENL_NAME, TGD_NLSDN_GENL_GROUP_NAME));
  if (err != 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to set socket membership. Error: {}", nl_geterror(err)));
  }

  socketFamilyId_ = genl_ctrl_resolve(socket_, TGD_NLSDN_GENL_NAME);
  if (socketFamilyId_ < 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to resolve socket family. FamilyId: {}", socketFamilyId_));
  }
  // TODO: We should support seq numbers..
  nl_socket_disable_seq_check(socket_);

  VLOG(1) << "Successfully connected netlink socket to talk to driver\n";
}

ArmNetlinkSocket::~ArmNetlinkSocket() {
  VLOG(1) << "Netlink socket is being destroyed\n";

  if (socket_) {
    // TODO: Any teardown ?
    nl_socket_free(socket_);
    socket_ = nullptr;
  }
}

std::optional<DriverNlMessage>
ArmNetlinkSocket::getMessage() {
  driverNlMessage_.reset();

  int err = 0;
  if ((err = nl_recvmsgs_default(socket_)) != 0) {
    LOG(ERROR) << folly::sformat(
        "Failed to recv data from socket. Error: {}", nl_geterror(err));
    return std::nullopt;
  } else {
    // Assuming we were called when data was ready, our recvFunc_
    // will now be called and populate our static driverNlMessage_
    // with data from driver
    // Let copy ctor do our job
    VLOG(6) << "Got netlink message from driver\n";
    return {driverNlMessage_};
  }
}

// TODO. Lets not throw from here as this is a callback from
// libnl and they may do cleanup internally
int
ArmNetlinkSocket::recvFunc_(struct nl_msg* nlMessage, void*) noexcept {
  struct nlmsghdr* nlHeader = nullptr;
  struct genlmsghdr* nlMessageHeader = nullptr;
  struct nlattr* nlAttrs[TGD_NLSDN_NUM_ATTR] = {};

  nlHeader = nlmsg_hdr(nlMessage);
  if (nlHeader == nullptr) {
    return -1;
  }
  nlMessageHeader = genlmsg_hdr(nlHeader);
  if (nlMessageHeader == nullptr) {
    return -1;
  }
  driverNlMessage_.mType =
      static_cast<DriverNlMessageType>(nlMessageHeader->cmd);

  if (genlmsg_parse(
          nlHeader, 0, nlAttrs, TGD_NLSDN_ATTR_MAX, tgd_nlsdn_policy) < 0) {
    return -1;
  }

  for (int attr = 0; attr < TGD_NLSDN_NUM_ATTR; ++attr) {
    if (nlAttrs[attr] == nullptr) {
      continue;
    }
    auto enumAttr = static_cast<enum tgd_nlsdn_attrs>(attr);
    if (tgd_nlsdn_policy[attr].type == NLA_U8) {
      driverNlMessage_.u8Attrs.push_back({enumAttr, nla_get_u8(nlAttrs[attr])});
    }
    if (tgd_nlsdn_policy[attr].type == NLA_U16) {
      driverNlMessage_.u16Attrs.push_back(
          {enumAttr, nla_get_u16(nlAttrs[attr])});
    }
    if (tgd_nlsdn_policy[attr].type == NLA_U32) {
      driverNlMessage_.u32Attrs.push_back(
          {enumAttr, nla_get_u32(nlAttrs[attr])});
    }
    if (tgd_nlsdn_policy[attr].type == NLA_U64) {
      if (enumAttr == TGD_NLSDN_ATTR_RADIO_MACADDR) {
        driverNlMessage_.radioMac =
            folly::MacAddress::fromHBO(nla_get_u64(nlAttrs[attr]));
      } else {
        driverNlMessage_.u64Attrs.push_back(
            {enumAttr, nla_get_u64(nlAttrs[attr])});
      }
    }
    if (tgd_nlsdn_policy[attr].type == NLA_UNSPEC) {
      const uint8_t* data =
          static_cast<const uint8_t*>(nla_data(nlAttrs[attr]));
      int len = nla_len(nlAttrs[attr]);

      driverNlMessage_.u8vlaAttrs.first = enumAttr;
      driverNlMessage_.u8vlaAttrs.second.assign(data, data + len);
    }
  }
  return 0;
}

void
ArmNetlinkSocket::sendMessage(const DriverNlMessage& message) {
  // NOTE: This can be called hundreds or thousands of times per second
  //       e.g. by ArmDriverIf::sendCodebook(). Do not print logs here!!
  VLOG(5) << "Sending message to driver\n";

  NetlinkMessage nlMessage;
  int err = 0;

  if (genlmsg_put(
          nlMessage.msg_,
          NL_AUTO_PID,
          NL_AUTO_SEQ,
          socketFamilyId_,
          0,
          0,
          static_cast<uint8_t>(message.mType),
          TGD_NLSDN_VERSION) == nullptr) {
    throw std::runtime_error("Failed to add hdr to netlink message");
  }

  err = nla_put_u64(
      nlMessage.msg_, TGD_NLSDN_ATTR_RADIO_MACADDR, message.radioMac.u64HBO());
  if (err < 0) {
    throw std::runtime_error(folly::sformat(
        "Failed to add radio MAC to message. Error: {}", nl_geterror(err)));
  }
  for (const auto& u8Attr : message.u8Attrs) {
    err = nla_put_u8(nlMessage.msg_, u8Attr.first, u8Attr.second);
    if (err < 0) {
      throw std::runtime_error(folly::sformat(
          "Failed to add attr to message. Error: {}", nl_geterror(err)));
    }
  }
  for (const auto& u16Attr : message.u16Attrs) {
    err = nla_put_u16(nlMessage.msg_, u16Attr.first, u16Attr.second);
    if (err < 0) {
      throw std::runtime_error(folly::sformat(
          "Failed to add attr to message. Error: {}", nl_geterror(err)));
    }
  }
  for (const auto& u32Attr : message.u32Attrs) {
    err = nla_put_u32(nlMessage.msg_, u32Attr.first, u32Attr.second);
    if (err < 0) {
      throw std::runtime_error(folly::sformat(
          "Failed to add attr to message. Error: {}", nl_geterror(err)));
    }
  }
  for (const auto& u64Attr : message.u64Attrs) {
    err = nla_put_u64(nlMessage.msg_, u64Attr.first, u64Attr.second);
    if (err < 0) {
      throw std::runtime_error(folly::sformat(
          "Failed to add attr to message. Error: {}", nl_geterror(err)));
    }
  }
  if (!message.u8vlaAttrs.second.empty()) {
    err = nla_put(
        nlMessage.msg_,
        message.u8vlaAttrs.first,
        message.u8vlaAttrs.second.size(),
        message.u8vlaAttrs.second.data());
    if (err < 0) {
      throw std::runtime_error(folly::sformat(
          "Failed to add attr to message. Error: {}", nl_geterror(err)));
    }
  }
  // TODO: Add nla_put for custom types

  err = nl_send_auto(socket_, nlMessage.msg_);
  if (err < 0) {
    throw std::runtime_error(
        folly::sformat("Failed to send message. Error: {}", nl_geterror(err)));
  }

  VLOG(5) << "Sent netlink message to driver\n";
  // NetlinkMessage dtor will release the buffer
}

int
ArmNetlinkSocket::getSocketFd() const {
  int fd = nl_socket_get_fd(socket_);
  if (fd != -1) {
    return fd;
  }
  throw std::runtime_error("Invalid socket fd");
}

} // namespace terragraph
} // namespace facebook
