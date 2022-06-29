/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BaseCounterMonitor.h"

#include <chrono>
#include <glog/logging.h>

#include "MinionClient.h"
#include "StatCache.h"

namespace facebook {
namespace terragraph {

DEFINE_string(
    driver_if_socket_id,
    "driver-if_counter_monitor",
    "ZMQ identity to use for driver-if stats publisher");
DEFINE_string(
    driver_if_pub_url,
    "tcp://[::1]:18990",
    "ZMQ URL for driver-if stats publisher");
DEFINE_int32(
    link_keys_refresh_interval_ms,
    2000,
    "Interval for refreshing link keys cache");

// subscribe to a stats stream, parse <link_name> to <metric name>
BaseCounterMonitor::BaseCounterMonitor(
    const std::vector<StatFormat>& statFormat)
    : statFormat_(statFormat) {
  // refresh link keys periodically
  linkKeysTimer_ =
      fbzmq::ZmqTimeout::make(this, [this]() noexcept { refreshLinkKeys(); });
  linkKeysTimer_->scheduleTimeout(
      std::chrono::milliseconds(FLAGS_link_keys_refresh_interval_ms),
      true /* periodic */);
  prepare();
}

void
BaseCounterMonitor::refreshLinkKeys() {
  VLOG(1) << "Refreshing link keys";
  std::unique_ptr<facebook::terragraph::MinionClient> minionClient =
      std::make_unique<facebook::terragraph::MinionClient>(context_);
  auto apiResp = minionClient->getLinkStatusDump();
  if (!apiResp) {
    LOG(ERROR) << "Unable to fetch link status dump from e2e minion.";
    return;
  }
  // generate key mappings for each link (map raw key name to short name)
  std::unordered_map<std::string, RadioMetric> linkKeys{};
  for (const auto& link : apiResp.value().linkStatusDump) {
    if (!link.second.radioMac_ref().has_value() ||
        !link.second.ifname_ref().has_value()) {
      LOG(ERROR) << "Missing radioMac and/or ifName for responder mac: "
                 << link.second.responderMac;
      continue;
    }
    // generate expected stat names for a responder mac
    auto curLinkKeys = StatCache::generateLinkKeys(statFormat_, link.second);
    // append to the link keys list
    linkKeys.insert(curLinkKeys.begin(), curLinkKeys.end());
  }
  // copy newly generated keys
  linkKeys_ = linkKeys;
}

void
BaseCounterMonitor::prepare() noexcept {
  // add driver-if socket
  // TODO - we should allow a list of sockets
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> zmqSock(
      context_, fbzmq::IdentityString(FLAGS_driver_if_socket_id));
  auto res = zmqSock.connect(fbzmq::SocketUrl{FLAGS_driver_if_pub_url});
  if (res.hasError()) {
    LOG(ERROR) << "Unable to connect to driver-if socket";
    return;
  }
  zmqSock.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();
  csSubSockList_.emplace_back(std::move(zmqSock));
  for (size_t i = 0; i < csSubSockList_.size(); i++) {
    addSocket(
        fbzmq::RawZmqSocketPtr{*csSubSockList_[i]},
        ZMQ_POLLIN,
        [ this, i ](int) noexcept {
          auto res = csSubSockList_[i].recvThriftObj<fbzmq::thrift::MonitorPub>(
              serializer_, std::chrono::milliseconds{1000});
          if (res.hasError()) {
            LOG(ERROR) << "Error reading message: " << res.error();
            return;
          }

          auto& message = res.value();
          if (message.pubType_ref().value() ==
              fbzmq::thrift::PubType::COUNTER_PUB)
          {
            processCountersMessage(message.counterPub_ref().value());
          }
        });
  }
}

void
BaseCounterMonitor::processCountersMessage(
    fbzmq::thrift::CounterValuesResponse& counters) {
  // keep track of matching metrics
  std::vector<RadioStat> metricList;
  for (const auto& kv : counters.counters_ref().value()) {
    // parse key string into key + baseband entity
    const stats::StatInfo info(kv.first); // key, entity
    auto linkKeysIter = linkKeys_.find(info.key);
    if (linkKeysIter == linkKeys_.end()) {
      continue;
    }
    RadioStat metricValue{linkKeysIter->second.radioMac,
                          linkKeysIter->second.responderMac,
                          linkKeysIter->second.shortName,
                          kv.second.timestamp_ref().value(),
                          kv.second.value_ref().value()};
    metricList.push_back(metricValue);
  }
  if (!metricList.empty()) {
    // send processed metrics
    processStats(metricList);
  }
}

} // namespace terragraph
} // namespace facebook
