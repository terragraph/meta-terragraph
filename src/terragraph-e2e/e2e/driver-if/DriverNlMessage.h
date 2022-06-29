/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <utility>
#include <vector>

#include <folly/MacAddress.h>
#include <glog/logging.h>

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

/*
 * Abstracting messages to the driver by type and data.
 *
 * This layer is only to store the message in a way which netlink understands.
 *
 * There are no explicit GET, SET types. It should be implied in the API, e.g.
 * getMessage() and sendMessage().
 *
 * Example: Doing a sendMessage() with type LINK_STATS is a request to driver
 * and a getMessage() is the response from driver.
 *
 * Some types may only be one-way or an acknowledgement of success in the
 * reverse direction.
 *
 * Example: LINK_INIT is a request and may only respond with the status of
 * the request (if at all).
 *
 * This is to keep things simple for now. If direction-aware types are needed,
 * we can always add them later.
 */

/**
 * Netlink message types.
 *
 * These correspond with `TGENUM_CMD` declarations in `fb_tgd_nlsdn_common.h`.
 */
enum class DriverNlMessageType : uint8_t {
  NONE,
  NODE_INIT = TGD_NLSDN_CMD_TGINIT,
  NODE_INIT_NOTIFY = TGD_NLSDN_CMD_NOTIFY_TGINIT,
  BF_SCAN = TGD_NLSDN_CMD_BF_SCAN,
  DR_LINK_STATUS = TGD_NLSDN_CMD_NOTIFY_LINK_STATUS,
  PASSTHRU_NB = TGD_NLSDN_CMD_PASSTHRU_NB,
  PASSTHRU_SB = TGD_NLSDN_CMD_PASSTHRU_SB,
  DRVR_REQ = TGD_NLSDN_CMD_SET_DRVR_CONFIG,
  DRVR_STAT_NB = TGD_NLSDN_CMD_DRVRSTAT_NB,
  DRVR_RSP = TGD_NLSDN_CMD_NOTIFY_DRVR_RSP,
  DEV_ALLOC = TGD_NLSDN_CMD_DEV_ALLOC,
  DEV_ALLOC_RSP = TGD_NLSDN_CMD_DEV_ALLOC_RSP,
  WSEC_STATUS = TGD_NLSDN_CMD_NOTIFY_WSEC_STATUS,
  WSEC_LINKUP_STATUS = TGD_NLSDN_CMD_NOTIFY_WSEC_LINKUP_STATUS,
  DEV_UPDOWN_STATUS = TGD_NLSDN_CMD_NOTIFY_DEV_UPDOWN_STATUS,
  SET_GPS_TIME = TGD_NLSDN_CMD_SET_GPS_TIME,
  SET_GPS_POS = TGD_NLSDN_CMD_SET_GPS_POS,
  // TODO: Add remaining types...
};

/**
 * Wrapper for messages transmitted and received over netlink.
 */
class DriverNlMessage {
 public:
  /** Constructor. */
  DriverNlMessage() : mType(DriverNlMessageType::NONE){};

  /** \{ */
  DriverNlMessage(const DriverNlMessage&) = default;
  DriverNlMessage& operator=(const DriverNlMessage&) = default;
  /** \} */

  /** Reset all internal state. */
  void
  reset() {
    mType = DriverNlMessageType::NONE;
    u8Attrs.clear();
    u16Attrs.clear();
    u32Attrs.clear();
    u64Attrs.clear();
    u8vlaAttrs.second.clear();
    // TODO: clear any other unspec attrs that maybe added
  }

  /**
   * Set the radio MAC address associated with this message.
   *
   * Returns false if the provided MAC address could not be parsed.
   */
  bool
  setRadioMac(const std::string& macAddr) {
    if (!macAddr.empty()) {
      try {
        radioMac = folly::MacAddress(macAddr);
        return true;
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Invalid radio MAC: " << macAddr;
      }
    }
    return false;
  }

  /** The message type. */
  DriverNlMessageType mType;

  /** The MAC address of the RF port. */
  folly::MacAddress radioMac;

  // Owner's responsibilty to fill in the attributes below for relevant
  // mType. Netlink handler will simply stick in all valid
  // entries for each attr type in the message with no validation.

  /** The u8 netlink attributes. */
  std::vector<std::pair<enum tgd_nlsdn_attrs, uint8_t>> u8Attrs;
  /** The u16 netlink attributes. */
  std::vector<std::pair<enum tgd_nlsdn_attrs, uint16_t>> u16Attrs;
  /** The u32 netlink attributes. */
  std::vector<std::pair<enum tgd_nlsdn_attrs, uint32_t>> u32Attrs;
  /** The u64 netlink attributes. */
  std::vector<std::pair<enum tgd_nlsdn_attrs, uint64_t>> u64Attrs;
  /** The variable-length netlink attributes. */
  std::pair<enum tgd_nlsdn_attrs, std::vector<uint8_t>> u8vlaAttrs;

  // Anything UNSPEC in nla will have custom structs which should be defined
  // and shared all over (firmware, driver, e2e) unless there exists a strong
  // reason to differentiate. Example. STATS message. Add a similar vector
  // with that message type. If we get too many, we may think of adding some
  // abstraction to convert these types to/from netlink messages
};

} // namespace terragraph
} // namespace facebook
