/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqTimeout.h>
#include <folly/io/async/EventBase.h>

#include "BasePublisher.h"
#include "../common/CompressionUtil.h"
#include "../common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

/*
 *  NmsPublisher publishes stats and node info to the NMS aggregator.
 */
class NmsPublisher final : public BasePublisher {
 public:
  NmsPublisher(
      fbzmq::Context& context,
      const std::string& macAddr,
      const std::string& aggregatorRouterUrl,
      const std::string& myNetworkInfoFile,
      const thrift::StatsAgentParams& statsAgentParams);

  // Cache event queue to disk
  void cacheEvents();

  // Process counter object
  void processCountersMessage(
      const fbzmq::thrift::CounterValuesResponse& counters) noexcept override;

  // Process event log object
  void processEventLogMessage(
      const fbzmq::thrift::EventLog& eventLog) noexcept override;

 private:
  // Stats queue type
  typedef std::unordered_map<std::string /* key */, thrift::AggrStat> StatsMap;

  // Initializes and configures aggregatorSock_
  void initAggregatorSock();

  // connect aggregatorSock_ to given url:
  // 1. Disconnect from previous aggregator if necessary.
  // 2. Only updates aggregatorRouterUrl_ if current connection succeeds.
  // 3. Try rollback if current connection fails
  void connectToAggregator(const std::string& aggrUrl);

  // Send the object to aggregator.
  template <
      class T,
      typename std::enable_if<!std::is_fundamental<T>::value>::type* = nullptr>
  bool
  sendToAggregator(
      std::string receiverId,
      thrift::AggrMessageType mType,
      T obj,
      bool compress = false) noexcept {
    thrift::AggrMessage msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto res = aggregatorSock_.sendMultiple(
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(NMSConsts::kNmsPublisherId).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());
    if (res.hasError()) {
      LOG(ERROR) << "Error sending "
                 << TEnumMapFactory<thrift::AggrMessageType>::
                     makeValuesToNamesMap().at(mType)
                 << " to :" << receiverId << " from NmsPublisher. "
                 << res.error();
      return false;
    }

    return true;
  }

  // Initializes ZMQ sockets
  void prepare() noexcept;

  // Periodically sends reports to the aggregator.
  void pushNmsReport() noexcept;

  // Periodically sends queued high-frequency stats to the aggregator.
  void pushHighFrequencyStats() noexcept;

  // (Re)connects to the aggregator upon a URL change.
  void checkAggregatorUrl() noexcept;

  // Push stats and events to the aggregator.
  void pushQueuedStatsAndEvents(
      const thrift::AggrMessageType& messageType,
      StatsMap& statsQueue,
      thrift::EventLog& eventLog);

  // Returns an aggregator URL from /tmp/mynetworkinfo,
  // or std::nullopt if no URL exists or an error occurred.
  std::optional<std::string> getAggrUrl() noexcept;

  // Load event log from disk and remove cache file
  void loadEvents();

  // The fixed aggregator URL. If set, /tmp/mynetworkinfo will be ignored.
  const std::string fixedAggregatorRouterUrl_{};

  // Interval at which we report stats
  std::chrono::seconds nmsDefaultReportInterval_;

  // Interval at which we report high-frequency stats
  std::chrono::seconds nmsHighFrequencyReportInterval_;

  // ZmqTimeout for performing periodic submission of stats
  std::unique_ptr<fbzmq::ZmqTimeout> nmsDefaultReportTimer_;

  // ZmqTimeout for performing periodic submission of high-frequency stats
  std::unique_ptr<fbzmq::ZmqTimeout> nmsHighFrequencyReportTimer_;

  // The current zmq url on which the router port in aggregator listens to
  std::string aggregatorRouterUrl_{};

  // Whether aggregatorSock_ is currently connected
  bool aggrConnected_{false};

  // dealer socket to connect to the router socket on the aggregator
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> aggregatorSock_;

  // Network information file
  const std::string myNetworkInfoFile_{};

  // Stat queues
  StatsMap statsQueuesLF_;
  StatsMap statsQueuesHF_;

  // Hash our mac address (for sharding, if multiple aggregators are running)
  size_t macHashValue_{0};

  // Maximum number of queued outgoing messages for aggregatorSock_ (ZMQ_SNDHWM)
  int zmqSndHwm_;

  // List of json seralized Event thrift structures for caching events
  thrift::EventLog eventLog_;
};

} // namespace stats
} // namepsace terragraph
} // namespace facebook
