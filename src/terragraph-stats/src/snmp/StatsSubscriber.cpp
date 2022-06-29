/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatsSubscriber.h"
#include "StatCache.h"

#include "stats/common/StatInfo.h"

#include <chrono>
#include <glog/logging.h>

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {

DEFINE_string(
    driver_if_socket_id,
    "driver-if_snmp_pub",
    "ZMQ identity to use for driver-if stats publisher");
DEFINE_string(
    driver_if_pub_url,
    "tcp://[::1]:18990",
    "ZMQ URL for driver-if stats publisher");

StatsSubscriber::StatsSubscriber(fbzmq::Context& context) : context_(context) {
  prepare();
}

void
StatsSubscriber::prepare() noexcept {
  // add driver-if socket
  fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> zmqSock(
      context_, fbzmq::IdentityString(FLAGS_driver_if_socket_id));
  auto res = zmqSock.connect(fbzmq::SocketUrl{FLAGS_driver_if_pub_url});
  if (res.hasError()) {
    LOG(FATAL) << "Unable to connect to driver-if socket";
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
            LOG(ERROR) << "Error reading message. " << res.error();
            return;
          }

          auto& message = res.value();
          VLOG(1) << "Received publication of type: "
                  << folly::get_default(
                         TEnumMapFactory<fbzmq::thrift::PubType>::
                             makeValuesToNamesMap(),
                         message.pubType_ref().value(),
                         "UNKNOWN");
          switch (message.pubType_ref().value()) {
            case fbzmq::thrift::PubType::COUNTER_PUB:
              processCountersMessage(message.counterPub_ref().value());
              break;
            default:
              VLOG(2) << "Skip unexpected publication of type: "
                      << folly::get_default(
                             TEnumMapFactory<fbzmq::thrift::PubType>::
                                 makeValuesToNamesMap(),
                             message.pubType_ref().value(),
                             "UNKNOWN");
          }
        });
  }
}

void
StatsSubscriber::processCountersMessage(
    fbzmq::thrift::CounterValuesResponse& counters) {
  auto keyCache = StatCache::getKeyNameCacheInstance()->rlock();
  for (const auto& kv : counters.counters_ref().value()) {
    // parse key string into key + baseband entity
    const stats::StatInfo info(kv.first);
    VLOG(1) << "Received counter publication. Key: " << info.key
            << " Entity: " << info.entity << " Value: "
            << kv.second.value_ref().value()
            << " TS: " << kv.second.timestamp_ref().value();
    auto keyIt = keyCache->find(info.key);
    if (keyIt != keyCache->end()) {
      // add stat to latest radioStats_ cache
      auto linkMetric = keyIt->second;
      VLOG(2) << "Adding cache for: " << info.key << " = "
              << kv.second.value_ref().value();
      auto radioStats = StatCache::getRadioStatsInstance()->wlock();
      auto& radioStat =
          (*radioStats)[linkMetric.localMac][linkMetric.remoteMac];
      // tag ifName
      radioStat.ifName = linkMetric.ifName;
      radioStat.lastUpdated = std::time(nullptr);
      // match stat to RadioStat entry
      switch (linkMetric.snmpColumn) {
        case SnmpColumn::SNR:
          radioStat.snr = (long)kv.second.value_ref().value();
          break;
        case SnmpColumn::RSSI:
          radioStat.rssi = (long)kv.second.value_ref().value();
          break;
        case SnmpColumn::MCS:
          radioStat.mcs = (u_long)kv.second.value_ref().value();
          break;
        default:
          LOG(INFO) << "Unknown radio stat: " << (int)linkMetric.snmpColumn;
      }
    }
  }
}

} // namespace terragraph
} // namespace facebook
