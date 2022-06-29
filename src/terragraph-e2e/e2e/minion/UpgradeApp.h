/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>
#include <folly/Uri.h>
#include <libtorrent/session.hpp>
#include <libtorrent/torrent_handle.hpp>

#include "MinionApp.h"
#include "UpgradeStateCache.h"
#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/Event_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that handles software upgrades.
 *
 * This app primarily communicates with a separate UpgradeApp on the E2E
 * controller.
 */
class UpgradeApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   * @param useHttps whether to use (and only allow) HTTPS sessions to download
   *                 new software images
   * @param ipv6GlobalAddressableIfname the network interface name holding this
   *                                    node's globally-reachable IPv6 address
   * @param upgradeStateCache the upgrade state cache
   */
  UpgradeApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr,
      bool useHttps,
      const std::string& ipv6GlobalAddressableIfname,
      UpgradeStateCache& upgradeStateCache);

  /** Destructor. */
  ~UpgradeApp();

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** State during torrent-based "prepare" stage. */
  struct PrepareTorrentState {
    /** The libtorrent session. */
    std::shared_ptr<libtorrent::session> ltSession;

    /** The libtorrent handle for the downloading image. */
    libtorrent::torrent_handle ltHandle;

    /** The image download directory. */
    std::string localDir;

    /** The steady_clock time point representing a download timeout. */
    std::chrono::steady_clock::time_point endTs;
  };

  /** Handle a generic upgrade request. */
  void processUpgradeReq(
      const std::string& senderApp, const thrift::Message& message);

  /** Handle a PREPARE request. */
  void startPrepareUpgrade(const thrift::UpgradeReq& upgradeReq);

  /** Handle a RESET_STATUS request. */
  void resetStatus(const thrift::UpgradeReq& upgradeReq);

  /** Handle a COMMIT_UPGRADE request. */
  void scheduleCommitUpgrade(const thrift::UpgradeReq& upgradeReq);

  /**
   * Commit the upgrade (unless prerequisites are not met).
   *
   * NOTE: This will reboot the node!
   */
  void commitUpgrade();

  /** Send the current upgrade state to our StatusApp. */
  void sendUpgradeStatus();
  /** Send the current upgrade state to our StatusApp and log an event. */
  void sendUpgradeStatusAndLogEvent(
      const thrift::EventId eventId, const thrift::EventLevel level);
  /** Send a `DOWNLOAD_FAILED` state with the given reason to our StatusApp. */
  void sendPrepareDownloadFailure(const std::string& reason);

  /**
   * Download a file from an HTTP server, and set minionImageLocalPath_.
   *
   * Returns true upon successful completion, or false otherwise.
   */
  bool downloadHttp(
      const folly::Uri& imageUrl,
      const std::string& localDir,
      const thrift::UpgradeReq& upgradeReq);

  /**
   * Download a torrent using a magnet URI.
   *
   * Returns false if an early failure occurred. Otherwise, return true and
   * start tracking torrent status via downloadTorrentUpdate().
   */
  bool downloadTorrent(
      const folly::Uri& imageUrl,
      const std::string& localDir,
      const thrift::UpgradeReq& upgradeReq);

  /**
   * Check for completion of the torrent-based download.
   *
   * Clear prepareTorrentState_ when finished. If the download was successful,
   * set minionImageLocalPath_ and call prepareProcessImage().
   */
  bool downloadTorrentUpdate();

  /** "Prepare" phase: verify the downloaded image and flash it. */
  void prepareProcessImage();

  /**
   * Read and parse meta information from the downloaded image.
   *
   * This also performs basic validity checks by locally computing the MD5 hash.
   *
   * Returns std::nullopt upon any failures.
   */
  std::optional<thrift::ImageMeta> getMetaInfo();

  /** Flash the node with the newly-downloaded image. */
  bool flashMinionImage();

  /**
   * Save a preloaded node config blob to where it can be picked up by watchdog
   * scripts.
   */
  bool savePreloadedConfig(
      const std::string& version, const std::string& preloadedJson);

  /** Reset state related to prepareTorrentState_. */
  void resetPrepareTorrentState();

  /** Full path of the downloaded software image (during the PREPARE phase). */
  std::string minionImageLocalPath_{};

  /**
   * Whether to use (and only allow) HTTPS sessions to download new software
   * images.
   */
  bool useHttps_{false};

  /** Interface name holding a globally-reachable IPv6 address. */
  std::string ipv6GlobalAddressableIfname_{""};

  /** The node's current upgrade state. */
  thrift::UpgradeStatus upgradeStatus_;

  /** The upgrade state cache. */
  UpgradeStateCache& upgradeStateCache_;

  /** Timer to check the status of the torrent-based "prepare" stage. */
  std::unique_ptr<fbzmq::ZmqTimeout> prepareTorrentTimer_{nullptr};

  /** Timer to keep track of the scheduled "commit" stage. */
  std::unique_ptr<fbzmq::ZmqTimeout> scheduleCommitTimeout_{nullptr};

  /** State during torrent-based "prepare" stage (or NULL). */
  std::optional<PrepareTorrentState> prepareTorrentState_{};
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
