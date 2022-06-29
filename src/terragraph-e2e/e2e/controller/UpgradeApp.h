/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/async/ZmqTimeout.h>
#include <libtorrent/session.hpp>

#include "CtrlApp.h"
#include "StatusApp.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * App that handles software upgrades.
 *
 * This app primarily communicates with a separate UpgradeApp on the E2E
 * minion.
 *
 * Main concepts:
 * - Request: Upgrade all or selected nodes to a new software image. There are
 *   two upgrade phases: "prepare" (image download) and "commit" (image switch).
 * - Batch: A set of nodes that can be upgraded in parallel.
 *
 * Work flow:
 * - A request is popped from pendingReqs_ and converted into batches
 *   of nodes that can be processed in parallel.
 * - When a batch is initiated, requests are dispatched to the nodes at once,
 *   and a common timeout is maintained. The batch is stored as curBatch_.
 * - When all the nodes in a batch complete (or abort with 'skipFailure' set),
 *   the next batch from the same request is processed.
 * - If a node in a batch fails and 'skipFailure' is not set, the entire batch
 *   and all other batches in the pendingBatches_ are discarded.
 * - If a batch times out, and 'skipFailure' is set, the current batch is
 *   discarded, and the next batch from the same request is initiated. If
 *   'skipFailure' is not set, the current request is aborted.
 */
class UpgradeApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param statusReportsInterval the interval at which to run the main software
   *                              upgrades logic and synchronize with minion
   *                              status reports
   */
  UpgradeApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      std::chrono::seconds statusReportsInterval);

 private:
  /** A set of nodes to be upgraded in parallel. */
  class Batch {
   public:
    /** Empty constructor. */
    explicit Batch() : nodeNames_(), ugReq_(), initTime_(), skipLinks_() {}

    /**
     * Constructor.
     * @param nodeNames the set of nodes in the batch
     * @param ugReq the upgrade request
     */
    explicit Batch(
      std::unordered_set<std::string> nodeNames,
      std::shared_ptr<thrift::UpgradeGroupReq> ugReq)
      : nodeNames_(nodeNames), ugReq_(ugReq), initTime_(), skipLinks_() {};

    // ---- Accessors ----

    /** Returns the set of nodes in the batch. */
    std::unordered_set<std::string> getNodeNames() const {
      return nodeNames_;
    }

    /** Returns whether there any nodes pending in the current batch. */
    bool pending() const {
      return !nodeNames_.empty() && ugReq_;
    }

    /** Returns the number of pending nodes in the current batch. */
    int size() const {
      return nodeNames_.size();
    }

    /** Returns the upgrade request. */
    std::shared_ptr<thrift::UpgradeGroupReq> getReq() const {
      CHECK(ugReq_) << "UpgradeRequest pointer is not set.";
      return ugReq_;
    }

    /**
     * Returns whether this upgrade batch has timed out (relative to the current
     * system time).
     */
    bool timedOut() const {
      CHECK(initTime_) << "Init Time is not set.";
      auto timeElapsed = std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::steady_clock::now() - *initTime_)
          .count();
      return timeElapsed >= ugReq_->timeout;
    }

    /** Returns the wireless links to skip in the wirelessLinkAlive() check. */
    std::unordered_set<std::string>& getSkipLinks() {
      return skipLinks_;
    }

    /** Returns the number of BGP peers per POP node. */
    std::unordered_map<std::string, size_t>& getBgpPeers() {
      return bgpPeers_;
    }

    // ---- Mutators ----

    /** Remove a node from this batch. */
    void removeNode(const std::string& nodeName) {
      nodeNames_.erase(nodeName);
    }

    /** Add a node to this batch. */
    void addNode(const std::string& nodeName) {
      nodeNames_.insert(nodeName);
    }

    /** Add a list of nodes to this batch. */
    void addNodes(const std::unordered_set<std::string>& nodeNames) {
      nodeNames_.insert(nodeNames.begin(), nodeNames.end());
    }

    /** Clear out the entire batch, and delete the reference to the request. */
    void clear() {
      nodeNames_.clear();
      ugReq_.reset();
    }

    /** Initiate the batch at the current system time. */
    void
    initiate(
        const std::unordered_set<std::string> skipLinks = {},
        const std::unordered_map<std::string, size_t> bgpPeers = {}) {
      initTime_ = std::make_optional<std::chrono::steady_clock::time_point>(
          std::chrono::steady_clock::now());
      skipLinks_ = skipLinks;
      bgpPeers_ = bgpPeers;
    }

   private:
    /** Set of nodes in the batch. */
    std::unordered_set<std::string> nodeNames_;

    /** Pointer to the upgrade request. */
    std::shared_ptr<thrift::UpgradeGroupReq> ugReq_;

    /** Time at which the batch was initiated. */
    std::optional<std::chrono::steady_clock::time_point> initTime_;

    /** Wireless links to skip during wirelessLinkAlive() check. */
    std::unordered_set<std::string> skipLinks_;

    /** Number of BGP peers per POP node before the batch was initiated. */
    std::unordered_map<std::string /*popNodeName*/, size_t /*peers*/> bgpPeers_;
  };

  /** Upgrade image metadata. */
  struct ImageInfo {
    /** The upgrade image details. */
    thrift::UpgradeImage upgradeImage;
    /** The filename on disk. */
    std::string filename;
    /** The libtorrent handle for the seeding image. */
    std::optional<libtorrent::torrent_handle> ltHandle;
  };

  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process an upgrade state request. */
  void processUpgradeStateReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to abort an upgrade. */
  void processUpgradeAbortReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request for a commit plan. */
  void processUpgradeCommitPlanReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message);

  /** Process an upgrade request. */
  void processUpgradeGroupReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to add an upgrade image. */
  void processUpgradeAddImageReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to delete an upgrade image. */
  void processUpgradeDelImageReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to list upgrade images. */
  void processUpgradeListImagesReq(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request to reset the upgrade status on nodes. */
  void processResetReq(const thrift::UpgradeGroupReq& message);

  /**
   * Perform the main software upgrades logic.
   *
   * This function is invoked periodically, and will sync with minion status
   * reports and process tasks accordingly.
   */
  void syncWithStatusReports();

  /**
   * Initiate the next batch of nodes.
   * 1. If an upgrade is not required, fetch the next batch.
   * 2. If an upgrade is required, dispatch requests to the minion, and
   *    set curBatch_.
   * 3. Mark the time at which the batch was initiated.
   */
  void populateNextBatch();

  /** Convert the next request into batches of nodes to be processed. */
  void populateNextReq();

  /**
   * Abort the current request.
   * 1. Clear the current batch.
   * 2. Clear pending batches for the current request.
   * 3. Clear pending requests with the same request ID.
   *
   * @param resetStatus if true, send RESET_STATUS to all nodes in the request
   * @param reason optional reason string (for logging purposes)
   */
  void clearCurReq(bool resetStatus, const std::string& reason = "");

  /** Abort a pending upgrade request by ID. */
  void clearPendingReqById(
      const std::string& reqId, const std::string& reason = "");

  /**
   * Check if a "prepare" is needed, and dispatch requests to E2E minion
   * instances.
   *
   * Sets curBatch_ to the new upgrade batch.
   */
  void initiatePrepare(Batch& batch);

  /**
   * Check if a "commit" is needed, and dispatch requests to E2E minion
   * instances.
   *
   * Sets curBatch_ to the new upgrade batch.
   */
  void initiateCommit(Batch& batch);

  /** Filter out nodes not ready to "prepare". */
  void filterPrepareBatch(Batch& batch);

  /** Filter out nodes not ready to "commit". */
  void filterCommitBatch(Batch& batch);

  /** Follow up on "prepare" requests sent to the nodes in curBatch_. */
  void checkPrepareBatch();

  /** Follow up on "commit" requests sent to the nodes in curBatch_. */
  void checkCommitBatch();

  /** Initialize torrent-related objects. */
  void initTorrentUpgrades();

  /**
   * Process a new upgrade image located in (dir + filename).
   *
   * Returns the new image name if successful, or std::nullopt otherwise.
   */
  std::optional<std::string> processImageFile(
      const std::string& dir, const std::string& filename);

  /**
   * Add a set of nodes to the first element in pendingBatches_.
   *
   * If no pending batch exists, create a new pending batch.
   */
  void addToPendingBatches(
      std::unordered_set<std::string>& nodeNames,
      std::shared_ptr<thrift::UpgradeGroupReq> ugReq);

  /**
   * Schedule a node for a prepare/commit retry if node did not exceed retry
   * limit.
   */
  void scheduleRetry(
      const std::string& nodeName,
      std::shared_ptr<thrift::UpgradeGroupReq> ugReq);

  /**
   * Schedule a full upgrade by spawning a PREPARE_UPGRADE request and
   * a COMMIT_UPGRADE request.
   */
  void scheduleFullUpgrade(thrift::UpgradeGroupReq req);

  /** After an upgrade request finishes, update the golden image config. */
  void updateGoldenImageAfterUpgrade(const thrift::UpgradeGroupReq& req);

  /** Set the golden image in the controller config. */
  bool setGoldenImage(const thrift::GoldenImage& goldenImage);

  /** If applicable, find nodes to upgrade to the golden image. */
  void goldenImageUpgrade();

  /** Cached map of node IDs to their latest status reports. */
  std::unordered_map<std::string, thrift::StatusReport> minionToStatus_;

  /** Timer to periodically run the main software upgrades logic. */
  std::unique_ptr<fbzmq::ZmqTimeout> statusReportsSyncTimeout_{nullptr};

  /** Timer to periodically apply the golden image to nodes. */
  std::unique_ptr<fbzmq::ZmqTimeout> goldenImageCheckTimeout_{nullptr};

  /** Set of pending nodes. */
  Batch curBatch_;

  /** Current request being processed. */
  std::shared_ptr<thrift::UpgradeGroupReq> curReq_{nullptr};

  /** All nodes to upgrade as part of curReq_, mapping from node name to MAC. */
  std::unordered_map<std::string, std::string> curReqNodes_;

  /** Upgrade status for nodes in the current request. */
  std::unordered_map<std::string /* node name */, bool> upgradeStatus_;

  /** For commits, store the next image data before rebooting. */
  std::unordered_map<std::string /* node name */, thrift::ImageMeta>
      nodeToNextImage_;

  /** Queue for batches belonging to the same request. */
  std::deque<Batch> pendingBatches_;

  /** Queue for incoming requests to the controller. */
  std::deque<thrift::UpgradeGroupReq> pendingReqs_;

  /** libtorrent session (holds all active torrent state). */
  libtorrent::session ltSession_;

  /** BitTorrent tracker URL. */
  std::string btTrackerUrl_;

  /** List of upgrade images (keyed by image name). */
  std::unordered_map<std::string /* name */, ImageInfo> imageList_;

  /** Number of commit/prepare retries so far per node. */
  std::unordered_map<std::string, size_t> nodeName2UpgradeRetries_{};

  /**
   * The upgrade request ID of the last/current golden image upgrade procedure.
   */
  std::string goldenImageUpgradeId_;

  /**
   * Public IP address used by the nodes to reach the tracker + http endpoints.
   */
  std::string publicIpv6Address_;
};

} // namespace terragraph
} // namespace facebook
