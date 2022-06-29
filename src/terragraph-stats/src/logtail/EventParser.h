/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "LogTailer.h"

#include <memory>
#include <regex>
#include <string>
#include <unordered_map>

#include <fbzmq/zmq/Context.h>

#include "e2e/common/EventClient.h"
#include "e2e/if/gen-cpp2/Event_types.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"

using apache::thrift::detail::TEnumMapFactory;

namespace facebook {
namespace terragraph {
namespace stats {

/*
 * EventParser logs events that match user-defined regexes in config.
 */
class EventParser final : public LogTailer {
 public:
  EventParser(
      fbzmq::Context& context,
      const std::string& macAddr,
      const std::string& configFileName,
      int32_t submissionInterval,
      uint32_t bufferSize);

 private:
  struct ProcessedEventFilter {
    std::regex regex;
    thrift::EventLevel level;

    ProcessedEventFilter(const std::string& regex, const std::string& level) {
      this->regex = std::regex(regex);
      this->level = TEnumMapFactory<thrift::EventLevel>::
          makeNamesToValuesMap().at(level.c_str());
    }
  };

  // ZMQ ID
  std::string myId_;

  // Event client
  std::unique_ptr<EventClient> eventClient_;

  std::unordered_map<
      std::string /* log source */,
      std::unordered_map<std::string /* event name */, ProcessedEventFilter>>
      processedEventFilters_;

  // From LogTailer
  void monitor() noexcept override;
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
