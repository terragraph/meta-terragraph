/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "LogTailer.h"

#include <string>
#include <type_traits>
#include <vector>

#include <fbzmq/zmq/Zmq.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "stats/common/CompressionUtil.h"
#include "stats/common/Consts.h"
#include "stats/if/gen-cpp2/Aggregator_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

/*
 * LogPublisher publishes stats and node info to the NMS aggregator.
 */
class LogPublisher final : public LogTailer {
 public:
  LogPublisher(
      fbzmq::Context& context,
      const std::string& macAddr,
      const std::string& configFileName,
      int32_t submissionInterval,
      uint32_t bufferSize,
      const std::string& myNetworkInfoFile);

 private:
  // The zmq url on which the router port in aggregator listens to
  std::string aggregatorRouterUrl_;

  bool aggregatorConnected_{false};

  // dealer socket to connect to the router socket on the aggregator
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> aggregatorSock_;

  // All message exchanges get serialized with this serializer
  apache::thrift::CompactSerializer serializer_{};

  // Internal buffer for processed logs yet to be sent to Aggregator
  std::vector<thrift::AggrSyslog> syslogsBuffer_{};

  size_t macHashValue_{0};

  // Network information file
  const std::string myNetworkInfoFile_{};

  // Send the object to aggregator.
  template <
      class T,
      typename std::enable_if<!std::is_fundamental<T>::value>::type* = nullptr>
  bool
  sendToAggregator(
      std::string receiverId,
      thrift::AggrMessageType mType,
      T obj,
      bool compress = false) {
    thrift::AggrMessage msg;
    errno = 0;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    if (compress) {
      CompressionUtil::compress(msg);
    }

    auto res = aggregatorSock_.sendMultiple(
        fbzmq::Message::from(receiverId).value(),
        fbzmq::Message::from(NMSConsts::kLogPublisherId).value(),
        fbzmq::Message::fromThriftObj(msg, serializer_).value());
    if (res.hasError()) {
      LOG(ERROR) << "Error sending "
                 << TEnumMapFactory<thrift::AggrMessageType>::
                     makeValuesToNamesMap().at(mType) << " to "
                 << receiverId << " from LogPublisher: " << res.error();
      return false;
    }

    return true;
  }

  std::optional<std::string> getAggregatorUrl() noexcept;

  // connect aggregatorSock_ to given url:
  // 1. Disconnect from previous aggregator if necessary.
  // 2. Only updates aggregatorRouterUrl_ if current connection succeeds.
  // 3. Try rollback if current connection fails
  void connectToAggregator(const std::string& aggrUrl);

  void checkAggregatorUrl() noexcept;

  void fillSyslogBuffer();

  void flushSyslogs();

  // From LogTailer
  void monitor() noexcept override;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
