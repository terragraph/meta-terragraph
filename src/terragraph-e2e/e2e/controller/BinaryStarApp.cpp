/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BinaryStarApp.h"

#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/gen/Base.h>

#include "BinaryStarFsm.h"
#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "topology/TopologyWrapper.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

DEFINE_int32(
    bstar_heartbeat_period_ms,
    10000,
    "Period for heartbeats between controllers, in milliseconds");

DEFINE_int32(
    bstar_failover_missed_heartbeats,
    3,
    "Number of missed heartbeats before declaring the other controller 'dead'");

DEFINE_int32(
    bstar_primary_recovery_heartbeats,
    30,
    "If the backup is 'active' and the primary comes back online, the backup "
    "will yield to the primary (i.e. automatic recovery) after receiving this "
    "number of successive heartbeats (0 to disable). Ignored on primary.");

namespace facebook {
namespace terragraph {

BinaryStarApp::BinaryStarApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    bool isBstarEnabled,
    bool isBstarPrimary,
    const std::string& pubSockUrl,
    const std::string& peerPubSockUrl,
    const std::string& versionFile)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kBinaryStarAppCtrlId),
      isBstarEnabled_{isBstarEnabled},
      isBstarPrimary_{isBstarPrimary},
      peerPubSock_{zmqContext},
      peerSubSock_{zmqContext} {
  // If feature is disabled, quit here, don't initialize sockets
  if (!isBstarEnabled_) {
    return;
  }

  // Set initial primary/backup state
  LOG(INFO) << "[High Availability Mode] Running as "
            << (isBstarPrimary ? "PRIMARY" : "BACKUP") << " controller...";
  bstarFsm_.state = isBstarPrimary ?
      thrift::BinaryStarFsmState::STATE_PRIMARY :
      thrift::BinaryStarFsmState::STATE_BACKUP;

  // Get controller version string (ignore errors)
  if (folly::readFile(versionFile.c_str(), version_)) {
    version_ = folly::trimWhitespace(version_).str();
  }

  // Send periodic heartbeats to peer controller
  heartbeatTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
    sendHeartbeat();
  });
  heartbeatTimeout_->scheduleTimeout(
      std::chrono::milliseconds(FLAGS_bstar_heartbeat_period_ms), true);
  scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
    sendHeartbeat();  // send heartbeat immediately
  });

  // Initialize PUB/SUB sockets for heartbeating peer controller
  VLOG(1) << "Binding to '" << pubSockUrl << "'";
  if (peerPubSock_.bind(fbzmq::SocketUrl{pubSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << pubSockUrl << "'";
  }
  VLOG(1) << "Connecting to peer at '" << peerPubSockUrl << "'";
  if (peerSubSock_.connect(fbzmq::SocketUrl{peerPubSockUrl}).hasError()) {
    LOG(FATAL) << "Error connecting to '" << peerPubSockUrl << "'";
  }
  peerSubSock_.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();  // accept all messages
  addSocket(
      fbzmq::RawZmqSocketPtr{*peerSubSock_}, ZMQ_POLLIN, [this](int) noexcept {
    auto message = peerSubSock_.recvThriftObj<thrift::Message>(
        serializer_, E2EConsts::kReadTimeout);
    if (message.hasError()) {
      LOG(ERROR) << "Error reading message: " << message.error();
      return;
    }

    // Decompress the message (if needed)
    std::string error;
    if (!CompressionUtil::decompress(message.value(), error)) {
      LOG(ERROR) << error;
      return;
    }

    switch (message->mType) {
      case thrift::MessageType::BSTAR_SYNC:
        processBstarSync(message.value());
        break;
      default:
        LOG(ERROR)
            << "Wrong type of message ("
            << folly::get_default(
                   TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                   message->mType,
                   "UNKNOWN")
            << ") received from peer";
        break;
    }
  });
}

void
BinaryStarApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::BSTAR_FSM:
      processBstarFsm(senderApp, message);
      break;
    case thrift::MessageType::BSTAR_GET_STATE:
      processBstarGetState(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
      break;
  }
}

void
BinaryStarApp::processBstarFsm(
    const std::string& senderApp, const thrift::Message& message) {
  if (!isBstarEnabled_) {
    return;
  }

  VLOG(3) << "Received new FSM from " << senderApp;
  auto fsm = maybeReadThrift<thrift::BinaryStar>(message);
  if (!fsm) {
    handleInvalidMessage("BinaryStar", senderApp);
    return;
  }

  // Log FSM state changes
  if (bstarFsm_.state != fsm->state) {
    logStateChange(bstarFsm_.state, fsm->state);

    // Did we become ACTIVE?
    if (fsm->state == thrift::BinaryStarFsmState::STATE_ACTIVE) {
      doActiveStateChangeActions();
    }
  }

  // Store new FSM
  bstarFsm_ = fsm.value();
}

void
BinaryStarApp::processBstarGetState(
    const std::string& senderApp, const thrift::Message& /*message*/) {
  // Send back uninitialized struct even if this feature is disabled
  sendToCtrlApp(senderApp, thrift::MessageType::BSTAR_FSM, bstarFsm_);
}

void
BinaryStarApp::processBstarSync(const thrift::Message& message) {
  auto heartbeat = maybeReadThrift<thrift::BinaryStarSync>(message);
  if (!heartbeat) {
    handleInvalidMessage("BinaryStarSync", "");
    return;
  }

  auto binaryStarFsmStateMap =
      TEnumMapFactory<thrift::BinaryStarFsmState>::makeValuesToNamesMap();

  VLOG(2) << "Received heartbeat from peer (state="
          << binaryStarFsmStateMap.at(heartbeat->state)
          << ", seqNum=" << heartbeat->seqNum << ")";

  if (heartbeat->version != version_) {
    std::string stateMsg = "Controller version mismatch!";
    LOG(ERROR) << "[High Availability Mode] " << stateMsg << "\n"
                  "  Local version: " << version_ << "\n"
                  "  Peer version: " << heartbeat->version;
    eventClient_->logEventDynamic(
        thrift::EventCategory::HIGH_AVAILABILITY,
        thrift::EventId::PEER_VERSION_MISMATCH,
        thrift::EventLevel::WARNING,
        stateMsg,
        folly::dynamic::object(
            "local_version", version_)("peer_version", heartbeat->version));
  }

  int64_t now = BinaryStarFsm::getTimeMs();
  int64_t prevHeartbeatTime = lastHeartbeatTime_;
  thrift::BinaryStarFsmState prevState = bstarFsm_.state;
  int32_t peerTimeoutMs =
      FLAGS_bstar_heartbeat_period_ms * FLAGS_bstar_failover_missed_heartbeats;

  // Update last received heartbeat time
  lastHeartbeatTime_ = now;

  // Pass peer state to FSM as an event
  thrift::BinaryStarFsmEvent event = static_cast<thrift::BinaryStarFsmEvent>(
      heartbeat->state);
  auto maybeFsm = BinaryStarFsm::processEvent(bstarFsm_, event);
  if (maybeFsm.hasError()) {
    // Fatal error: Try reverting to original primary/backup state and hope
    // for the best (won't solve misconfiguration of dual-primary/backup).
    LOG(ERROR) << "[High Availability Mode] " << maybeFsm.error();
    std::string localStateStr = folly::get_default(
       binaryStarFsmStateMap, prevState, "UNKNOWN");
    std::string peerStateStr = folly::get_default(
       binaryStarFsmStateMap, heartbeat->state, "UNKNOWN");
    eventClient_->logEventDynamic(
        thrift::EventCategory::HIGH_AVAILABILITY,
        thrift::EventId::HIGH_AVAILABILITY_STATE_CHANGE,
        thrift::EventLevel::FATAL,
        maybeFsm.error(),
        folly::dynamic::object(
            "local_state", localStateStr)("peer_state", peerStateStr));

    bstarFsm_.state = isBstarPrimary_ ?
        thrift::BinaryStarFsmState::STATE_PRIMARY :
        thrift::BinaryStarFsmState::STATE_BACKUP;
    LOG(ERROR) << "[High Availability Mode] Reverted to original state ("
               << binaryStarFsmStateMap.at(bstarFsm_.state)
               << ").";
    return;
  }

  // Store new FSM, update peer expiry
  bstarFsm_ = maybeFsm.value();
  bstarFsm_.peerExpiry = now + peerTimeoutMs;

  // Log FSM state changes
  if (bstarFsm_.state != prevState) {
    logStateChange(prevState, bstarFsm_.state);
  }

  // PASSIVE:
  // - Update sequence number.
  // - Forward app data from ACTIVE peer to our apps (unless version mismatch).
  if (bstarFsm_.state == thrift::BinaryStarFsmState::STATE_PASSIVE &&
      heartbeat->state == thrift::BinaryStarFsmState::STATE_ACTIVE) {
    if (prevState != thrift::BinaryStarFsmState::STATE_PASSIVE) {
      // Just became passive, don't do anything
      // (sequence number mismatch will trigger full data sent next heartbeat)
    } else {
      seqNum_ = heartbeat->seqNum;

      if (heartbeat->version == version_) {
        // [DATA_SYNC_SECTION]
        if (heartbeat->data.topology_ref().has_value()) {
          sendToCtrlApp(
              E2EConsts::kTopologyAppCtrlId,
              thrift::MessageType::BSTAR_APP_DATA,
              heartbeat->data);
        }
        if (heartbeat->data.configNetworkOverrides_ref().has_value() ||
            heartbeat->data.configNodeOverrides_ref().has_value() ||
            heartbeat->data.configAutoNodeOverrides_ref().has_value() ||
            heartbeat->data.configController_ref().has_value()) {
          sendToCtrlApp(
              E2EConsts::kConfigAppCtrlId,
              thrift::MessageType::BSTAR_APP_DATA,
              heartbeat->data);
        }
      }
    }
  }

  // ACTIVE:
  // - If received sequence number is different from ours, we will transmit the
  //   full app data on the next heartbeat.
  // - If we are the backup controller: update successive heartbeat counter for
  //   automatic recovery of the primary.
  if (bstarFsm_.state == thrift::BinaryStarFsmState::STATE_ACTIVE) {
    if (prevState != thrift::BinaryStarFsmState::STATE_ACTIVE) {
      // Just became active, trigger state change actions only
      doActiveStateChangeActions();
    } else {
      // Check received sequence number
      if (heartbeat->seqNum != seqNum_) {
        VLOG(2) << "Received unexpected sequence number from peer (saw "
                << heartbeat->seqNum << ", expected " << seqNum_
                << "). Queueing full data sync...";
        sendFullData_ = true;
      }

      // If backup, check automatic recovery heartbeat counter (if enabled)
      if (!isBstarPrimary_ && FLAGS_bstar_primary_recovery_heartbeats > 0) {
        if (now - prevHeartbeatTime > peerTimeoutMs) {
          autoRecoveryHeartbeats_ = 0;  // peer has died since last heartbeat
        }
        autoRecoveryHeartbeats_++;

        VLOG(2) << "BACKUP: Recorded " << autoRecoveryHeartbeats_
                << " successive heartbeat(s) from passive primary "
                   "(automatic recovery at "
                << FLAGS_bstar_primary_recovery_heartbeats << ")";

        if (autoRecoveryHeartbeats_ >=
            FLAGS_bstar_primary_recovery_heartbeats) {
          // Trigger automatic recovery
          yieldToPrimary();
        }
      }
    }
  }

  // Send updated FSM to broker
  sendToCtrlApp(
      E2EConsts::kBrokerCtrlId, thrift::MessageType::BSTAR_FSM, bstarFsm_);
}

void
BinaryStarApp::sendHeartbeat() {
  // Construct and send heartbeat message (FSM state, sequence number, app data)
  thrift::BinaryStarSync heartbeat;
  heartbeat.state = bstarFsm_.state;
  heartbeat.version = version_;
  if (bstarFsm_.state == thrift::BinaryStarFsmState::STATE_ACTIVE) {
    // If ACTIVE, include any new app data in this heartbeat
    auto lockedSyncedAppData = SharedObjects::getSyncedAppData()->wlock();
    thrift::BinaryStarAppData emptyData;
    if (!sendFullData_ && lockedSyncedAppData->unsyncedAppData == emptyData) {
      // No new data to send, so don't increment sequence number
      heartbeat.seqNum = seqNum_;
    } else {
      // Send data with incremented sequence number
      heartbeat.seqNum = ++seqNum_;
      heartbeat.data = sendFullData_
          ? lockedSyncedAppData->fullAppData
          : lockedSyncedAppData->unsyncedAppData;
    }

    // Clear unsynced data for the next period
    lockedSyncedAppData->unsyncedAppData = emptyData;
    lockedSyncedAppData.unlock();  // lockedSyncedAppData -> NULL
    sendFullData_ = false;  // set to 'true' if we receive wrong seqNum
  } else {
    heartbeat.seqNum = seqNum_;
  }

  VLOG(2) << "Sending heartbeat to peer (state="
          << TEnumMapFactory<thrift::BinaryStarFsmState>::
              makeValuesToNamesMap().at(heartbeat.state)
          << ", seqNum=" << heartbeat.seqNum << ")";
  sendToPeer(thrift::MessageType::BSTAR_SYNC, heartbeat, true /* compress */);
}

void
BinaryStarApp::doActiveStateChangeActions() {
  // Clear internal state
  clear();

  // [DATA_SYNC_SECTION]
  // Request new app data from all apps
  sendToCtrlApp(
      E2EConsts::kTopologyAppCtrlId,
      thrift::MessageType::BSTAR_GET_APP_DATA,
      thrift::BinaryStarGetAppData());
  sendToCtrlApp(
      E2EConsts::kConfigAppCtrlId,
      thrift::MessageType::BSTAR_GET_APP_DATA,
      thrift::BinaryStarGetAppData());
}

void
BinaryStarApp::yieldToPrimary() {
  std::string stateMsg = "Primary has recovered, changing to BACKUP...";
  LOG(INFO) << "[High Availability Mode] " << stateMsg;
  eventClient_->logEvent(
      thrift::EventCategory::HIGH_AVAILABILITY,
      thrift::EventId::HIGH_AVAILABILITY_STATE_CHANGE,
      thrift::EventLevel::INFO,
      stateMsg);

  // Clear internal state
  clear();

  // Change to BACKUP and send heartbeat immediately
  bstarFsm_.state = thrift::BinaryStarFsmState::STATE_BACKUP;
  scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
    sendHeartbeat();

    // Tell nodes to switch controllers -- we can send this to all nodes since
    // minion only connects to one controller, so if we can reach a node then
    // it's connected to us.
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    auto macs = folly::gen::from(lockedTopologyW->getAllNodes()) |
                folly::gen::field(&thrift::Node::mac_addr) |
                folly::gen::as<std::vector<std::string>>();
    lockedTopologyW.unlock();  // lockedTopologyW -> NULL
    for (const auto& mac : macs) {
      if (mac.empty()) {
        continue;
      }
      sendToMinionApp(
          mac,
          E2EConsts::kBrokerMinionId,
          thrift::MessageType::BSTAR_SWITCH_CONTROLLER,
          thrift::BinaryStarSwitchController());
    }
  });
}

void
BinaryStarApp::clear() {
  seqNum_ = 0;
  sendFullData_ = false;
  lastHeartbeatTime_ = 0;
  autoRecoveryHeartbeats_ = 0;
  SharedObjects::getSyncedAppData()->wlock()->clear();
}

void
BinaryStarApp::logStateChange(
    const thrift::BinaryStarFsmState& oldState,
    const thrift::BinaryStarFsmState& newState) {
  auto binaryStarFsmStateMap =
      TEnumMapFactory<thrift::BinaryStarFsmState>::makeValuesToNamesMap();
  std::string stateMsg = folly::sformat(
      "State changed from {} to {}",
      binaryStarFsmStateMap.at(oldState),
      binaryStarFsmStateMap.at(newState));
  LOG(INFO) << "[High Availability Mode] " << stateMsg;
  eventClient_->logEvent(
      thrift::EventCategory::HIGH_AVAILABILITY,
      thrift::EventId::HIGH_AVAILABILITY_STATE_CHANGE,
      thrift::EventLevel::INFO,
      stateMsg);
}

} // namespace terragraph
} // namespace facebook
