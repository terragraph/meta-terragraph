/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <stddef.h>
#include <string>

namespace facebook {
namespace terragraph {

/**
 * Constants used within Terragraph's E2E software.
 */
class E2EConsts {
 public:
  // ---  Controller ---

  /** Main ZMQ identity of the E2E controller. */
  const static std::string kMainCtrlId;
  /** ZMQ identity of the broker on the E2E controller. */
  const static std::string kBrokerCtrlId;

  // --- Minion ---

  /** ZMQ identity of the broker on the E2E minion. */
  const static std::string kBrokerMinionId;
  /** ZMQ identity of the broadcast PUB socket on the E2E minion. */
  const static std::string kBroadcastSockMinionId;

  // --- Apps ---

  /** ZMQ identity of IgnitionApp on the E2E controller. */
  const static std::string kIgnitionAppCtrlId;
  /** ZMQ identity of IgnitionApp on the E2E minion. */
  const static std::string kIgnitionAppMinionId;

  /** ZMQ identity of StatusApp on the E2E controller. */
  const static std::string kStatusAppCtrlId;
  /** ZMQ identity of StatusApp on the E2E minion. */
  const static std::string kStatusAppMinionId;

  /** ZMQ identity of the TopologyApp. */
  const static std::string kTopologyAppCtrlId;

  /** ZMQ identity of DriverApp. */
  const static std::string kDriverAppMinionId;

  /** ZMQ identity of UpgradeApp on the E2E controller. */
  const static std::string kUpgradeAppCtrlId;
  /** ZMQ identity of UpgradeApp on the E2E minion. */
  const static std::string kUpgradeAppMinionId;

  /** ZMQ identity of ConfigApp on the E2E controller. */
  const static std::string kConfigAppCtrlId;
  /** ZMQ identity of ConfigApp on the E2E minion. */
  const static std::string kConfigAppMinionId;

  /** ZMQ identity of ScanApp. */
  const static std::string kScanAppCtrlId;

  /** ZMQ identity of SchedulerApp. */
  const static std::string kSchedulerAppCtrlId;

  /** ZMQ identity of OpenrClientApp. */
  const static std::string kOpenrClientAppMinionId;

  /** ZMQ identity of TrafficApp on the E2E controller. */
  const static std::string kTrafficAppCtrlId;
  /** ZMQ identity of TrafficApp on the E2E minion. */
  const static std::string kTrafficAppMinionId;

  /** ZMQ identity of BinaryStarApp. */
  const static std::string kBinaryStarAppCtrlId;

  /** ZMQ identity of TopologyBuilderApp. */
  const static std::string kTopologyBuilderAppCtrlId;

  /** ZMQ identity of the API Streamer. */
  const static std::string kApiEventSubId;

  // --- Provisioning ---

  /** KvStore key holding the E2E controller URL. */
  const static std::string kCtrlUrlKey;
  /** KvStore key holding the stats aggregator URL. */
  const static std::string kAggrUrlKey;

  // --- Timeouts ---

  /**
   * Poll timeout, used to periodically break from the poll waiting and perform
   * other functions.
   */
  const static std::chrono::milliseconds kPollTimeout;

  /**
   * Maximum time to wait for reading data on a socket.
   *
   * This is an important constant, as we do not implement any recovery from
   * read errors. We expect in our network reads to be "fast" since we talk to
   * directly-adjacent nodes.
   */
  const static std::chrono::milliseconds kReadTimeout;

  // --- Socket Option ---

  /**
   * ToS (type of service) socket options.
   *
   * DSCP = 34 [AF41] (first 6 bits), ECN = 0 (last 2 bits) => total 136
   */
  const static int kIpTos;

  /** ZMQ ZAP domain (must be non-empty for NULL security). */
  const static std::string kZmqZapDomain;
  /**
   * ZMQ ZAP metadata property name for IP address field (must be < 256 chars).
   */
  const static std::string kZmqIpAddressMetaProperty;
  /** ZMQ ZAP metadata property name for Identity field (defined by ZMQ). */
  const static std::string kZmqIdentityMetaProperty;

  // --- Inproc Socket Url ---

  /** Driver PAIR socket URL (bound in DriverIf, connected from DriverApp). */
  const static std::string kMinionDriverPairUrl;

  /** ZMQ ZAP inproc socket endpoint. */
  const static std::string kZmqZapEndpoint;

  // --- Upgrades ---

  /** Default BitTorrent tracker port. */
  const static unsigned short int kTorrentTrackerPort;

  /** "Creator" field in torrents. */
  const static std::string kTorrentCreator;

  /** Upgrade image header size. */
  const static size_t kImageHeaderSize;

  /** File extension for upgrade images. */
  const static std::string kImageFileExtension;

  /**
   * ToS (type of service) for BitTorrent packets.
   *
   * DSCP = 18 [AF21] (first 6 bits), ECN = 0 (last 2 bits) => total 72
   */
  const static int kTorrentTos;

  // --- Config ---

  /** File extension for JSON files. */
  const static std::string kJsonFileExtension;

  /** Delimiter used in software versions. */
  const static std::string kSwVersionDelimiter;

  /** Delimiter used in config path. */
  const static std::string kConfigPathDelimiter;

  /** Wildcard used in config path. */
  const static std::string kConfigPathWildcard;

  // --- Events ---

  /**
   * Category name (in fbzmq::thrift::EventLog) for events generated by the
   * controller and/or minion.
   */
  const static std::string kEventCategory;
  /**
   * Category name (in fbzmq::thrift::EventLog) for scan results generated by
   * the controller and/or minion.
   */
  const static std::string kEventScanResultCategory;
  /**
   * Category name (in fbzmq::thrift::EventLog) for iPerf results generated by
   * the controller and/or minion.
   */
  const static std::string kEventIperfResultCategory;
  /**
   * Category name (in fbzmq::thrift::EventLog) for ping results generated by
   * the controller and/or minion.
   */
  const static std::string kEventPingResultCategory;

  // --- Controller/Minion Shared ---

  /**
   * Accuracy threshhold in order for GPS to be enabled on a node.
   *
   * This represents the location accuracy needed for GPS time estimation within
   * 500ns. Light travels 50m in 170ns, leaving 330ns margin for other
   * uncertainties.
   */
  const static double kGpsAccuracyThresh;

  /**
   * The number of seconds between the Unix epoch (midnight on Jan 1, 1970) and
   * the GPS epoch (midnight on Jan 6, 1980).
   */
  const static std::chrono::seconds kUnixGpsEpochDifference;
};

} // namespace terragraph
} // namespace facebook
