/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>

#include "CtrlApp.h"
#include "topology/TopologyWrapper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/PassThru_types.h"

namespace facebook {
namespace terragraph {

/**
 * App that handles link ignition (wireless link bring-up).
 *
 * This app primarily communicates with a separate IgnitionApp on the E2E
 * minion.
 *
 * Summary of this app's functions:
 * - Periodically search for dead links emanating from all reachable/live nodes,
 *   then try to ignite eligible links on each iteration.
 * - React to link failures reported from nodes by marking the link as down,
 *   and later try to re-ignite the link in the regular ignition procedure.
 * - Node unreachability will result in all the incoming and outgoing links
 *   of the node being marked down. Ignition attempts will be made on them
 *   as described above.
 */
class IgnitionApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param extendedDampenInterval the minimum time before re-igniting the same
   *                               link after 'extendedDampenFailureInterval' of
   *                               failed ignition attempts
   * @param extendedDampenFailureInterval the minimum duration of successive
   *                                      failed ignition attempts across the
   *                                      same link before changing the dampen
   *                                      interval to 'extendedDampenInterval'
   * @param backupCnLinkInterval the minimum time that must elapse before trying
   *                             to ignite using backup CN links
   * @param p2mpAssocDelay the minimum time before igniting successive P2MP links
   * @param ignoreDampenIntervalAfterResp whether to ignore the regular dampen
   *                                      interval upon receiving a link-down
   *                                      event from a node
   */
  IgnitionApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      std::chrono::seconds extendedDampenInterval,
      std::chrono::seconds extendedDampenFailureInterval,
      std::chrono::seconds backupCnLinkInterval,
      std::chrono::seconds p2mpAssocDelay,
      bool ignoreDampenIntervalAfterResp);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process thrift::SetLinkStatusReq (usually from API/CLI). */
  void processSetLinkStatusReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::ForceDissocReq. */
  void processForceDissocReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process thrift::IgnitionParams. */
  void processSetIgnitionParams(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::GetIgnitionState. */
  void processGetIgnitionState(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);
  /** Process thrift::LinkStatusEvent (from TopologyApp). */
  void processLinkStatusEvent(
      const std::string& senderApp, const thrift::Message& message);

  /** Return the current ignition params. */
  thrift::IgnitionParams getIgnitionParams() const;

  /**
   * Main network ignition function.
   * - If auto-ignition is enabled, pick a set of links to send ignition
   *   requests (LINK_UP) to.
   * - If auto-ignition is disabled, only request link status (GET_LINK_STATUS).
   */
  void linkUpTimeoutExpired();

  /** Process thrift::SetLinkStatusReq (LINK_UP). */
  void processLinkUpReq(
      const thrift::IgnitionCandidate& igCandidate,
      const thrift::Link& link,
      const thrift::Node& initiatorNode,
      const thrift::Node& responderNode,
      const std::string& senderApp);
  /** Process thrift::SetLinkStatusReq (LINK_DOWN). */
  void processLinkDownReq(
      const thrift::IgnitionCandidate& igCandidate,
      const thrift::Link& link,
      const std::string& senderApp);

  /**
   * Send thrift::SetLinkStatus to an E2E minion.
   *
   * This does not perform any validation on 'igCandidate'.
   */
  void sendSetLinkStatus(
      const thrift::IgnitionCandidate& igCandidate,
      const thrift::LinkStatusType& linkStatus,
      const std::string& source);

  /**
   * Remove entries from linkToInitialAttemptTs_ if both ends of a link went
   * offline.
   */
  void cleanUpInitialLinkUpAttempts(const TopologyWrapper& topologyW);

  /**
   * Remove entries from cnToPossibleIgnitionTs_ if the other ends of all links
   * to a CN went offline.
   */
  void cleanUpCnLinkUpAttempts(const TopologyWrapper& topologyW);

  /**
   * Remove entries from radioToLinkUpTs_ if all links to a radio went offline.
   */
  void cleanUpRadioLinkUpRecords(const TopologyWrapper& topologyW);

  /**
   * Ignition loop interval (for linkupTimeout_) at which all new ignition
   * attempts are made.
   */
  std::chrono::seconds linkUpInterval_{5};

  /**
   * The ignition timeout on each minion, also accounting for network delays.
   *
   * This represents the duration that each initiator will sweep (if no response
   * was received) before giving up. Controller should not send subsequent
   * ignition commands faster than this.
   */
  std::chrono::seconds bfTimeout_{15 + 1 /* margin for network delays */};

  /**
   * The minimum time before re-igniting the same link if no link status was
   * reported to the controller.
   */
  std::chrono::seconds dampenInterval_{10};

  /**
   * The minimum time before re-igniting the same link after
   * extendedDampenFailureInterval_ of failed ignition attempts.
   */
  std::chrono::seconds extendedDampenInterval_;

  /**
   * The minimum duration of successive failed ignition attempts across the same
   * link before changing the dampen interval to extendedDampenInterval_.
   */
  std::chrono::seconds extendedDampenFailureInterval_;

  /**
   * The minimum time that must elapse before trying to ignite using backup
   * links (starting from when a DN-to-CN link could have been ignited from
   * either a primary or backup link).
   */
  std::chrono::seconds backupCnLinkInterval_;

  /** The minimum time before igniting successive P2MP links. */
  std::chrono::seconds p2mpAssocDelay_;

  /**
   * Whether to ignore dampenInterval_ upon receiving a link-down event from a
   * node.
   */
  bool ignoreDampenIntervalAfterResp_{false};

  /** The last ignition candidates. */
  std::vector<thrift::IgnitionCandidate> lastIgCandidates_{};

  /** Timer to periodically perform network-wide link ignition. */
  std::unique_ptr<fbzmq::ZmqTimeout> linkupTimeout_{nullptr};

  /**
   * Mapping from links to the MOST RECENT ignition attempt made.
   *
   * This is cleared by processLinkStatusEvent() as an optimization.
   */
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToAttemptTs_;

  /**
   * Mapping from ignition initiator node name to the MOST RECENT ignition
   * attempt made from that initiator.
   *
   * This is ONLY used to track the minion's own ignition timeout and prevent
   * sending ignition commands faster than `bfTimeout_`. We clear this entry
   * upon receiving `LinkStatusEvent` from a node.
   */
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      initiatorToAttemptTs_;

  /**
   * Mapping from links to the OLDEST ignition attempt made.
   *
   * This is cleared when a link comes up or in cleanUpInitialLinkUpAttempts().
   */
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      linkToInitialAttemptTs_;

  /**
   * Mapping from CNs to the EARLIEST time an ignition attempt could have been
   * made (but was not necessarily made, e.g. with backup links).
   *
   * This is cleared when a link comes up or in cleanUpCnLinkUpAttempts().
   */
  std::unordered_map<std::string, std::chrono::steady_clock::time_point>
      cnToPossibleIgnitionTs_;

  /**
   * Mapping from radio MACs to the MOST RECENT received LINK_UP.
   *
   * This is cleared when LINK_DOWN is received on the same radio/link or in
   * cleanUpRadioLinkUpRecords().
   */
  std::unordered_map<
      std::string /* radio MAC */,
      std::pair<
          std::chrono::steady_clock::time_point, std::string /* link name */>>
              radioToLinkUpTs_;

  /** Per-link auto-ignition control. */
  std::unordered_set<std::string> linkAutoIgniteOff_{};

  /**
   * Per-node starting index for iteration in the list of associated links,
   * used to try links in round-robin fashion (and not get stuck on failures).
   */
  std::unordered_map<std::string /* node name */, size_t> linkupIterationIndex_;

  /** Is auto-ignition enabled? */
  bool autoIgnitionEnabled_{true};
};

} // namespace terragraph
} // namespace facebook
