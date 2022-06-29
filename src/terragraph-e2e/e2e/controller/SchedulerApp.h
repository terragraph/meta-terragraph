/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/Controller_types.h"
#include <fbzmq/async/ZmqTimeout.h>

#include "CtrlApp.h"

namespace facebook {
namespace terragraph {

/**
 * App that handles slot scheduling.
 */
class SchedulerApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   */
  SchedulerApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl);

  /**
   * Adjust an operation `purpose` with an approximate starting BWGD of `bwgd`
   * and length `len` according to the defined slot map w.r.t. the given tx and
   * rx nodes and returns the adjusted BWGD.
   */
  uint64_t adjustBwgd(
      thrift::SlotPurpose purpose,
      uint64_t bwgd,
      uint32_t len,
      const std::string& txNode,
      const std::vector<std::string>& rxNodes);

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process thrift::GetSlotMapConfig. */
  void processGetSlotMapConfig(
      const std::string& senderApp, const thrift::GetSlotMapConfig& config);
  /** Process thrift::SlotMapConfig. */
  void processSetSlotMapConfig(
      const std::string& senderApp, const thrift::SlotMapConfig& config);

  /** @see adjustBwgd() */
  uint64_t adjustBwgdInLoop(
      thrift::SlotPurpose purpose,
      uint64_t bwgd,
      uint32_t len,
      const std::string& txNode,
      const std::vector<std::string>& rxNodes);

  /** Delete old (i.e. past) slotMap_ entries to free memory. */
  void cleanupSlotMap() noexcept;

  /** Timer for cleaning up past slots in the slots map. */
  std::unique_ptr<fbzmq::ZmqTimeout> mapCleanupTimeout_;

  /** Slot map configuration. */
  thrift::SlotMapConfig slotMapConfig_;

  /**
   * The actual slot map, from slot to set of nodes transmitting/receiving at
   * this slot.
   */
  std::map<uint64_t /* slot */, std::unordered_set<std::string /* mac */>>
      slotMap_;
};

} // namespace terragraph
} // namespace facebook
