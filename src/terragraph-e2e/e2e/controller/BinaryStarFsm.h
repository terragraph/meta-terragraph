/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/Expected.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Finite-state machine (FSM) for a (heavily modified) "Binary Star" (BSTAR)
 * primary-backup replication scheme.
 *
 * In this setup, we run two controllers, a "primary" and "backup", on separate
 * machines. If the primary catastrophically fails (e.g. power outage, network
 * failure, hardware failure, etc.), the backup will assume control of the
 * Terragraph E2E network.
 *
 * Original "Binary Star" design:
 * http://zguide.zeromq.org/php:chapter4#High-Availability-Pair-Binary-Star-Pattern
 *
 * High-level protocol overview:
 * - Each controller's state consists of an initial fixed configuration
 *   (primary, backup) as well as a runtime FSM state (PRIMARY, BACKUP, ACTIVE,
 *   PASSIVE). There are two "steady states" for the primary-backup pair:
 *    1. ACTIVE primary <--> PASSIVE (or offline) backup
 *    2. ACTIVE backup <--> PASSIVE (or offline) primary
 * - The controllers (peers) stay in their initial states (PRIMARY/BACKUP) until
 *   they hear their peer's state, triggering the change to ACTIVE/PASSIVE.
 * - The peers exchange periodic heartbeats to determine liveness.
 * - The E2E minion (client) only connects to one controller at any given time.
 *   If it receives no response from the current controller for a set period
 *   (timeout), it will disconnect and try connecting to the other controller.
 * - An inactive (BACKUP/PASSIVE) controller only assumes control (becomes
 *   ACTIVE) when two conditions are met simultaneously:
 *    1. It has missed too many heartbeats from the peer (peer timeout).
 *    2. It receives a message from a client (client request event).
 * - Split-brain is avoided ONLY if it is impossible to partition the network
 *   such that a subset of nodes can see each controller while the controllers
 *   cannot see each other.
 *
 * Architecture:
 * - BinaryStarFsm (controller):
 *     Contains a static function representing the state machine. The actual
 *     states, events, and structs are defined in Controller.thrift.
 * - Broker (controller):
 *     Maintains a copy of the BSTAR FSM. When receiving a client request, the
 *     FSM will indicate whether to drop the request (when inactive), forward it
 *     (when active), or trigger failover (to become active).
 * - BinaryStarApp (controller):
 *     Maintains a synchronized copy of the BSTAR FSM, as well as ZMQ PUB/SUB
 *     sockets with the peer (for sending/receiving heartbeats). This is the
 *     main driver for the FSM, and also handles data synchronization between
 *     peers and with controller apps.
 * - Other controller apps:
 *     Install handlers to send data (if ACTIVE) or receive data (if PASSIVE)
 *     to/from BinaryStarApp.
 * - Broker (minion):
 *     Read the primary and backup controller URL, and switch between them if
 *     the current connection times out.
 *
 * Major modifications to the original "Binary Star" protocol:
 * - We add a protocol for synchronizing persistent data between the two
 *   controllers; refer to BinaryStarApp for details.
 * - We assume the primary and backup servers are NOT equal; rather, the primary
 *   is preferred. The FSM is modified accordingly (e.g. when PRIMARY sees
 *   PASSIVE BACKUP, then PRIMARY should become ACTIVE, not the other way).
 * - We attempt automatic recovery: if ACTIVE BACKUP sees that PRIMARY has come
 *   back online, it will yield control once it deems PRIMARY to be stable.
 * - The primary controller must be started BEFORE the backup (order matters!).
 *   Otherwise, the backup may become ACTIVE (until automatic recovery occurs).
 * - If an exceptional FSM state occurs (e.g. peers reconnecting after a
 *   network partition to find they have split-brained), peers will attempt to
 *   recover by reverting to their initial states (PRIMARY/BACKUP).
 */
class BinaryStarFsm {
 public:
  /**
   * Process an event in the given FSM, and return the new FSM state.
   *
   * Upon failure, the failure reason will be returned:
   * - If 'event' is a client request, the request should be dropped.
   * - If 'event' is an FSM state, the failure is fatal.
   */
  static folly::Expected<thrift::BinaryStar, std::string> processEvent(
      const thrift::BinaryStar& fsm, const thrift::BinaryStarFsmEvent& event);

  /** Returns the current timestamp as Unix time in milliseconds. */
  static int64_t getTimeMs();
};

} // namespace terragraph
} // namespace facebook
