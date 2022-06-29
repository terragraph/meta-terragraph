/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BasePublisher.h"

#include <folly/Format.h>
#include <folly/MapUtil.h>

#include "SharedObjects.h"
#include "../common/Consts.h"
#include "e2e/common/JsonUtils.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

BasePublisher::BasePublisher(
    fbzmq::Context& context,
    const std::string& macAddr,
    const std::string& className,
    const thrift::StatsAgentParams& statsAgentParams)
    : context_(context),
      macAddr_(macAddr),
      statsBufferSize_{
          (size_t)statsAgentParams.publisherParams.statsBufferSize},
      eventsBufferSize_{
          (size_t)statsAgentParams.publisherParams.eventLogsBufferSize},
      convertToRate_{statsAgentParams.publisherParams.convertToRate},
      publishValueWithRate_{
          statsAgentParams.publisherParams.publishValueWithRate},
      className_(className) {
  // Load configs
  auto lockedNodeConfig = SharedObjects::getNodeConfigWrapper()->rlock();
  topologyName_ = lockedNodeConfig->getTopologyInfo()->topologyName;
  nodeName_ = lockedNodeConfig->getTopologyInfo()->nodeName;
  lockedNodeConfig.unlock();  // lockedNodeConfig -> NULL

  // Initialize ZMQ sockets
  prepare(statsAgentParams);

  // Initialize stats blacklist
  for (const auto& kv : statsAgentParams.publisherParams.statsBlacklist) {
    try {
      statsBlacklist_.push_back(std::regex(kv.second));
      VLOG(3) << "Blacklisting stats under group '" << kv.first << "': "
              << kv.second;
    } catch (const std::regex_error& ex) {
      LOG(ERROR) << "Ignoring malformed regex for stats blacklist group "
                 << kv.first << ": " << kv.second << " (" << ex.what() << ")";
    }
  }

  // Initialize high-frequency stats whitelist
  for (const auto& kv :
       statsAgentParams.publisherParams.highFrequencyStatsWhitelist) {
    try {
      highFrequencyStatsWhitelist_.push_back(std::regex(kv.second));
      VLOG(3) << "Whitelisting high-frequency stats under group '" << kv.first
               << "': " << kv.second;
    } catch (const std::regex_error& ex) {
      LOG(ERROR) << "Ignoring malformed regex for high-frequency stats "
                    "whitelist group " << kv.first << ": " << kv.second << " ("
                 << ex.what() << ")";
    }
  }
}

void
BasePublisher::prepare(
    const thrift::StatsAgentParams& statsAgentParams) noexcept {
  // open ZMQ sockets
  for (const auto& it : statsAgentParams.sources) {
    if (!it.second.enabled || it.second.zmq_url.empty()) {
      continue;
    }

    fbzmq::Socket<ZMQ_SUB, fbzmq::ZMQ_CLIENT> zmqSock(
        context_,
        fbzmq::IdentityString(
            folly::sformat("{}_{}_monitor_pub", it.first, className_)));

    // connect to monitor for stats
    VLOG(2) << "[" << className_ << "] Connecting to '" << it.second.zmq_url
            << "'";
    auto res = zmqSock.connect(fbzmq::SocketUrl{it.second.zmq_url});
    if (res.hasError()) {
      LOG(ERROR) << "Failed to connect to " << it.second.zmq_url << ". "
                 << res.error();
      continue;
    }

    // subscribe
    zmqSock.setSockOpt(ZMQ_SUBSCRIBE, "", 0).value();
    csSubSockList_.emplace_back(std::move(zmqSock));
  }

  // Add sockets
  for (size_t i = 0; i < csSubSockList_.size(); i++) {
    addSocket(
        fbzmq::RawZmqSocketPtr{*csSubSockList_[i]},
        ZMQ_POLLIN,
        [ this, i ](int) noexcept {
          auto res = csSubSockList_[i].recvThriftObj<fbzmq::thrift::MonitorPub>(
              serializer_, NMSConsts::kReadTimeout);
          if (res.hasError()) {
            LOG(ERROR) << "Error reading message: " << res.error();
            return;
          }

          auto& message = res.value();
          VLOG(5) << "Received publication of type: "
                  << folly::get_default(
                         TEnumMapFactory<fbzmq::thrift::PubType>::
                             makeValuesToNamesMap(),
                         message.pubType_ref().value(),
                         "UNKNOWN");
          switch (message.pubType_ref().value()) {
            case fbzmq::thrift::PubType::COUNTER_PUB:
              processCountersMessage(message.counterPub_ref().value());
              break;
            case fbzmq::thrift::PubType::EVENT_LOG_PUB:
              processEventLogMessage(message.eventLogPub_ref().value());
              break;
            default:
              VLOG(5) << "Skip unexpected publication of type: "
                      << folly::get_default(
                             TEnumMapFactory<fbzmq::thrift::PubType>::
                                 makeValuesToNamesMap(),
                             message.pubType_ref().value(),
                             "UNKNOWN");
          }
        });
  }
}

bool
BasePublisher::regexMatches(
    const std::string& key, const std::vector<std::regex>& regexes) {
  for (const auto& regex : regexes) {
    if (std::regex_match(key, regex)) {
      return true;
    }
  }
  return false;
}

bool
BasePublisher::isBlacklisted(const std::string& key) {
  return regexMatches(key, statsBlacklist_);
}

bool
BasePublisher::isWhitelisted(const std::string& key) {
  return regexMatches(key, highFrequencyStatsWhitelist_);
}

std::optional<thrift::Event>
BasePublisher::parseTerragraphEventLog(
    const fbzmq::thrift::EventLog& eventLog) const {
  // We only expect single-sample logs
  if (eventLog.samples_ref().value().size() != 1) {
    LOG(ERROR) << "Received EventLog with "
               << eventLog.samples_ref().value().size()
               << " samples, expected 1";
    return std::nullopt;
  }

  // Deserialize event from JSON
  auto maybeEvent =
      JsonUtils::deserializeFromJson<thrift::Event>(
          eventLog.samples_ref().value()[0]);
  if (!maybeEvent) {
    LOG(ERROR) << "Failed to deserialize event: "
               << eventLog.samples_ref().value()[0];
    return std::nullopt;
  }

  // Fill some empty fields
  if (!maybeEvent->entity_ref().has_value()) {
    maybeEvent->entity_ref() = macAddr_;
  }
  if (!maybeEvent->nodeId_ref().has_value()) {
    maybeEvent->nodeId_ref() = macAddr_;
  }
  if (!maybeEvent->nodeName_ref().has_value()) {
    if (!nodeName_.empty() && maybeEvent->nodeId_ref().value() == macAddr_) {
      maybeEvent->nodeName_ref() = nodeName_;
    }
  }
  if (!maybeEvent->topologyName_ref().has_value()) {
    if (!topologyName_.empty()) {
      maybeEvent->topologyName_ref() = topologyName_;
    }
  }

  return maybeEvent;
}

std::optional<double>
BasePublisher::getCounterRate(
    const std::string& key,
    const fbzmq::thrift::Counter& counter,
    const std::unordered_map<std::string, fbzmq::thrift::Counter>&
        prevValues) const {
  auto prevCounter = prevValues.find(key);
  if (prevCounter == prevValues.end()) {
    VLOG(3) << "Dropping first occurrence of counter '" << key
            << "' with value " << counter.value_ref().value();
    return std::nullopt;
  }
  double curValue = counter.value_ref().value();
  int64_t curTime = counter.timestamp_ref().value();
  double prevValue = prevCounter->second.value_ref().value();
  int64_t prevTime = prevCounter->second.timestamp_ref().value();
  if (curValue < prevValue) {
    VLOG(2) << "Dropping counter '" << key << "' with decreasing value (prev="
            << prevValue << ", cur=" << curValue << ")";
    return std::nullopt;
  } else if (curTime <= prevTime) {
    VLOG(4) << "Dropping counter '" << key
            << "' with non-increasing timestamp (prev=" << prevTime
            << ", cur=" << curTime << ")";
    return std::nullopt;
  } else {
    return (curValue - prevValue) / (double)(curTime - prevTime);
  }
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
