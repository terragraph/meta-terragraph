/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "MessageHandler.h"
#include "Const.h"
#include "Crypto.h"
#include "Message.pb.h"
#include "TerragraphNode.h"
#include "Utils.h"
#include <cstddef> // for std::size_t
#include <gflags/gflags.h>
#include <glog/logging.h>

DECLARE_bool(encryption);

namespace facebook {
namespace terragraph {
namespace mobd {

static TerragraphNode node;
static Message response;

static enum {
  PRE_SHARED_KEY,
  SESSION_KEY_SENT,
  AUTHENTICATED
} state = PRE_SHARED_KEY;

void
resetAuthentication() {
  state = PRE_SHARED_KEY;
  crypto::init(DEFAULT_KEYGEN_PHRASE);
}

const Message*
messageHandler(const Message& m) {

  // Respond only when security handshake is complete
  if (FLAGS_encryption &&
      ((state != AUTHENTICATED && m.message_type() != Message::HELLO) ||
       (state == AUTHENTICATED && m.message_type() == Message::HELLO))) {
    resetAuthentication();
    return nullptr;
  }

  // Start initializing the response message
  const bool disWdogOk = disableWatchdog(m);
  response.Clear();
  response.set_message_type(m.message_type());
  if (m.has_cookie()) {
    response.set_cookie(m.cookie());
  }
  std::string response_body;
  switch (m.message_type()) {
    case Message::HELLO: {
      LOG(INFO) << "RX HELLO, state " << state;
      Hello hello;
      if (state == PRE_SHARED_KEY) {
        state = SESSION_KEY_SENT;
        crypto::init(); // set random key
        hello.set_keygen_phrase(crypto::getKeyGenPhrase());
      } else { // state == SESSION_KEY_SENT
        state = AUTHENTICATED;
        hello.set_auth_ok(true);
      }
      hello.SerializeToString(&response_body);
      response.set_message_body(response_body);
    } break;
    case Message::NODE_INFO: {
      LOG(INFO) << "RX NODE_INFO";
      NodeInfo nodeInfo;
      nodeInfo.set_uboot_version(node.ubootVersion);
      nodeInfo.set_tg_version(node.osVersion);
      nodeInfo.set_tg_secondary_version(node.secondaryOsVersion);
      nodeInfo.set_dtb_version(node.dtbVersion);
      nodeInfo.set_nic0_mac_address(node.nic0Mac);
      *nodeInfo.mutable_radio_mac() = {
          node.radioMac.begin(), node.radioMac.end()};
      nodeInfo.SerializeToString(&response_body);
      response.set_message_body(response_body);
    } break;
    case Message::IGNITE_LINK: {
      LOG(INFO) << "RX IGNITE_LINK";
      IgniteLink rxIgnite;
      if (m.has_message_body() && rxIgnite.ParseFromString(m.message_body()) &&
          rxIgnite.has_initiator_mac() && rxIgnite.has_responder_mac() &&
          rxIgnite.has_ignite()) {
        bool ok;
        if (rxIgnite.ignite()) {
          ok = node.assoc(rxIgnite.initiator_mac(), rxIgnite.responder_mac());
        } else {
          ok = node.dissoc(rxIgnite.initiator_mac(), rxIgnite.responder_mac());
        }
        IgniteLink txIgnite;
        txIgnite.set_initiator_mac(rxIgnite.initiator_mac());
        txIgnite.set_responder_mac(rxIgnite.responder_mac());
        txIgnite.set_ignite(ok);
        txIgnite.SerializeToString(&response_body);
        response.set_message_body(response_body);
      } else {
        response.set_error_message("Missing or invalid fields");
      }
    } break;
    case Message::LINK_STATS: {
      LOG(INFO) << "RX LINK_STATS";
      LinkStats rxLinkStats;
      if (m.has_message_body() &&
          rxLinkStats.ParseFromString(m.message_body()) &&
          rxLinkStats.has_initiator_mac() && rxLinkStats.has_responder_mac()) {
        LinkStats txLinkStats;
        txLinkStats.set_initiator_mac(rxLinkStats.initiator_mac());
        txLinkStats.set_responder_mac(rxLinkStats.responder_mac());
        int timeoutSec = node.defaultLinkStatsTimeoutSec;
        if (rxLinkStats.has_timeout_sec()) {
          timeoutSec = rxLinkStats.timeout_sec();
        }
        std::vector<int> snr;
        std::vector<int> rssi;
        node.getLinkStats(
            timeoutSec,
            rxLinkStats.initiator_mac(),
            rxLinkStats.responder_mac(),
            snr,
            rssi);
        // See: "Repeated Numeric Fields" in
        // https://developers.google.com/protocol-buffers/docs/reference/cpp-generated
        *txLinkStats.mutable_snr() = {snr.begin(), snr.end()};
        *txLinkStats.mutable_rssi() = {rssi.begin(), rssi.end()};
        txLinkStats.SerializeToString(&response_body);
        response.set_message_body(response_body);
      } else {
        response.set_error_message("Missing or invalid fields");
      }
    } break;

    case Message::TEST: {
      LOG(INFO) << "RX TEST";
      Test rxTest;
      if (m.has_message_body() && rxTest.ParseFromString(m.message_body()) &&
          rxTest.has_num_test_bytes() && rxTest.num_test_bytes() >= 0 &&
          (std::size_t)rxTest.num_test_bytes() <= MAX_TEST_BYTES) {
        Test txTest;
        txTest.set_num_test_bytes(rxTest.num_test_bytes());
        std::string test_data;
        test_data.reserve(rxTest.num_test_bytes());
        for (int i = 0; i < rxTest.num_test_bytes(); i++) {
          test_data.push_back((char)(i & 0xff));
        }
        txTest.set_test_data(test_data);
        txTest.SerializeToString(&response_body);
        response.set_message_body(response_body);
      } else {
        response.set_error_message("Missing or invalid fields");
      }
    } break;

    case Message::REBOOT: {
      LOG(INFO) << "RX REBOOT";
      if (node.reboot()) {
        return nullptr; // don't try to respond when reboot is successful
      } else {
        response.set_error_message("Reboot failed");
      }
    } break;

    case Message::BOOT_SECONDARY_IMAGE: {
      LOG(INFO) << "RX BOOT_SECONDARY_IMAGE";
      if (!node.swapPrimaryAndSecondaryPartitions()) {
        response.set_error_message("Failed to swap partitions");
      } else if (!node.reboot()) {
        response.set_error_message("Failed to reboot");
      } else
        return nullptr; // don't respond when secondary image boot is successful
    } break;

    case Message::WATCHDOG: {
      LOG(INFO) << "RX WATCHDOG";
    } break;

    default: {
      LOG(INFO) << "RX message type: " << m.message_type() << "not handled yet";
      response.set_error_message("Not handled yet.");
    } break;
  }

  // Enable the watchdog only when it is explicitly requested.
  const bool enWdogOk = enableWatchdog(m);
  if (!response.has_error_message() && (!disWdogOk || !enWdogOk)) {
    response.set_error_message("Failed to enable or disable the watchdog");
  }
  return &response;
} // namespace mobd

} // namespace mobd
} // namespace terragraph
} // namespace facebook
