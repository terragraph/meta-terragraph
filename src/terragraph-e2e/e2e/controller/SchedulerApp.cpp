/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SchedulerApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/synchronization/Baton.h>
#include <folly/gen/Base.h>

#include "e2e/common/Consts.h"
#include "e2e/common/TimeUtils.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

namespace facebook {
namespace terragraph {

// Start scheduling at least this many seconds in the future
static const auto kCleanupInterval = std::chrono::seconds(5);
// Cleanup map slots up to this many seconds in the past
static const auto kCleanupSafetyMargin = std::chrono::seconds(5);

SchedulerApp::SchedulerApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kSchedulerAppCtrlId) {

  // These are defaults, overridable via CLI
  slotMapConfig_.slotLen = 16;
  slotMapConfig_.periodLen = 128;
  // Slots have to be sorted by start bwgd
  auto S = [](int start, int len) {
    thrift::Slot slot;
    slot.start = start;
    slot.len = len;
    return slot;
  };
  slotMapConfig_.mapping = {
      {thrift::SlotPurpose::SP_IM, {S(0, 5), S(64, 5)}},
      {thrift::SlotPurpose::SP_PBF, {S(13, 5), S(77, 5)}},
      {thrift::SlotPurpose::SP_RTAC,
       {S(25, 2),
        S(28, 2),
        S(31, 2),
        S(34, 2),
        S(89, 2),
        S(92, 2),
        S(95, 2),
        S(98, 2)}},
      {thrift::SlotPurpose::SP_HYBRID_PBF, {S(13, 10), S(77, 10)}},
      {thrift::SlotPurpose::SP_NULLING, {S(38, 5), S(102, 5)}},
      {thrift::SlotPurpose::SP_NULLING_APPLY, {S(58, 1), S(122, 1)}},
  };

  mapCleanupTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { cleanupSlotMap(); });
  mapCleanupTimeout_->scheduleTimeout(kCleanupInterval, true /* isPeriodic */);
}

void
SchedulerApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {

  switch (message.mType) {
    case thrift::MessageType::GET_SLOT_MAP_CONFIG:
      if (auto req = maybeReadThrift<thrift::GetSlotMapConfig>(message)) {
        processGetSlotMapConfig(senderApp, std::move(*req));
      } else {
        handleInvalidMessage("GetSlotMapConfig", senderApp, minion);
      }
      break;
    case thrift::MessageType::SET_SLOT_MAP_CONFIG:
      if (auto req = maybeReadThrift<thrift::SlotMapConfig>(message)) {
        processSetSlotMapConfig(senderApp, std::move(*req));
      } else {
        handleInvalidMessage("SlotMapConfig", senderApp, minion);
      }
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

uint64_t
SchedulerApp::adjustBwgd(
    thrift::SlotPurpose purpose,
    const uint64_t bwgd,
    uint32_t len,
    const std::string& txNode,
    const std::vector<std::string>& rxNodes) {
  uint64_t res;
  folly::Baton<> baton;
  runInEventLoop([&] {
    res = adjustBwgdInLoop(purpose, bwgd, len, txNode, rxNodes);
    baton.post();
  });
  baton.wait();
  return res;
}

uint64_t
SchedulerApp::adjustBwgdInLoop(
    thrift::SlotPurpose purpose,
    const uint64_t bwgd,
    uint32_t len,
    const std::string& txNode,
    const std::vector<std::string>& rxNodes) {
  auto iter = slotMapConfig_.mapping.find(purpose);
  CHECK(iter != slotMapConfig_.mapping.end());
  const std::vector<thrift::Slot>& slots = iter->second;

  uint64_t startSlot =
      (bwgd + slotMapConfig_.slotLen - 1) / slotMapConfig_.slotLen;
  uint32_t offset = startSlot % slotMapConfig_.periodLen;
  uint64_t periodStart = startSlot - offset;
  len = (len + slotMapConfig_.slotLen - 1) / slotMapConfig_.slotLen;

  // Ensure the configuration has at least 1 slot that can accommodate len
  CHECK(
      folly::gen::from(slots) |
      folly::gen::any([len](const thrift::Slot& slot) {
        return static_cast<uint32_t>(slot.len) >= len;
      }));

  // Generator source containing tx and rx nodes
  auto nodes = folly::gen::just(txNode) + folly::gen::from(rxNodes);

  // Find index of first slot that ends after offset in the current period
  size_t startIdx, offsetInSlot;
  for (startIdx = 0; startIdx < slots.size(); startIdx++) {
    if (static_cast<uint64_t>(slots[startIdx].start + slots[startIdx].len) >
        offset) {
      break;
    }
  }
  if (startIdx == slots.size()) {
    // If none end after offset, then the first slot in the next period will do
    startIdx = 0;
    offsetInSlot = 0;
    periodStart += slotMapConfig_.periodLen;
  } else {
    offsetInSlot = offset >= static_cast<uint32_t>(slots[startIdx].start)
                       ? offset - static_cast<uint32_t>(slots[startIdx].start)
                       : 0;
  }

  // Loop throuh periods until something is found. This is not an infinite loop
  // since there exists a slot larger than len, so in some period in the future
  // we'll also find such slot that is free
  for (;; periodStart += slotMapConfig_.periodLen) {
    // Loop through all slots, starting with stardIdx
    for (size_t s = startIdx; s < slots.size(); s++) {
      const thrift::Slot& slot = slots[s];
      // Skip if this slot is too short
      if (static_cast<uint32_t>(slot.len) < len) {
        continue;
      }
      // Find a position i (if any) within the slot s.t. [i, i+len) is free,
      // i.e. unused by the tx and rx nodes
      uint32_t freeLen = 0; // length of free slots block
      for (size_t i = offsetInSlot; i < static_cast<uint32_t>(slot.len); i++) {
        auto it = slotMap_.find(periodStart + slot.start + i);
        if (it != slotMap_.end() &&
            nodes | folly::gen::any([&it](const std::string& n) {
              return it->second.count(n) != 0;
            })) {
          // used
          freeLen = 0;
          continue;
        }
        // free
        freeLen++;
        if (freeLen >= len) {
          i -= len - 1;
          // [i, i+len) is our interval
          for (uint32_t j = 0; j < len; j++) {
            // mark used
            auto& used = slotMap_[periodStart + slot.start + i + j];
            nodes | [&used](const std::string& n) mutable { used.insert(n); };
          }
          return (periodStart + slot.start + i) * slotMapConfig_.slotLen;
        }
      }
      offsetInSlot = 0; // Start at the beginning of the next slot
    }
    startIdx = 0;
  }
}

void
SchedulerApp::cleanupSlotMap() noexcept {
  std::time_t end = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() - kCleanupSafetyMargin);
  uint64_t endSlot = TimeUtils::unixTimeToBwgd(end) / slotMapConfig_.slotLen;

  auto it = slotMap_.lower_bound(endSlot);
  // it points to the first element, which is >= endSlot,
  // or end() if all elements are < endBwgd
  slotMap_.erase(slotMap_.begin(), it);
}

void
SchedulerApp::processGetSlotMapConfig(
    const std::string& senderApp,
    const thrift::GetSlotMapConfig& /* config */) {
  sendToCtrlApp(
      senderApp, thrift::MessageType::SLOT_MAP_CONFIG, slotMapConfig_);
}

void
SchedulerApp::processSetSlotMapConfig(
    const std::string& senderApp, const thrift::SlotMapConfig& config) {
  for (const auto& pair : config.mapping) {
    int prevSlotEnd = -1;
    for (const thrift::Slot& slot : pair.second) {
      if (slot.start < prevSlotEnd) {
        sendE2EAck(senderApp, false, "Slots for each purpose must be sorted");
        return;
      }
      prevSlotEnd = slot.start + slot.len;
    }
  }
  slotMapConfig_ = config;
  sendE2EAck(senderApp, true, "Slot config set");
}

} // namespace terragraph
} // namespace facebook
