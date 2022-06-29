/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>

#include "CtrlApp.h"
#include "e2e/common/CompressionUtil.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

/**
 * App that handles heartbeating and data synchronization between the primary
 * and backup controllers in the "Binary Star" replication scheme.
 *
 * See BinaryStarFsm for details about the state machine.
 *
 * The data synchronization protocol is strictly best-effort; it is NOT fully
 * fault-tolerant.
 *
 * Data synchronization protocol overview:
 * - The ACTIVE peer sends new application data to the PASSIVE as part of the
 *   heartbeat message. It also attaches a sequence number, which the PASSIVE
 *   echoes back in its heartbeat.
 * - If the ACTIVE receives a heartbeat with a mismatching sequence number, it
 *   will send a full copy of its data with the next heartbeat. Otherwise, it
 *   will only send data that changed (if any).
 * - The sequence number is only incremented when the ACTIVE sends either new
 *   data or a full copy of its data. It is NOT incremented on every heartbeat.
 * - When a peer becomes ACTIVE, it will reset its sequence number, then request
 *   current data to be sent from all its applications.
 * - Sequence numbers are initialized to 0. The first heartbeat from the ACTIVE
 *   will have a sequence number of 1, since it has requested data from its
 *   applications (see above).
 * - Only a PASSIVE peer (not PRIMARY/BACKUP) will update its sequence number
 *   when receiving a heartbeat. This guarantees that the first heartbeat sent
 *   to the ACTIVE will mismatch (since 0 cannot match), and thus trigger full
 *   data sync on the next heartbeat.
 *
 * When adding new fields to sync (using thrift::BinaryStarAppData), edit ALL
 * blocks labeled [DATA_SYNC_SECTION] in the implementation file.
 */
class BinaryStarApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * This will bind or connect all sockets.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param isBstarEnabled whether to enable the "Binary Star" high availability
   *                       (HA) feature
   * @param isBstarPrimary whether this controller is the "primary" in the high
   *                       availability (HA) configuration
   * @param pubSockUrl the "Binary Star" publisher address, i.e. the ZMQ `PUB`
   *                   socket URL on which peerPubSock_ binds
   * @param peerPubSockUrl the peer's "Binary Star" publisher address, i.e. the
   *                       ZMQ `SUB` socket URL to which peerSubSock_ connects
   * @param versionFile the path to the current software version file
   */
  BinaryStarApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      bool isBstarEnabled,
      bool isBstarPrimary = true,
      const std::string& pubSockUrl = "",
      const std::string& peerPubSockUrl = "",
      const std::string& versionFile = "");

  /** Wrapper for data synced with apps. */
  struct SyncedAppData {
    /** Set the topology. */
    void setTopology(const thrift::Topology& topology) {
      fullAppData.topology_ref() = topology;
      unsyncedAppData.topology_ref() = topology;
    }
    /** Set the network overrides. */
    void setNetworkOverrides(const std::string& configNetworkOverrides) {
      fullAppData.configNetworkOverrides_ref() = configNetworkOverrides;
      unsyncedAppData.configNetworkOverrides_ref() = configNetworkOverrides;
    }
    /** Set the user node overrides. */
    void setNodeOverrides(const std::string& configNodeOverrides) {
      fullAppData.configNodeOverrides_ref() = configNodeOverrides;
      unsyncedAppData.configNodeOverrides_ref() = configNodeOverrides;
    }
    /** Set the automatic node overrides. */
    void setAutoNodeOverrides(const std::string& configAutoNodeOverrides) {
      fullAppData.configAutoNodeOverrides_ref() = configAutoNodeOverrides;
      unsyncedAppData.configAutoNodeOverrides_ref() = configAutoNodeOverrides;
    }
    /** Set the controller config. */
    void setControllerConfig(const std::string& configController) {
      fullAppData.configController_ref() = configController;
      unsyncedAppData.configController_ref() = configController;
    }
    /** Clear all data. */
    void clear() {
      fullAppData = thrift::BinaryStarAppData();
      unsyncedAppData = thrift::BinaryStarAppData();
    }
    /** The full app data. */
    thrift::BinaryStarAppData fullAppData;
    /** The unsynced app data. */
    thrift::BinaryStarAppData unsyncedAppData;
  };

 private:
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process a FSM (finite-state machine) change from the broker. */
  void processBstarFsm(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a FSM (finite-state machine) state request. */
  void processBstarGetState(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a heartbeat from the peer controller. */
  void processBstarSync(const thrift::Message& message);

  /**
   * Send a heartbeat to the peer. This heartbeat will also contain all
   * synchronization information (app data, sequence number).
   */
  void sendHeartbeat();

  /**
   * Perform actions related to a state change to ACTIVE:
   * - Reset the heartbeat sequence number.
   * - Request new app data from all apps.
   */
  void doActiveStateChangeActions();

  /**
   * Perform actions related to a backup ACTIVE yielding to the primary:
   * - Set the FSM state to BACKUP and send a heartbeat to the peer.
   * - Send commands to all minions to switch to the other controller.
   */
  void yieldToPrimary();

  /** Clear all internal state, excluding FSM state. */
  void clear();

  /** Log a FSM (finite-state machine) state change. */
  void logStateChange(
      const thrift::BinaryStarFsmState& oldState,
      const thrift::BinaryStarFsmState& newState);

  /** Send a Thrift object to the peer through peerPubSock_. */
  template <class T>
  void
  sendToPeer(thrift::MessageType mType, T obj, bool compress = false) {
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto ret = peerPubSock_.sendThriftObj(msg, serializer_);
    if (ret.hasError()) {
      LOG(ERROR) << "Error sending "
                 << TEnumMapFactory<thrift::MessageType>::
                     makeValuesToNamesMap().at(mType)
                 << " to peer: " << ret.error();
    }
  }

  /** Whether "Binary Star" replication is enabled. */
  bool isBstarEnabled_{false};

  /** The primary (true) or backup (false) controller in the pair. */
  bool isBstarPrimary_{true};

  /** The ZMQ `PUB` socket to publish heartbeats/acks to the peer. */
  fbzmq::Socket<ZMQ_PUB, fbzmq::ZMQ_SERVER> peerPubSock_;

  /** The ZMQ `SUB` socket to subscribe to heartbeats/acks from the peer. */
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> peerSubSock_;

  /** Serializer for all messages. */
  apache::thrift::CompactSerializer serializer_{};

  /** The controller version string. */
  std::string version_;

  /** Periodic heartbeat timer to the other controller. */
  std::unique_ptr<fbzmq::ZmqTimeout> heartbeatTimeout_{nullptr};

  /** The current "Binary Star" FSM (finite-state machine). */
  thrift::BinaryStar bstarFsm_;

  /**
   * The current heartbeat sequence number.
   * This value is ONLY incremented when sending new app data (NOT every
   * heartbeat), and is reset to 0 upon becoming ACTIVE.
   */
  int32_t seqNum_{0};

  /**
   * If ACTIVE, whether to send the full app data on the next heartbeat
   * (instead of only unsynced data).
   */
  bool sendFullData_{false};

  /**
   * The millisecond timestamp on the last heartbeat received.
   * This is only used for triggering automatic recovery of the primary.
   */
  int64_t lastHeartbeatTime_{0};

  /**
   * The number of successive heartbeats received so far from the primary,
   * used for triggering automatic recovery.
   */
  int32_t autoRecoveryHeartbeats_{0};
};

} // namespace terragraph
} // namespace facebook
