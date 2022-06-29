/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Broker.h"

#include <chrono>
#include <cstdlib>

#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/dynamic.h>
#include <folly/json.h>

#include "e2e/common/CompressionUtil.h"
#include "e2e/common/TimeUtils.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

using apache::thrift::detail::TEnumMapFactory;
using namespace fbzmq;

namespace {
// Interval to read E2E controller URLs from mynetworkinfo
const std::chrono::seconds kGetCtrlUrlIntvl{5};

// If the destination ZMQ ID of a message in appsSock_ begins with this prefix,
// always forward it back to appsSock_ (and bypass controllerSock_).
const std::string kAppSockForwardPrefix{":FWD:"};

// Throttle per-destination ZMQ send error messages to once per X seconds,
// since one hung app (e.g. StatusApp) might produce tens of errors per second.
//
// Assume error log line is ~140 chars, logrotate stores ~10MB total logs
// = ~70k lines, so throttling to 1 line every 10 seconds would give ~1 week
// log retention for one unreachable destination.
const int64_t kZmqSendErrorLogThrottleSec{10};

// Max entries in zmqSendErrors_ (just to cap memory usage)
const size_t kZmqSendErrorMaxSize{100};
} // namespace

namespace facebook {
namespace terragraph {
namespace minion {

Broker::Broker(
    fbzmq::Context& zmqContext,
    const std::string& macAddr,
    const std::string& controllerRouterUrl,
    const std::string& appsSockBindUrl,
    const std::string& broadcastPubSockBindUrl,
    const std::chrono::seconds& ctrlSockTimeout,
    const std::string& myNetworkInfoFile)
    : zmqContext_{zmqContext},
      macAddr_{macAddr},
      appsSock_{zmqContext, IdentityString{E2EConsts::kBrokerMinionId}},
      broadcastPubSock_{zmqContext},
      broadcastPubSockBindUrl_{broadcastPubSockBindUrl},
      ctrlSockTimeout_{ctrlSockTimeout},
      myNetworkInfoFile_{myNetworkInfoFile} {

  // Prepare dealer socket to talk to the controller
  initControllerSock();

  // Time out controllerSock_ if we haven't heard from it in too long
  // (we should receive periodic STATUS_REPORT_ACK for minion's status report)
  ctrlSockTimeoutTimer_ = ZmqTimeout::make(this, [this]() noexcept {
    LOG(INFO) << "Controller socket timed out!";

    // If we have an alternate controller URL (for primary-backup replication),
    // switch to it now -- in the "Binary Star" scheme, this "client vote" is
    // what triggers the controller state change from inactive -> active
    if (usingPrimaryCtrlUrl_) {
      if (controllerBackupRouterUrl_.empty()) {
        LOG(INFO) << folly::format(
            "Reconnecting to primary controller URL `{}`",
            controllerPrimaryRouterUrl_);
        connectToCtrl(controllerPrimaryRouterUrl_);
      } else {
        LOG(INFO) << folly::format(
            "Switching to backup controller URL `{}`",
            controllerBackupRouterUrl_);
        usingPrimaryCtrlUrl_ = false;
        connectToCtrl(controllerBackupRouterUrl_);
      }
    } else {
      if (controllerPrimaryRouterUrl_.empty()) {
        LOG(INFO) << folly::format(
            "Reconnecting to backup controller URL `{}`",
            controllerBackupRouterUrl_);
        connectToCtrl(controllerBackupRouterUrl_);
      } else {
        LOG(INFO) << folly::format(
            "Switching to primary controller URL `{}`",
            controllerPrimaryRouterUrl_);
        usingPrimaryCtrlUrl_ = true;
        connectToCtrl(controllerPrimaryRouterUrl_);
      }
    }
  });

  currentCtrlUrl_ = controllerRouterUrl;
  connectToCtrl(controllerRouterUrl);

  // Get controller url from mynetworkinfo periodically
  getCtrlUrlTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { getCtrlUrl(); });
  getCtrlUrlTimer_->scheduleTimeout(kGetCtrlUrlIntvl, true /* periodic */);

  // Bind to broadcastPubSockBindUrl (optional)
  if (!broadcastPubSockBindUrl.empty()) {
    VLOG(1) << "Binding to '" << broadcastPubSockBindUrl << "'";
    if (broadcastPubSock_.bind(SocketUrl{broadcastPubSockBindUrl}).hasError()) {
      LOG(FATAL) << "Error binding to '" << broadcastPubSockBindUrl << "'";
    }
  }

  // -- Prepare the router socket to talk to all minion apps --

  // Bind to appsSockBindUrl
  VLOG(1) << "Binding to '" << appsSockBindUrl << "'";
  if (appsSock_.bind(SocketUrl{appsSockBindUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << appsSockBindUrl << "'";
  }

  LOG(INFO) << "Attaching router socket/event callback...";

  // Message on router socket
  addSocket(RawZmqSocketPtr{*appsSock_}, ZMQ_POLLIN, [this](int) noexcept {
    Message firstFrameMsg, minionMsg, receiverAppMsg, senderAppMsg, thriftMsg;

    const auto recvRet = appsSock_.recvMultiple(
        firstFrameMsg, minionMsg, receiverAppMsg, senderAppMsg, thriftMsg);
    if (recvRet.hasError()) {
      LOG(ERROR) << "Error reading message: " << recvRet.error();
      return;
    }

    const auto minion = minionMsg.read<std::string>().value();
    const auto receiverApp = receiverAppMsg.read<std::string>().value();
    const auto senderApp = senderAppMsg.read<std::string>().value();

    VLOG(4) << "Processing a message from " << senderApp << " to "
            << receiverApp;

    // Send message to controller through controllerSock_ if no "minion" part
    // HACK! We want to support direct access to minion's appsSock_
    // (e.g. from other local processes) and need to figure out when to route
    // messages from minion to controllerSock_ vs. the client on appsSock_.
    // Do this by looking at the client's ZMQ ID, and expect a special prefix.
    if (minion.empty() && !receiverApp.rfind(kAppSockForwardPrefix, 0) == 0) {
      if (currentCtrlUrl_.empty()) {
        // Drop message if controller url has not been propagated
        VLOG(2) << "Dropping message from " << senderApp << " to "
                << receiverApp << " (empty controller URL)";
        return;
      }
      const auto sendRet =
          controllerSock_.sendMultiple(receiverAppMsg, senderAppMsg, thriftMsg);
      if (sendRet.hasError() && recordZmqSendError(receiverApp)) {
        LOG(ERROR) << "Error routing msg from " << senderApp << " to "
                   << receiverApp << ": " << sendRet.error();
      }
      return;
    }

    // Broadcast (i.e. pub) message if destined for broadcast ID
    if (receiverApp == E2EConsts::kBroadcastSockMinionId) {
      if (broadcastPubSockBindUrl_.empty()) {
        // Drop message if no broadcast url is provided
        VLOG(5) << "Dropping broadcast message from " << senderApp;
        return;
      }
      const auto sendRet =
          broadcastPubSock_.sendMultiple(senderAppMsg, thriftMsg);
      if (sendRet.hasError() && recordZmqSendError(receiverApp)) {
        LOG(ERROR) << "Error routing msg from " << senderApp
                   << " to broadcast socket: " << sendRet.error();
      }
      return;
    }

    // Else route it the corresponding receiverApp in minion
    const auto sendRet =
        appsSock_.sendMultiple(receiverAppMsg, senderAppMsg, thriftMsg);
    if (sendRet.hasError() && recordZmqSendError(receiverApp)) {
      LOG(ERROR) << "Error routing msg from " << senderApp << " to "
                 << receiverApp << ": " << sendRet.error();
    }
  });
}

void
Broker::initControllerSock() {
  controllerSock_ = fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT>(
      zmqContext_,
      IdentityString{macAddr_},
      folly::none, // Crypto keypair
      NonblockingFlag{true});

  // Tag all management messages as DSCP to differenciate with data traffic
  if (controllerSock_.setSockOpt(ZMQ_TOS, &E2EConsts::kIpTos, sizeof(int))
          .hasError()) {
    LOG(FATAL) << "Could not set ZMQ_TOS.";
  }

  VLOG(3) << "Attaching dealer socket/event callback...";

  // Message on dealer socket
  addSocket(
      RawZmqSocketPtr{*controllerSock_}, ZMQ_POLLIN, [this](int) noexcept {
        VLOG(4) << "Received a message on controller dealer socket";

        Message receiverAppMsg, senderAppMsg, thriftMsg;

        const auto recvRet = controllerSock_.recvMultiple(
            receiverAppMsg, senderAppMsg, thriftMsg);
        if (recvRet.hasError()) {
          LOG(ERROR) << "Error reading message: " << recvRet.error();
          return;
        }

        // Reset timer since we received a message on this socket
        ctrlSockTimeoutTimer_->scheduleTimeout(ctrlSockTimeout_);

        const auto receiverApp = receiverAppMsg.read<std::string>().value();
        const auto senderApp = senderAppMsg.read<std::string>().value();

        VLOG(4) << "Processing a message from controller : " << senderApp
                  << " to " << receiverApp;

        // Message for broker
        if (receiverApp == E2EConsts::kBrokerMinionId) {
          auto maybeMsg = thriftMsg.readThriftObj<thrift::Message>(serializer_);
          if (maybeMsg.hasError()) {
            LOG(ERROR) << "Error deserializing thrift Message from "
                        << senderApp << ": " << maybeMsg.error();
            return;
          }
          // Decompress the message (if needed)
          std::string error;
          if (!CompressionUtil::decompress(maybeMsg.value(), error)) {
            LOG(ERROR) << error;
            return;
          }
          processMessage(senderApp, maybeMsg.value());
          return;
        }

        // Route it to the corresponding receiver app
        const auto sendRet =
            appsSock_.sendMultiple(receiverAppMsg, senderAppMsg, thriftMsg);
        if (sendRet.hasError() && recordZmqSendError(receiverApp)) {
          LOG(ERROR) << "Error routing msg from " << senderApp << " to "
                     << receiverApp << ": " << sendRet.error();
          return;
        }
      });
}

void
Broker::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::BSTAR_SWITCH_CONTROLLER:
      processBstarSwitchController(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << senderApp;
  }
}

void
Broker::processBstarSwitchController(
    const std::string& senderApp, const thrift::Message& /*message*/) {
  LOG(INFO) << "Received request to switch controllers from " << senderApp;

  // Switch controllers by triggering the socket timeout function
  ctrlSockTimeoutTimer_->scheduleTimeout(std::chrono::milliseconds::zero());
}

void
Broker::getCtrlUrl() {
  std::string contents;
  if (!folly::readFile(myNetworkInfoFile_.c_str(), contents)) {
    LOG(ERROR) << "Could not read NetworkInfo from " << myNetworkInfoFile_;
    return;
  }

  thrift::NetworkInfo myNetworkInfo;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.deserialize(contents, myNetworkInfo);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse NetworkInfo struct "
               << folly::exceptionStr(ex);
    return;
  }

  // Reconnect if necessary
  if (myNetworkInfo.e2eCtrlUrl != controllerPrimaryRouterUrl_) {
    controllerPrimaryRouterUrl_ = myNetworkInfo.e2eCtrlUrl;
    LOG(INFO) << folly::format(
        "Controller primary URL changed to `{}`",
        controllerPrimaryRouterUrl_);
    if (usingPrimaryCtrlUrl_) {
      LOG(INFO) << folly::format(
          "Connecting to new primary URL `{}`",
          controllerPrimaryRouterUrl_);
      connectToCtrl(controllerPrimaryRouterUrl_);
    }
  }
  if (myNetworkInfo.e2eCtrlUrlBackup != controllerBackupRouterUrl_) {
    LOG(INFO) << folly::format(
        "Controller backup URL changed to `{}`",
        controllerBackupRouterUrl_);
    controllerBackupRouterUrl_ = myNetworkInfo.e2eCtrlUrlBackup;
    if (!usingPrimaryCtrlUrl_) {
      LOG(INFO) << folly::format(
          "Connecting to new backup URL `{}`",
          controllerBackupRouterUrl_);
      connectToCtrl(controllerBackupRouterUrl_);
    }
  }
}

void
Broker::connectToCtrl(const std::string& ctrlUrl) {
  if (ctrlUrl.empty()) {
    return;
  }

  // (Re)start socket timeout timer since we're going to (re)connect
  ctrlSockTimeoutTimer_->scheduleTimeout(ctrlSockTimeout_);

  // Disconnect previous connection if necessary
  if (!currentCtrlUrl_.empty()) {
    LOG(INFO) << folly::format(
        "Disconnecting from controller on url `{}`",
        currentCtrlUrl_);

    // `disconnect()` causes runtime errors when controllerSock_ is connected to
    // an incompatible socket. To avoid this, use `close()` to destroy and
    // reinitialize the socket. The only drawbacks are that the callbacks are
    // invalidated and the socket has to be reinitialized.

    // Remove controller socket callback
    removeSocket(RawZmqSocketPtr{*controllerSock_});
    controllerSock_.close();  // Destroy the socket
    initControllerSock();  // Reinitialize a new controllerSock_
  }

  // Connect to given URL
  LOG(INFO) << folly::format("Connecting to controller on url `{}`", ctrlUrl);
  if (controllerSock_.connect(SocketUrl{ctrlUrl}).hasError()) {
    LOG(ERROR) << folly::format("Error connecting to `{}`", ctrlUrl);

    // Try to roll back to previous URL if connect fails
    if (!currentCtrlUrl_.empty()) {
      LOG(INFO) << folly::format(
          "Try rollback connecting to controller on url `{}`", currentCtrlUrl_);
      if (controllerSock_.connect(SocketUrl{currentCtrlUrl_}).hasError()) {
        LOG(ERROR) << folly::format(
            "Error connecting to `{}`", currentCtrlUrl_);
      }
    }
    return;
  }

  // Connection succeeded, update current controller URL
  currentCtrlUrl_ = ctrlUrl;
}

bool
Broker::recordZmqSendError(const std::string& dstZmqId) {
  int64_t now = TimeUtils::getSteadyTimestamp();
  auto& countAndTs = zmqSendErrors_[dstZmqId];
  bool throttle = (now - countAndTs.second < kZmqSendErrorLogThrottleSec);
  if (!throttle) {
    countAndTs.second = now;
  }
  countAndTs.first++;
  VLOG(5) << "Failed routing " << countAndTs.first << " msgs to " << dstZmqId;
  while (zmqSendErrors_.size() > kZmqSendErrorMaxSize) {
    VLOG(5) << "zmqSendErrors_ exceeded size limit, deleting first entry: "
            << zmqSendErrors_.begin()->first;
    zmqSendErrors_.erase(zmqSendErrors_.begin());
  }
  return !throttle;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
