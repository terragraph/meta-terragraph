/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatusApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/String.h>

#include "../common/Consts.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace fbzmq;

DEFINE_string(version_file, "/etc/tgversion", "Version file");

namespace facebook {
namespace terragraph {
namespace stats {

StatusApp::StatusApp(fbzmq::Context& context, const std::string& routerSockUrl)
    : AggrApp(context, routerSockUrl, NMSConsts::kStatusAppAggrId),
      context_(context) {
  // Get aggregator version string
  if (!folly::readFile(FLAGS_version_file.c_str(), version_)) {
    LOG(ERROR) << "Could not read aggregator version file";
  } else {
    version_ = folly::trimWhitespace(version_).str();
    LOG(INFO) << "Current Aggregator Version: " << version_;
  }
}

void
StatusApp::processMessage(
    const std::string& agent,
    const std::string& senderApp,
    const thrift::AggrMessage& message) noexcept {
  switch (message.mType) {
    case thrift::AggrMessageType::GET_STATUS_DUMP:
      processGetStatusDump(senderApp, message);
      break;
    case thrift::AggrMessageType::IS_ALIVE:
      processIsAlive(senderApp);
      break;
    default:
      LOG(ERROR) << "Wrong type of message ("
                 << folly::get_default(
                        TEnumMapFactory<thrift::AggrMessageType>::
                            makeValuesToNamesMap(),
                        message.mType,
                        "UNKNOWN")
                 << ") received from " << agent << ":" << senderApp;
  }
}

void
StatusApp::processGetStatusDump(
    const std::string& senderApp, const thrift::AggrMessage& message) {
  VLOG(4) << "Request for status dump from " << senderApp;
  if (!maybeReadThrift<thrift::AggrGetStatusDump>(message)) {
    handleInvalidMessage("AggrGetStatusDump", senderApp);
    return;
  }

  thrift::AggrStatusDump dump{};
  dump.version_ref() = version_;
  sendToAggrApp(senderApp, thrift::AggrMessageType::STATUS_DUMP, dump);
}

void
StatusApp::processIsAlive(const std::string& senderApp) {
  VLOG(4) << "Received isAlive message from " << senderApp;
  sendAggrAck(senderApp, true, "Up");
}

} // namesapce stats
} // namespace terragraph
} // namespace facebook
