/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BinaryStarFsm.h"

#include <chrono>

namespace facebook {
namespace terragraph {

folly::Expected<thrift::BinaryStar, std::string>
BinaryStarFsm::processEvent(
    const thrift::BinaryStar& fsm, const thrift::BinaryStarFsmEvent& event) {
  thrift::BinaryStar ret(fsm);

  switch (fsm.state) {
    // PRIMARY: Wait to become ACTIVE or PASSIVE once we hear from our peer.
    // Meanwhile, accept client connections (e.g. if peer is not running).
    // NOTE: We modify the original FSM so that the primary still becomes ACTIVE
    // upon hearing from a PASSIVE peer, instead of switching roles.
    case thrift::BinaryStarFsmState::STATE_PRIMARY:
      if (event == thrift::BinaryStarFsmEvent::PEER_BACKUP ||
          event == thrift::BinaryStarFsmEvent::PEER_PASSIVE) {
        LOG(INFO) << "PRIMARY: Connected to peer backup (PASSIVE), "
                     "becoming ACTIVE...";
        ret.state = thrift::BinaryStarFsmState::STATE_ACTIVE;
      } else if (event == thrift::BinaryStarFsmEvent::PEER_ACTIVE) {
        LOG(INFO) << "PRIMARY: Connected to peer backup (ACTIVE), "
                     "becoming PASSIVE...";
        ret.state = thrift::BinaryStarFsmState::STATE_PASSIVE;
      } else if (event == thrift::BinaryStarFsmEvent::PEER_PRIMARY) {
        LOG(ERROR) << "PRIMARY: Peer also reported primary, possible "
                      "misconfiguration?";
        return folly::makeUnexpected(std::string("FATAL: Dual-Primary"));
      }
      break;

    // BACKUP: Wait to become ACTIVE or PASSIVE once we hear from our peer.
    // Meanwhile, reject client connections.
    // NOTE: We modify the original FSM so that the backup can become ACTIVE
    // without hearing from the peer, in case backup restarts while primary is
    // still offline. Thus, the primary must be started before the backup.
    case thrift::BinaryStarFsmState::STATE_BACKUP:
      if (event == thrift::BinaryStarFsmEvent::PEER_ACTIVE) {
        LOG(INFO) << "BACKUP: Connected to peer primary (ACTIVE), "
                     "becoming PASSIVE...";
        ret.state = thrift::BinaryStarFsmState::STATE_PASSIVE;
      } else if (event == thrift::BinaryStarFsmEvent::PEER_BACKUP) {
        LOG(ERROR) << "BACKUP: Peer also reported backup, possible "
                      "misconfiguration?";
        return folly::makeUnexpected(std::string("FATAL: Dual-Backup"));
      } else if (event == thrift::BinaryStarFsmEvent::CLIENT_REQUEST) {
        if (getTimeMs() >= fsm.peerExpiry) {
          // If peer is dead, switch to the active state
          LOG(INFO) << "BACKUP: Failover triggered, becoming ACTIVE...";
          ret.state = thrift::BinaryStarFsmState::STATE_ACTIVE;
        } else {
          // If peer is alive, reject connections
          return folly::makeUnexpected(std::string("Peer is alive"));
        }
      }
      break;

    // ACTIVE: Nothing to do, remains ACTIVE until the process ends.
    case thrift::BinaryStarFsmState::STATE_ACTIVE:
      if (event == thrift::BinaryStarFsmEvent::PEER_ACTIVE) {
        // Two actives would mean split-brain
        LOG(ERROR) << "ACTIVE: Peer reported ACTIVE (dual-active split-brain)!";
        return folly::makeUnexpected(
            std::string("FATAL: Split-Brain (Dual-Active)"));
      }
      break;

    // PASSIVE: Wait for peer state change, or trigger failover upon seeing
    // a client request if the peer looks dead.
    // NOTE: We modify the original FSM so that PASSIVE will not become ACTIVE
    // upon hearing from the primary (see comments above).
    case thrift::BinaryStarFsmState::STATE_PASSIVE:
      if (event == thrift::BinaryStarFsmEvent::PEER_BACKUP) {
        // Peer is restarting - become active, peer will go passive
        LOG(INFO) << "PASSIVE: Backup restarted, becoming ACTIVE...";
        ret.state = thrift::BinaryStarFsmState::STATE_ACTIVE;
      } else if (event == thrift::BinaryStarFsmEvent::PEER_PASSIVE) {
        // Two passives would mean cluster would be non-responsive
        LOG(ERROR) << "PASSIVE: Peer reported PASSIVE (dual-passive)!";
        return folly::makeUnexpected(std::string("FATAL: Dual-Passive"));
      } else if (event == thrift::BinaryStarFsmEvent::CLIENT_REQUEST) {
        if (getTimeMs() >= fsm.peerExpiry) {
          // If peer is dead, switch to the active state
          LOG(INFO) << "PASSIVE: Failover triggered, becoming ACTIVE...";
          ret.state = thrift::BinaryStarFsmState::STATE_ACTIVE;
        } else {
          // If peer is alive, reject connections
          return folly::makeUnexpected(std::string("Peer is alive"));
        }
      }
      break;
  }

  return ret;
}

int64_t
BinaryStarFsm::getTimeMs() {
  return static_cast<int64_t>(  // really 'unsigned long', but thrift :/
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace terragraph
} // namespace facebook
