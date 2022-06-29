/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UpgradeApp.h"

#include <cstdio>
#include <sys/statfs.h>
#include <unistd.h>

#include <boost/filesystem.hpp>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/gen/Base.h>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include "SharedObjects.h"
#include "UpgradeAppUtil.h"
#include "e2e/common/Consts.h"
#include "e2e/common/CurlUtil.h"
#include "e2e/common/IpUtil.h"
#include "e2e/common/SysUtils.h"
#include "e2e/common/UpgradeUtils.h"
#include "e2e/common/UuidUtils.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

DEFINE_bool(
    enable_bt_tracker_upgrades,
    true,
    "Whether to enable seeding of upgrade images using BitTorrent");
DEFINE_string(
    bt_tracker_ipv6_global_addressable_ifname,
    "lo",
    "The globally addressable ipv6 interface on the controller "
    "(if running the BitTorrent tracker)");
DEFINE_string(
    bt_tracker_override,
    "",
    "The external address of the BitTorrent tracker used by nodes to retrieve "
    "peer info (e.g. http://[::1]:6969/announce)");
DEFINE_string(
    local_bt_tracker_override,
    "",
    "The local address of the BitTorrent tracker used by the controller to "
    "seed image upgrades (e.g. http://chihaya:6969/announce)");
DEFINE_int32(
    bt_seeder_port,
    6881,
    "The port of the BitTorrent seeder used for image upgrades");
DEFINE_string(
    bt_announce_ip,
    "",
    "The external IP of the controller that is announced to other BitTorrent "
    "clients for image upgrades");
DEFINE_bool(
    bt_high_performance_seed,
    true,
    "Enable 'high performance seed' mode in libtorrent");
DEFINE_int32(
    bt_num_active_seeds,
    -1,
    "The number of active seeding and downloading torrents the queueing "
    "mechanism allows (-1 = unlimited)");
DEFINE_int32(
    bt_peer_tos,
    facebook::terragraph::E2EConsts::kTorrentTos,
    "The ToS (type of service) byte set in the IP header of every packet sent "
    "to peers (-1 = libtorrent default)");
DEFINE_string(
    upgrade_image_local_dir,
    "/data/images/",
    "The directory to store upgrade images");
DEFINE_uint64(
    upgrade_image_min_free_space,
    1073741824,  // 1GB
    "The minimum free disk space required for downloading upgrade images");
DEFINE_string(
    upgrade_image_http_path, "", "The HTTP path for node upgrade images");
DEFINE_bool(
    enable_simultaneous_image_and_config_upgrade,
    true,
    "Whether to enable sending a new config with an image upgrade");
DEFINE_int32(
    golden_image_check_interval_s,
    60,
    "The interval (in seconds) at which nodes may be automatically upgraded to "
    "the 'golden image' (if configured), or 0 to disable");
DEFINE_bool(
    set_golden_image_after_commits,
    true,
    "Whether to automatically update the golden image version (in controller "
    "config) after successful commits");

namespace facebook {
namespace terragraph {

UpgradeApp::UpgradeApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    std::chrono::seconds statusReportsInterval)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kUpgradeAppCtrlId) {
  // Periodic status sync
  statusReportsSyncTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { syncWithStatusReports(); });
  statusReportsSyncTimeout_->scheduleTimeout(statusReportsInterval, true);

  // Periodic golden image check
  if (FLAGS_golden_image_check_interval_s > 0) {
    goldenImageCheckTimeout_ =
        ZmqTimeout::make(this, [this]() noexcept { goldenImageUpgrade(); });
    goldenImageCheckTimeout_->scheduleTimeout(
        std::chrono::seconds(FLAGS_golden_image_check_interval_s), true);
  }

  // BitTorrent initialization
  if (FLAGS_enable_bt_tracker_upgrades) {
    initTorrentUpgrades();
  }

  // Strip trailing slash from node image HTTP path
  if (!FLAGS_upgrade_image_http_path.empty() &&
      FLAGS_upgrade_image_http_path.rfind('/') + 1 ==
          FLAGS_upgrade_image_http_path.length()) {
    FLAGS_upgrade_image_http_path = FLAGS_upgrade_image_http_path.substr(
        0, FLAGS_upgrade_image_http_path.length() - 1);
    VLOG(2) << "Trimmed trailing slash from upgrade image http path to \""
            << FLAGS_upgrade_image_http_path << "\"";
  }

  // Process/seed images after constructor
  scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
    for (const auto& pair : SysUtils::findFilesInDirectory(
             FLAGS_upgrade_image_local_dir, E2EConsts::kImageFileExtension)) {
      processImageFile(FLAGS_upgrade_image_local_dir, pair.second);
    }
  });
}

void
UpgradeApp::initTorrentUpgrades() {
  if (!FLAGS_bt_tracker_ipv6_global_addressable_ifname.empty()) {
    publicIpv6Address_ = IpUtil::getIpv6GlobalAddress(
        FLAGS_bt_tracker_ipv6_global_addressable_ifname);
  }

  // fall-back: use BT announce IP
  if (publicIpv6Address_.empty() && !FLAGS_bt_announce_ip.empty()) {
    publicIpv6Address_ = FLAGS_bt_announce_ip;
  }

  // Configure session params
  libtorrent::settings_pack ltSettings;
  // Switch default settings to "high performance seed" mode
  if (FLAGS_bt_high_performance_seed) {
    ltSettings = libtorrent::high_performance_seed();
  }
  // Override default max active seeds
  ltSettings.set_int(
      libtorrent::settings_pack::active_seeds, FLAGS_bt_num_active_seeds);
  // Remove user-agent from lt session
  ltSettings.set_str(libtorrent::settings_pack::user_agent, "");
  // Set ToS (type of service)
  if (FLAGS_bt_peer_tos >= 0 && FLAGS_bt_peer_tos <= 255) {
    ltSettings.set_int(libtorrent::settings_pack::peer_tos, FLAGS_bt_peer_tos);
  }
  // Set listen interfaces
  std::string listenInterfaces = folly::sformat(
      "0.0.0.0:{},[::]:{}", FLAGS_bt_seeder_port, FLAGS_bt_seeder_port);
  if (!publicIpv6Address_.empty()) {
    // Make sure a global IP is being used (??)
    // In libtorrent >= v1.2.4, the defaults sometimes only pick local IP addrs
    listenInterfaces +=
        folly::sformat(",[{}]:{}", publicIpv6Address_, FLAGS_bt_seeder_port);
  }
  ltSettings.set_str(
      libtorrent::settings_pack::listen_interfaces, listenInterfaces);
  if (!FLAGS_bt_announce_ip.empty()) {
    ltSettings.set_str(
        libtorrent::settings_pack::announce_ip,
        folly::sformat("{}", FLAGS_bt_announce_ip));
  }
  // Connect to all trackers specified
  ltSettings.set_bool(
      libtorrent::settings_pack::announce_to_all_trackers, true);
  // Don't use public DHT (Distributed Hash Table) - limit to trackers specified
  ltSettings.set_bool(libtorrent::settings_pack::enable_dht, false);
  // Don't use UPnP/NAT-PMP (used for NAT port forwarding on consumer routers)
  ltSettings.set_bool(libtorrent::settings_pack::enable_upnp, false);
  ltSettings.set_bool(libtorrent::settings_pack::enable_natpmp, false);
  ltSession_.apply_settings(ltSettings);

  // Get URL of BitTorrent tracker
  if (!FLAGS_bt_tracker_override.empty()) {
    btTrackerUrl_ = FLAGS_bt_tracker_override;
  } else {
    std::string trackerIpv6Address = publicIpv6Address_;
    if (trackerIpv6Address.empty()) {
      LOG(ERROR) << "Failed to get IPv6 global address! Please set the full "
                 << "BitTorrent tracker URL manually using the gflag "
                 << "'bt_tracker_override'.";
      trackerIpv6Address = "localhost";  // use a dummy address
    } else {
      trackerIpv6Address = folly::sformat("[{}]", trackerIpv6Address);
    }
    btTrackerUrl_ = folly::sformat(
        "http://{}:{}/announce",
        trackerIpv6Address,
        E2EConsts::kTorrentTrackerPort);
  }
  LOG(INFO) << "Using BitTorrent tracker URL: " << btTrackerUrl_;

  // Validate image directory path (expect trailing '/')
  if (FLAGS_upgrade_image_local_dir.empty() ||
      FLAGS_upgrade_image_local_dir.back() != '/') {
    FLAGS_upgrade_image_local_dir += '/';
  }

  // Create image directory (if necessary)
  boost::system::error_code errorCode;
  if (boost::filesystem::create_directories(
      FLAGS_upgrade_image_local_dir.c_str(), errorCode)) {
    LOG(INFO) << "Created image directory " << FLAGS_upgrade_image_local_dir;
  }
}

std::optional<std::string>
UpgradeApp::processImageFile(
    const std::string& dir, const std::string& filename) {
  std::string path = dir + filename;

  // Extract file info
  std::string cmd = path + " -m";
  LOG(INFO) << "Executing command: " << cmd;
  std::shared_ptr<FILE> fd(popen(cmd.c_str(), "r"), pclose);
  if (!fd) {
    LOG(ERROR) << "Failed to execute command: " << cmd;
    return std::nullopt;
  }
  std::string output;
  if (!folly::readFile(fileno(fd.get()), output)) {
    LOG(ERROR) << "Failed to read command output";
    return std::nullopt;
  }
  thrift::ImageMeta imageMeta;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.deserialize(output, imageMeta);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Failed to parse image metadata";
    return std::nullopt;
  }

  // Check for duplicate names
  std::string imageName = imageMeta.version;  // let "name" = version string
  if (imageList_.count(imageName)) {
    LOG(ERROR) << "Trying to add image with duplicate name: " << imageName;
    return std::nullopt;
  }

  // Validate the image
  try {
    UpgradeUtils::verifyImage(path, imageMeta.md5);
  } catch (std::exception& e) {
    LOG(ERROR) << folly::exceptionStr(e);
    return std::nullopt;
  }

  // Images are distributed over BitTorrent
  std::string magnet;
  std::optional<libtorrent::torrent_handle> handle = std::nullopt;
  if (FLAGS_enable_bt_tracker_upgrades) {
    // Create torrent
    libtorrent::error_code errorCode;
    libtorrent::file_storage fileStorage;
    libtorrent::add_files(fileStorage, path);
    // TODO support BitTorrent v2
    libtorrent::create_flags_t flags = {};
    flags |= lt::create_torrent::v1_only;
    libtorrent::create_torrent torrent(fileStorage, 0 /* piece_size */, flags);
    torrent.add_tracker(btTrackerUrl_);
    if (!FLAGS_local_bt_tracker_override.empty()) {
      torrent.add_tracker(FLAGS_local_bt_tracker_override);
    }
    torrent.set_creator(E2EConsts::kTorrentCreator.c_str());
    libtorrent::set_piece_hashes(torrent, dir, errorCode);
    if (errorCode) {
      LOG(ERROR) << "Failed to hash torrent content: " << errorCode.message();
      return std::nullopt;
    }
    libtorrent::entry entry = torrent.generate();
    if (entry.type() == libtorrent::entry::undefined_t) {
      LOG(ERROR) << "Failed to generate torrent for file: " << filename;
      return std::nullopt;
    }
    std::vector<char> buf;
    bencode(std::back_inserter(buf), entry);
    libtorrent::torrent_info torrentInfo(&buf[0], int(buf.size()), errorCode);
    if (errorCode) {
      LOG(ERROR) << "Failed to create torrent: " << errorCode.message();
      return std::nullopt;
    }

    // Make magnet URI
    magnet = libtorrent::make_magnet_uri(torrentInfo);
    if (magnet.empty()) {
      LOG(ERROR) << "Failed to make magnet URI";
      return std::nullopt;
    }

    // Start seeding torrent
    libtorrent::add_torrent_params params;
    params.ti = std::make_shared<libtorrent::torrent_info>(torrentInfo);
    params.save_path = dir;
    params.flags |= libtorrent::torrent_flags::seed_mode;
    handle = ltSession_.add_torrent(params, errorCode);
    if (errorCode) {
      LOG(ERROR) << "Failed to seed torrent: " << errorCode.message();
      return std::nullopt;
    }
    LOG(INFO) << "Seeding image [" << imageName << "] at " << path
              << " with magnet URI: " << magnet;
  }

  // Images are distributed over HTTP or HTTPS
  std::string httpUri;
  if (!FLAGS_upgrade_image_http_path.empty() && !publicIpv6Address_.empty()) {
    httpUri = folly::sformat(
        "http://[{}]{}/{}",
        publicIpv6Address_,
        FLAGS_upgrade_image_http_path,
        filename);
    LOG(INFO) << "Hosting image [" << imageName << "] with HTTP/HTTPS URI: " << httpUri;
  }

  // Add to list
  thrift::UpgradeImage upgradeImage;
  upgradeImage.name = imageName;
  upgradeImage.magnetUri = magnet;
  upgradeImage.httpUri_ref() = httpUri;
  upgradeImage.md5 = imageMeta.md5;
  upgradeImage.hardwareBoardIds = imageMeta.hardwareBoardIds;
  ImageInfo imageInfo = {upgradeImage, path, handle};
  imageList_.emplace(imageName, std::move(imageInfo));

  return std::make_optional(imageName);
}

void
UpgradeApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {

  switch (message.mType) {
    case thrift::MessageType::UPGRADE_STATE_REQ:
      processUpgradeStateReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_ABORT_REQ:
      processUpgradeAbortReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_COMMIT_PLAN_REQ:
      processUpgradeCommitPlanReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_GROUP_REQ:
      processUpgradeGroupReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_ADD_IMAGE_REQ:
      processUpgradeAddImageReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_DEL_IMAGE_REQ:
      processUpgradeDelImageReq(minion, senderApp, message);
      break;
    case thrift::MessageType::UPGRADE_LIST_IMAGES_REQ:
      processUpgradeListImagesReq(minion, senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
      break;
  }
}

void
UpgradeApp::processUpgradeStateReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  if (!maybeReadThrift<thrift::UpgradeStateReq>(message)) {
    handleInvalidMessage("UpgradeStateReq", senderApp, minion);
    return;
  }

  thrift::UpgradeStateDump stateDump;
  if (curBatch_.pending()) {
    auto nodeNames = curBatch_.getNodeNames();
    stateDump.curBatch = std::vector<std::string>(
        nodeNames.begin(), nodeNames.end());
    stateDump.curReq = *(curBatch_.getReq());
  } else if (!pendingBatches_.empty()) {
    stateDump.curReq = *(pendingBatches_.front().getReq());
  }
  for (const auto& batch : pendingBatches_) {
    auto nodeNames = batch.getNodeNames();
    stateDump.pendingBatches.push_back(
        std::vector<std::string>(nodeNames.begin(), nodeNames.end()));
  }
  stateDump.pendingReqs = std::vector<thrift::UpgradeGroupReq>(
      pendingReqs_.begin(), pendingReqs_.end());

  sendToCtrlApp(
      senderApp,
      thrift::MessageType::UPGRADE_STATE_DUMP,
      stateDump);
}

void
UpgradeApp::processUpgradeAbortReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  LOG(INFO) << "Received UpgradeAbortReq message from" << minion << ":"
            << senderApp;
  auto abortReq = maybeReadThrift<thrift::UpgradeAbortReq>(message);
  if (!abortReq) {
    handleInvalidMessage("UpgradeAbortReq", senderApp, minion);
    return;
  }

  const std::string kClearReqReason = "User request";

  // Clear all requests?
  if (abortReq->abortAll) {
    LOG(INFO) << "Aborting all upgrade requests...";
    clearCurReq(abortReq->resetStatus, kClearReqReason);
    pendingReqs_.clear();
    sendE2EAck(senderApp, true, "Aborted all requests");
    return;
  }

  // Clear specific requests?
  LOG(INFO) << "Aborting upgrade requests: "
            << folly::join(",", abortReq->reqIds);
  for (const auto& reqId : abortReq->reqIds) {
    // Check if current request should be aborted
    // (clearCurReq will also remove pending requests with same ID)
    if (curBatch_.pending()) {
      if (curBatch_.getReq()->urReq.upgradeReqId == reqId) {
        clearCurReq(abortReq->resetStatus, kClearReqReason);
        continue;
      }
    } else if (!pendingBatches_.empty()) {
      // In case the current batch is empty, but there are pending batches
      if (pendingBatches_.front().getReq()->urReq.upgradeReqId == reqId) {
        clearCurReq(abortReq->resetStatus, kClearReqReason);
        continue;
      }
    }

    // Otherwise, clear any pending requests with this ID
    clearPendingReqById(reqId, kClearReqReason);
  }
  sendE2EAck(senderApp, true, "Aborted all valid requests");
}

void
UpgradeApp::processUpgradeCommitPlanReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  VLOG(3) << "Received UpgradeCommitPlanReq message from " << minion << ":"
          << senderApp;

  auto commitPlanReq = maybeReadThrift<thrift::UpgradeCommitPlanReq>(message);
  if (!commitPlanReq) {
    handleInvalidMessage("UpgradeCommitPlanReq", senderApp, minion);
    return;
  }

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto commitPlan = UpgradeAppUtil::getCommitPlan(
      *lockedTopologyW, commitPlanReq->limit, commitPlanReq->excludeNodes);
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  sendToCtrlApp(
      senderApp,
      thrift::MessageType::UPGRADE_COMMIT_PLAN,
      commitPlan);
}

void
UpgradeApp::processUpgradeGroupReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  auto ugReq = maybeReadThrift<thrift::UpgradeGroupReq>(message);
  if (!ugReq) {
    handleInvalidMessage("UpgradeGroupReq", senderApp, minion);
    return;
  }

  LOG(INFO) << folly::format(
      "Received upgrade from {}:{}. {}",
      minion,
      senderApp,
      UpgradeAppUtil::getReqDesc(*ugReq));

  // General validity checks
  if (ugReq->ugType == thrift::UpgradeGroupType::NODES) {
    // If it's a NODES level request, validate nodes field
    if (ugReq->nodes.empty()) {
      sendE2EAck(senderApp, false, "Nodes field was empty");
      return;
    }
    // Next validate node names
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    for (auto const& nodeName : ugReq->nodes) {
      if (!lockedTopologyW->getNode(nodeName)) {
        sendE2EAck(senderApp, false, "Invalid node: " + nodeName);
        return;
      }
    }
  } else {
    // Validate the exclude nodes field
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    for (auto const& nodeName : ugReq->excludeNodes) {
      if (!lockedTopologyW->getNode(nodeName)) {
        sendE2EAck(senderApp, false, "Invalid node: " + nodeName);
        return;
      }
    }
  }

  if (ugReq->urReq.urType != thrift::UpgradeReqType::RESET_STATUS) {
    // Validity check on timeout
    if (ugReq->timeout <= 0) {
      sendE2EAck(
          senderApp,
          false,
          folly::sformat("Invalid timeout: {}", ugReq->timeout));
      return;
    }
    if (ugReq->retryLimit < 0) {
      sendE2EAck(
          senderApp,
          false,
          folly::sformat("Invalid retryLimit: {}", ugReq->retryLimit));
      return;
    }
  }

  switch (ugReq->urReq.urType) {
    case thrift::UpgradeReqType::PREPARE_UPGRADE:
    case thrift::UpgradeReqType::COMMIT_UPGRADE:
      LOG(INFO) << "Adding request to queue : " + ugReq->urReq.upgradeReqId;
      pendingReqs_.push_back(*ugReq);
      sendE2EAck(senderApp, true, ugReq->urReq.upgradeReqId);
      break;
    case thrift::UpgradeReqType::FULL_UPGRADE:
      LOG(INFO) << "Adding request to queue : " + ugReq->urReq.upgradeReqId;
      scheduleFullUpgrade(*ugReq);
      sendE2EAck(senderApp, true, ugReq->urReq.upgradeReqId);
      break;
    case thrift::UpgradeReqType::RESET_STATUS:
      processResetReq(*ugReq);
      sendE2EAck(senderApp, true, ugReq->urReq.upgradeReqId);
      return;
    default:
      LOG(ERROR) << "Unknown upgrade request type";
      sendE2EAck(senderApp, false, "Unknown upgrade request type");
      return;
  }

  // Log event
  eventClient_->logEventThrift(
    thrift::EventCategory::UPGRADE,
    thrift::EventId::UPGRADE_INFO,
    thrift::EventLevel::INFO,
    folly::sformat(
        "Received software upgrade request ({})",
        TEnumMapFactory<thrift::UpgradeReqType>::
            makeValuesToNamesMap().at(ugReq->urReq.urType)),
    ugReq.value());
}

void
UpgradeApp::processResetReq(const thrift::UpgradeGroupReq& ugReq) {
  LOG(INFO) << "Processing : " << UpgradeAppUtil::getReqDesc(ugReq);
  std::vector<std::string> nodeNames;

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  switch (ugReq.ugType) {
    case thrift::UpgradeGroupType::NODES:
      nodeNames = ugReq.nodes;
      break;
    case thrift::UpgradeGroupType::NETWORK:
      std::unordered_set<std::string> excludeNodes(
          ugReq.excludeNodes.begin(), ugReq.excludeNodes.end());

      nodeNames = folly::gen::from(lockedTopologyW->getAllNodes()) |
          folly::gen::field(&thrift::Node::name) |
          folly::gen::filter([excludeNodes] (auto const& nodeName) {
              return !excludeNodes.count(nodeName);
          }) | folly::gen::as<std::vector<std::string>>();
      break;
  }

  LOG(INFO) << "Resetting nodes : " << folly::join(",", nodeNames);
  for (auto const& nodeName : nodeNames) {
    // Check if node is valid
    auto nodeMac = lockedTopologyW->getMacByNodeName(nodeName);
    if (nodeMac && !nodeMac->empty()) {
      sendToMinionApp(
          *nodeMac,   // Minion mac addr
          E2EConsts::kUpgradeAppMinionId,            // Minion app id
          thrift::MessageType::UPGRADE_REQ,          // Type of message
          ugReq.urReq);
    }
  }
}

void
UpgradeApp::syncWithStatusReports() {
  // Copy current status reports
  minionToStatus_ = {};
  {
    auto lockedStatusReports = SharedObjects::getStatusReports()->rlock();
    for (const auto& kv : *lockedStatusReports) {
      minionToStatus_[kv.first] = kv.second.report;
    }
  }

  // Populate the next batch if current batch has no pending nodes
  if (!curBatch_.pending()) {
    populateNextBatch();

    if (!curBatch_.pending()) {
      return;  // nothing to process
    }
  }

  /*
  * Use the latest status dump to check the progress for all nodes in the
  * curBatch_.
  */
  auto ugReq = curBatch_.getReq();
  switch (ugReq->urReq.urType) {
    case thrift::UpgradeReqType::PREPARE_UPGRADE:
      checkPrepareBatch();
      break;
    case thrift::UpgradeReqType::COMMIT_UPGRADE:
      checkCommitBatch();
      break;
    default:
      break;
  }

  // Check for timeout
  if (curBatch_.timedOut()) {
    LOG(INFO) << "Operation on current batch timed out";
    for (const auto& nodeName : curBatch_.getNodeNames()) {
      // Add the node back to a pending batch if possible
      scheduleRetry(nodeName, ugReq);
    }
    curBatch_.clear();
  }
}

void
UpgradeApp::processUpgradeAddImageReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  LOG(INFO) << "Received UpgradeAddImageReq message from " << minion << ":"
            << senderApp;
  if (!FLAGS_enable_bt_tracker_upgrades && FLAGS_upgrade_image_http_path.empty()) {
    sendE2EAck(senderApp, false, "Feature disabled");
    return;
  }
  auto addReq = maybeReadThrift<thrift::UpgradeAddImageReq>(message);
  if (!addReq) {
    handleInvalidMessage("UpgradeAddImageReq", senderApp, minion);
    return;
  }

  // Check available disk space
  struct statfs fs;
  if (statfs(FLAGS_upgrade_image_local_dir.c_str(), &fs) == 0 &&
      fs.f_bsize * fs.f_bfree < FLAGS_upgrade_image_min_free_space) {
    LOG(ERROR) << "Trying to download image when out of disk space ("
               << (fs.f_bsize * fs.f_bfree) << " available, but "
               << FLAGS_upgrade_image_min_free_space << " required)";
    sendE2EAck(senderApp, false, "Out of disk space");
    return;
  }

  // Generate temporary save path
  char pathTemplate[] = "/tmp/imageXXXXXX";
  int fd = mkstemp(pathTemplate);
  if (fd == -1) {
    LOG(ERROR) << "Failed to create temporary file to download upgrade image";
    sendE2EAck(senderApp, false, "Could not create file");
    return;
  }
  close(fd);
  std::string savePath(pathTemplate);

  // Download in a new thread
  std::thread downloadThread([addReq, savePath, senderApp, this]() {
      // Download image
      LOG(INFO) << "Start downloading " << addReq->imageUrl;
      std::string err;
      if (!CurlUtil::download(addReq->imageUrl, savePath, err)) {
        LOG(ERROR) << err;
        this->sendE2EAck(senderApp, false, "Failed to download image");
        return;
      }
      LOG(INFO) << "Successfully downloaded " << addReq->imageUrl << " to "
                << savePath;

      // Find the MD5 of the new image - excluding its header
      std::string md5;
      try {
        md5 = UpgradeUtils::getImageMd5(savePath);
      } catch (std::exception& e) {
        LOG(ERROR) << folly::exceptionStr(e);
        std::remove(savePath.c_str());
        this->sendE2EAck(senderApp, false, e.what());
        return;
      }

      // Generate filename for the new image
      std::string newFilename = md5 + ".bin";
      std::string newPath = FLAGS_upgrade_image_local_dir + newFilename;
      if (access(newPath.c_str(), F_OK) != -1) {
        // Filename is already taken
        LOG(ERROR) << "Duplicate upgrade image upload: " << newPath;
        std::remove(savePath.c_str());
        sendE2EAck(senderApp, false,
            "Another file already exists with the same MD5 hash: " + md5);
        return;
      }

      // Copy the file and delete the original
      // NOTE: Can't move/rename because new path might be on a different device
      try {
        boost::filesystem::copy(savePath.c_str(), newPath.c_str());
      } catch (const std::exception& ex) {
        // Failed to move file
        LOG(ERROR) << "Failed to move " << savePath << " to " << newPath
                   << ": " << folly::exceptionStr(ex);
        std::remove(savePath.c_str());
        sendE2EAck(senderApp, false, "Failed to access image directory");
        return;
      }
      LOG(INFO) << "Moved " << savePath << " to " << newPath;
      std::remove(savePath.c_str());

      // Start seeding image
      auto imageName =
          this->processImageFile(FLAGS_upgrade_image_local_dir, newFilename);
      if (!imageName) {
        std::remove(newPath.c_str());
        this->sendE2EAck(senderApp, false, "Failed to seed image");
        return;
      }

      this->eventClient_->logEvent(
          thrift::EventCategory::UPGRADE,
          thrift::EventId::UPGRADE_IMAGE_INFO,
          thrift::EventLevel::INFO,
          folly::sformat("Added and seeding image: {}", imageName.value()));
      this->sendE2EAck(senderApp, true, "Finished downloading image");
  });
  downloadThread.detach();

  sendE2EAck(senderApp, true, "Download starting...");
}

void
UpgradeApp::processUpgradeDelImageReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  LOG(INFO) << "Received UpgradeDelImageReq message from " << minion << ":"
            << senderApp;
  if (!FLAGS_enable_bt_tracker_upgrades && FLAGS_upgrade_image_http_path.empty()) {
    sendE2EAck(senderApp, false, "Feature disabled");
    return;
  }
  auto delReq = maybeReadThrift<thrift::UpgradeDelImageReq>(message);
  if (!delReq) {
    handleInvalidMessage("UpgradeDelImageReq", senderApp, minion);
    return;
  }

  // Find requested image
  auto iter = imageList_.find(delReq->name);
  if (iter == imageList_.end()) {
    sendE2EAck(senderApp, false, "Image not found");
    return;
  }

  // Stop seeding image (for BitTorrent) and delete image from disk
  LOG(INFO) << "Deleting image: " << delReq->name;
  if (iter->second.ltHandle.has_value()) {
    ltSession_.remove_torrent(
        *(iter->second.ltHandle), libtorrent::session::delete_files);
  }
  imageList_.erase(iter);

  eventClient_->logEvent(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_IMAGE_INFO,
      thrift::EventLevel::INFO,
      folly::sformat("Deleted image: {}", delReq->name));
  sendE2EAck(senderApp, true, "Image deleted");
}

void
UpgradeApp::processUpgradeListImagesReq(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) {
  VLOG(4) << "Received UpgradeListImagesReq message from " << minion << ":"
          << senderApp;
  if (!FLAGS_enable_bt_tracker_upgrades && FLAGS_upgrade_image_http_path.empty()) {
    sendE2EAck(senderApp, false, "Feature disabled");
    return;
  }
  auto listReq = maybeReadThrift<thrift::UpgradeListImagesReq>(message);
  if (!listReq) {
    handleInvalidMessage("UpgradeListImagesReq", senderApp, minion);
    return;
  }

  // Send list of images to sender
  std::vector<thrift::UpgradeImage> images;
  for (const auto& kv : imageList_) {
    images.push_back(kv.second.upgradeImage);
  }
  thrift::UpgradeListImagesResp upgradeListImageResp;
  upgradeListImageResp.images = images;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::UPGRADE_LIST_IMAGES_RESP,
      upgradeListImageResp);
}

void
UpgradeApp::checkPrepareBatch() {
  auto ugReq = curBatch_.getReq();
  auto reqId = ugReq->urReq.upgradeReqId;

  LOG(INFO) << folly::format(
      "Monitoring prepare status for {} nodes (Req ID: {})",
      curBatch_.size(),
      reqId);

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  for (const auto& nodeName : curBatch_.getNodeNames()) {
    auto nodeMac = lockedTopologyW->getMacByNodeName(nodeName);
    if (!nodeMac) {
      LOG(ERROR) << "Unrecognized node name (" << nodeName << ")";
      curBatch_.removeNode(nodeName);
      continue;
    }

    // Nodes have already been validated in processUpgradeGroupReq
    const thrift::StatusReport& statusReport = minionToStatus_[*nodeMac];
    const thrift::UpgradeStatus& uStatus = statusReport.upgradeStatus;

    // Log message about node
    std::string nodeLog;

    // Check if prepare succeded
    if (UpgradeAppUtil::isPrepared(statusReport, *ugReq)) {
      // Prepare succeded
      nodeLog = "Prepare success";
      curBatch_.removeNode(nodeName);
      upgradeStatus_[nodeName] = true;

      eventClient_->logEventDynamic(
          thrift::EventCategory::UPGRADE,
          thrift::EventId::UPGRADE_PREPARE,
          thrift::EventLevel::INFO,
          nodeLog,
          folly::dynamic::object("request_id", reqId),
          nodeMac.value(),
          nodeMac.value(),
          nodeName);

    } else if (UpgradeAppUtil::prepareFailed(statusReport, *ugReq)) {
      nodeLog = folly::sformat("Prepare failed: {}", uStatus.reason);
      curBatch_.removeNode(nodeName);
      upgradeStatus_[nodeName] = false;

      eventClient_->logEventDynamic(
          thrift::EventCategory::UPGRADE,
          thrift::EventId::UPGRADE_PREPARE,
          thrift::EventLevel::ERROR,
          "Prepare failed",
          folly::dynamic::object("request_id", reqId)("reason", uStatus.reason),
          nodeMac.value(),
          nodeMac.value(),
          nodeName);

      scheduleRetry(nodeName, ugReq);
    } else {
      // prepare in progress
      nodeLog = "Prepare in progress";
    }

    // This statement can flood the log very easily
    VLOG(3) << folly::format(
        "{}: {}. {} (Req ID: {})",
        nodeName,
        folly::get_default(
            TEnumMapFactory<thrift::UpgradeStatusType>::makeValuesToNamesMap(),
            uStatus.usType,
            "UNKNOWN"),
        nodeLog,
        reqId);
  }

  // Print BitTorrent seeding status (when applicable)
  if (FLAGS_enable_bt_tracker_upgrades &&
      ugReq->urReq.torrentParams_ref().has_value()) {
    for (const auto& kv : imageList_) {
      if (ugReq->urReq.imageUrl != kv.second.upgradeImage.magnetUri) {
        continue;  // not this image
      }

      libtorrent::torrent_status status = kv.second.ltHandle->status();

      VLOG(2) << folly::sformat(
          "BitTorrent seeder status: "
          "{:.1f} kB/s down, {:.1f} kB/s up, "
          "{:d} seed(s), {:d} peer(s), {:d} connection(s)",
          (float)status.download_rate / 1000,
          (float)status.upload_rate / 1000,
          status.num_seeds,
          status.num_peers,
          status.num_connections);

      // Log stats
      setCounter(
          "upgrade.bt.progressPpm",
          status.progress_ppm,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.seeds",
          status.num_seeds,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.peers",
          status.num_peers,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.connections",
          status.num_connections,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.downloadBps",
          status.download_rate,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.uploadBps",
          status.upload_rate,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.downloadPayloadBytes",
          status.total_payload_download,
          fbzmq::thrift::CounterValueType::GAUGE);
      setCounter(
          "upgrade.bt.uploadPayloadBytes",
          status.total_payload_upload,
          fbzmq::thrift::CounterValueType::GAUGE);

      break;
    }
  }
}

void
UpgradeApp::checkCommitBatch() {
  auto ugReq = curBatch_.getReq();
  auto reqId = ugReq->urReq.upgradeReqId;

  LOG(INFO) << folly::format(
      "Monitoring commit status for {} nodes (Req ID: {})",
      curBatch_.size(),
      reqId);

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto bgpPeers = curBatch_.getBgpPeers();
  for (const std::string& nodeName : curBatch_.getNodeNames()) {
    auto node = lockedTopologyW->getNode(nodeName);
    if (!node) {
      LOG(ERROR) << "Unrecognized node name (" << nodeName << ")";
      curBatch_.removeNode(nodeName);
      continue;
    }

    // Nodes have already been validated in processUpgradeGroupReq
    const thrift::StatusReport& statusReport = minionToStatus_[node->mac_addr];
    const thrift::UpgradeStatus& uStatus = statusReport.upgradeStatus;

    // Log message about node
    std::string nodeLog;

    const thrift::ImageMeta& nextImage = nodeToNextImage_[nodeName];
    if (UpgradeAppUtil::commitSuccess(statusReport, nextImage.version)) {
      // Node is up - next, wait for links to come up
      auto links = lockedTopologyW->getLinksByNodeName(nodeName);
      if (UpgradeAppUtil::wirelessLinkAlive(links, curBatch_.getSkipLinks())) {
        // Lastly, wait for BGP peers on POPs
        size_t currentPeerCount =
            statusReport.bgpStatus_ref().has_value() ?
                statusReport.bgpStatus_ref().value().size() : 0;
        auto iter = bgpPeers.find(nodeName);
        if (iter == bgpPeers.end() || currentPeerCount >= iter->second) {
          nodeLog = "Commit success";
          curBatch_.removeNode(nodeName);
          upgradeStatus_[nodeName] = true;

          eventClient_->logEventDynamic(
              thrift::EventCategory::UPGRADE,
              thrift::EventId::UPGRADE_COMMIT,
              thrift::EventLevel::INFO,
              nodeLog,
              folly::dynamic::object("request_id", reqId),
              node->mac_addr,
              node->mac_addr,
              nodeName);
        } else {
          nodeLog = folly::sformat(
              "Waiting for BGP peers to come up (expecting {}, have {}).",
              iter->second,
              currentPeerCount);
        }
      } else {
        nodeLog = "Waiting for wireless links to come up.";
      }
    } else if (UpgradeAppUtil::commitFailure(statusReport, nextImage.version)) {
      nodeLog = folly::sformat("Commit failure: {}", uStatus.reason);
      curBatch_.removeNode(nodeName);
      upgradeStatus_[nodeName] = false;

      eventClient_->logEventDynamic(
          thrift::EventCategory::UPGRADE,
          thrift::EventId::UPGRADE_COMMIT,
          thrift::EventLevel::ERROR,
          "Commit failure",
          folly::dynamic::object("request_id", reqId)("reason", uStatus.reason),
          node->mac_addr,
          node->mac_addr,
          nodeName);

      scheduleRetry(nodeName, ugReq);
    } else {
      // Commit in progress
      nodeLog = folly::sformat(
          "Reboot in progress. Last seen: {}s ago",
          std::difftime(
              std::time(nullptr), (std::time_t)statusReport.timeStamp));
    }

    VLOG(3) << folly::format(
        "{}: {}. {} (Req ID: {})",
        nodeName,
        folly::get_default(
            TEnumMapFactory<thrift::UpgradeStatusType>::makeValuesToNamesMap(),
            uStatus.usType,
            "UNKNOWN"),
        nodeLog,
        reqId);
  }
}

void
UpgradeApp::filterPrepareBatch(UpgradeApp::Batch& batch) {
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto ugReq = batch.getReq();

  for (auto const& nodeName : batch.getNodeNames()) {
    auto node = lockedTopologyW->getNode(nodeName);
    // Check if node exists in topology
    if (!node) {
      LOG(INFO)
          << folly::format("{}: Missing from topology. Skipping", nodeName);
      batch.removeNode(nodeName);
      continue;
    }
    if (node->mac_addr.empty()) {
      LOG(INFO) << folly::format(
          "{}: Empty node MAC in topology. Skipping", nodeName);
      batch.removeNode(nodeName);
      continue;
    }
    // Check if node is online
    if (node->status == thrift::NodeStatusType::OFFLINE) {
      LOG(INFO) << folly::format("{}: Node is offline", nodeName);
      batch.removeNode(nodeName);
      scheduleRetry(nodeName, ugReq);
      continue;
    }
    // Check if node is ready to prepare
    std::string errMsg;
    const auto& statusReport = minionToStatus_[node->mac_addr];
    if (!UpgradeAppUtil::isPrepareReady(statusReport, *ugReq, errMsg)) {
      LOG(INFO) << folly::format(
          "{} not ready to prepare. {}. Skipping", nodeName, errMsg);
      batch.removeNode(nodeName);
      continue;
    }
  }
}

void
UpgradeApp::filterCommitBatch(UpgradeApp::Batch& batch) {
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  auto ugReq = batch.getReq();
  for (const auto& nodeName : batch.getNodeNames()) {
    auto node = lockedTopologyW->getNode(nodeName);

    // Check if node exists in topology
    if (!node) {
      LOG(INFO)
          << folly::format("{}: Missing from topology. Skipping", nodeName);
      batch.removeNode(nodeName);
      continue;
    }
    if (node->mac_addr.empty()) {
      LOG(INFO) << folly::format(
          "{}: Empty node MAC in topology. Skipping", nodeName);
      batch.removeNode(nodeName);
      continue;
    }
    // Check if node is online
    if (node->status == thrift::NodeStatusType::OFFLINE) {
      LOG(INFO) << folly::format("{}: Node is offline", nodeName);
      batch.removeNode(nodeName);
      scheduleRetry(nodeName, ugReq);
      continue;
    }
    // Check if node is ready to commit
    std::string errMsg;
    const auto& statusReport = minionToStatus_[node->mac_addr];
    if (!UpgradeAppUtil::isCommitReady(
        statusReport, *ugReq, errMsg)) {
      LOG(INFO) << folly::format(
          "{} not ready to commit. {}. Skipping", nodeName, errMsg);
      batch.removeNode(nodeName);
      continue;
    }
  }
}

void
UpgradeApp::populateNextBatch() {
  // Convert the next request into batches if required
  if (pendingBatches_.empty()) {
    populateNextReq();
    if (pendingBatches_.empty()) {
      return;
    }
  }

  auto batch = pendingBatches_.front();
  pendingBatches_.pop_front();
  auto ugReq = batch.getReq();

  LOG(INFO) << folly::format(
      "Processing next pending batch with {} node(s)", batch.size());

  // Dispatch requests to the minion nodes
  switch (ugReq->urReq.urType) {
    case thrift::UpgradeReqType::PREPARE_UPGRADE:
      // filter out nodes that don't need to be prepared
      filterPrepareBatch(batch);

      // Return if batch is empty
      if (!batch.pending()) {
        VLOG(2) << "Batch is empty after filtering, no nodes to prepare";
        return;
      }

      // Check if we need to limit the number of nodes to prepare in parallel
      if (ugReq->limit <= 0 || batch.size() <= ugReq->limit) {
        // prepare all remaining nodes in parallel
        initiatePrepare(batch);
      } else {
        // Pick the first ugReq->limit nodes for prepare
        auto batchNodeNames = batch.getNodeNames();
        std::unordered_set<std::string> nodesToPrepare;
        auto itr = batchNodeNames.begin();
        for (int count = 0;
            itr != batchNodeNames.end() && count < ugReq->limit;
            ++count) {
          nodesToPrepare.insert(*itr);
          itr = batchNodeNames.erase(itr);
        }

        // Add remaining nodes to pending queue
        addToPendingBatches(batchNodeNames, ugReq);

        Batch prepareBatch(nodesToPrepare, ugReq);
        initiatePrepare(prepareBatch);
      }
      break;
    case thrift::UpgradeReqType::COMMIT_UPGRADE: {
      // Filter out nodes not ready to commit
      filterCommitBatch(batch);

      // Return if batch is empty
      if (!batch.pending()) {
        VLOG(2) << "Batch is empty after filtering, no nodes to commit";
        return;
      }

      // Find the nodes ready to commit and put other nodes back into
      // the pending queue
      auto batchNodeNames = batch.getNodeNames();
      std::unordered_set<std::string> nodesToCommit;
      if (ugReq->limit < 0) {
        // if limit < 0, commit all at once
        nodesToCommit = batchNodeNames;
      } else {
        auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
        nodesToCommit = UpgradeAppUtil::getCommitCandidates(
            *lockedTopologyW, batchNodeNames, (int)ugReq->limit);
      }

      if (nodesToCommit.empty()) {
        // Corner Case: If no node was picked by the algorithm, schedule all
        // remaining nodes to commit at once. (This only happens when nodes are
        // reachable from controller but have no programmed path in topology
        // file)
        nodesToCommit = batchNodeNames;
        batchNodeNames.clear();
        LOG(ERROR) << folly::format(
            "Committing alive nodes unreachable via topology: {}",
            folly::join(", ", nodesToCommit));
      } else {
        // Remove nodes picked for commit from batch
        for (const auto& nodeName : nodesToCommit) {
          batchNodeNames.erase(nodeName);
        }
      }

      // Add remaining nodes in batch to pending queue
      addToPendingBatches(batchNodeNames, ugReq);

      // Start commit for picked nodes
      if (!nodesToCommit.empty()) {
        Batch commitBatch(nodesToCommit, ugReq);
        initiateCommit(commitBatch);
      }
      break;
    }
    default:
      break;
  }
}

void
UpgradeApp::addToPendingBatches(
    std::unordered_set<std::string>& nodeNames,
    std::shared_ptr<thrift::UpgradeGroupReq> ugReq) {
  // If there is a batch pending. Add nodes to it
  if (!nodeNames.empty()) {
    if (!pendingBatches_.empty()) {
      pendingBatches_.front().addNodes(nodeNames);
    } else {
      pendingBatches_.push_front(Batch(nodeNames, ugReq));
    }
  }
}

void
UpgradeApp::populateNextReq() {
  // Log the results of the current request before processing the next one
  if (!upgradeStatus_.empty()) {
    int numFailed = 0;
    for (const auto& kv : upgradeStatus_) {
      if (!kv.second) {
        numFailed++;
      }
    }

    auto eventId =
        curReq_->urReq.urType == thrift::UpgradeReqType::PREPARE_UPGRADE
            ? thrift::EventId::UPGRADE_PREPARE
            : thrift::EventId::UPGRADE_COMMIT;

    if (numFailed) {
      eventClient_->logEventDynamic(
          thrift::EventCategory::UPGRADE,
          eventId,
          thrift::EventLevel::ERROR,
          "Upgrade process failed",
          folly::dynamic::object("request_id", curReq_->urReq.upgradeReqId)(
              "num_success",
              upgradeStatus_.size() - numFailed)("num_failed", numFailed));
    } else {
      eventClient_->logEventDynamic(
          thrift::EventCategory::UPGRADE,
          eventId,
          thrift::EventLevel::INFO,
          "Upgrade process succeeded",
          folly::dynamic::object("request_id", curReq_->urReq.upgradeReqId));
    }

    // Update golden image config structure
    updateGoldenImageAfterUpgrade(*curReq_);
  }

  // Reset state for the current request
  if (curReq_) {
    curReq_.reset();
    upgradeStatus_.clear();
    nodeToNextImage_.clear();
  }

  // Clear other request state as we process a new request.
  curReqNodes_.clear();
  nodeName2UpgradeRetries_.clear();

  // If no more pending requests, return
  if (pendingReqs_.empty()) {
    return;
  }

  curReq_= std::make_shared<thrift::UpgradeGroupReq>(pendingReqs_.front());
  pendingReqs_.pop_front();
  std::string urTypeStr = folly::get_default(
      TEnumMapFactory<thrift::UpgradeReqType>::makeValuesToNamesMap(),
      curReq_->urReq.urType,
      "UNKNOWN");
  LOG(INFO) << "Processing queued request " << curReq_->urReq.upgradeReqId
            << " (" << urTypeStr << ")";

  // Handle FULL_UPGRADE (requeue as prepare/commit separately)
  // TODO clean this up, e.g. process "prepare" immediately here
  if (curReq_->urReq.urType == thrift::UpgradeReqType::FULL_UPGRADE) {
    scheduleFullUpgrade(*curReq_);
    return;
  }

  // Identify all nodes to upgrade
  std::unordered_set<std::string> nodeNames;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  if (curReq_->ugType == thrift::UpgradeGroupType::NODES) {
    for (const std::string& nodeName : curReq_->nodes) {
      auto node = lockedTopologyW->getNode(nodeName);
      if (!node) {
        LOG(ERROR) << "Skipping unknown node in upgrade request: " << nodeName;
        continue;
      }
      nodeNames.insert(nodeName);
      curReqNodes_[nodeName] = node->mac_addr;
    }
  } else {
    // filter out exclusions
    auto excludeNodes = folly::gen::from(curReq_->excludeNodes) |
                        folly::gen::as<std::unordered_set<std::string>>();
    for (const auto& node : lockedTopologyW->getAllNodes()) {
      if (!excludeNodes.count(node.name)) {
        nodeNames.insert(node.name);
        curReqNodes_[node.name] = node.mac_addr;
      }
    }
  }
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL
  if (nodeNames.empty()) {
    LOG(ERROR) << "No nodes to upgrade in request: "
               << curReq_->urReq.upgradeReqId;
    return;
  }
  addToPendingBatches(nodeNames, curReq_);

  // Log an event marking the start of a new upgrade request
  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_INFO,
      thrift::EventLevel::INFO,
      "Processing queued request",
      *curReq_);
}

void
UpgradeApp::clearCurReq(bool resetStatus, const std::string& reason) {
  // Get the current req id. Tricky!
  // TODO can we just use curReq_ here?
  thrift::UpgradeGroupReq curReq;
  if (curBatch_.pending()) {
    curReq = *curBatch_.getReq();
  } else if (!pendingBatches_.empty() && pendingBatches_.front().pending()) {
    curReq = *pendingBatches_.front().getReq();
  } else {
    LOG(INFO) << "No current request to clear";
    return;
  }
  std::string reqId = curReq.urReq.upgradeReqId;

  LOG(INFO) << folly::format(
      "Aborting current request (Req ID: {}), {}. Pending nodes: {}",
      reqId,
      reason,
      folly::join(", ", curBatch_.getNodeNames()));
  curBatch_.clear();

  LOG(INFO)
      << folly::format("Clearing {} pending batches", pendingBatches_.size());
  pendingBatches_.clear();

  clearPendingReqById(reqId, reason);

  // Optionally send RESET_STATUS to all nodes in this request
  if (resetStatus) {
    for (const auto& kv : curReqNodes_) {
      VLOG(2) << "Sending RESET_STATUS to " << kv.first;
      thrift::UpgradeReq urReq;
      urReq.urType = thrift::UpgradeReqType::RESET_STATUS;
      urReq.upgradeReqId = reqId;
      sendToMinionApp(
          kv.second,
          E2EConsts::kUpgradeAppMinionId,
          thrift::MessageType::UPGRADE_REQ,
          urReq);
    }
  }
}

void
UpgradeApp::clearPendingReqById(
    const std::string& reqId, const std::string& reason) {
  LOG(INFO) << folly::format(
      "Clearing pending requests with ID: {} ({})", reqId, reason);

  pendingReqs_.erase(
      std::remove_if(pendingReqs_.begin(), pendingReqs_.end(),
          [reqId] (const auto& req) {
              return reqId == req.urReq.upgradeReqId;
          }),
      pendingReqs_.end());
}

void
UpgradeApp::scheduleRetry(
    const std::string& nodeName,
    std::shared_ptr<thrift::UpgradeGroupReq> ugReq) {
  if (!ugReq->skipPopFailure) {
    // We should halt the upgrade on a pop upgrade failure
    auto bgpPeers = curBatch_.getBgpPeers();
    if (bgpPeers.count(nodeName)) {
      // Pop failed to upgrade
      // Stop the current request and any pending request with the same ID.
      clearPendingReqById(
          ugReq->urReq.upgradeReqId,
          folly::sformat("POP node {} failed to upgrade properly", nodeName));
      return;
    }
  }

  auto& nodeRetries = nodeName2UpgradeRetries_[nodeName];
  if (nodeRetries < (size_t) ugReq->retryLimit) {
    ++nodeRetries;
    LOG(INFO) << folly::format(
        "{} is scheduled for retry #{}", nodeName, nodeRetries);
    std::unordered_set<std::string> nodeNameSet({nodeName});
    // Add the node to the first pending batch.
    addToPendingBatches(nodeNameSet, ugReq);
  } else if (!ugReq->skipFailure) {
    // Stop the current request. No more retries are possible
    clearCurReq(false /* resetStatus */, folly::sformat(
        "{} reached max retries and skipFailure is false", nodeName));
  } else {
    LOG(INFO) << folly::format(
        "{} reached max number of retries ({}). Dropping.",
        nodeName,
        nodeRetries);
  }
}

void
UpgradeApp::scheduleFullUpgrade(thrift::UpgradeGroupReq req) {
  req.urReq.urType = thrift::UpgradeReqType::PREPARE_UPGRADE;
  pendingReqs_.push_back(req);

  req.urReq.urType = thrift::UpgradeReqType::COMMIT_UPGRADE;
  pendingReqs_.push_back(req);
}

void
UpgradeApp::initiatePrepare(UpgradeApp::Batch& batch) {
  auto ugReq = batch.getReq();

  LOG(INFO) << folly::format(
      "Initiating prepare request for nodes: {} (Req ID: {})",
      folly::join(", ", batch.getNodeNames()),
      ugReq->urReq.upgradeReqId);

  // Discard nodes that don't need to be prepared
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  for (auto const& nodeName : batch.getNodeNames()) {
    auto node = lockedTopologyW->getNode(nodeName);
    if (!node) {
      LOG(ERROR)
          << folly::format("{}: Missing from topology. Skipping.", nodeName);
      batch.removeNode(nodeName);
      continue;
    }

    // Dispatch request
    sendToMinionApp(
        node->mac_addr,
        E2EConsts::kUpgradeAppMinionId,
        thrift::MessageType::UPGRADE_REQ,
        ugReq->urReq);

    // Clear any existing failure status for this node
    auto iter = minionToStatus_.find(node->mac_addr);
    if (iter != minionToStatus_.end()) {
      iter->second.upgradeStatus.usType = thrift::UpgradeStatusType::NONE;
    }
  }

  if (batch.pending()) {
    curBatch_ = batch;
    curBatch_.initiate();
  }
}

void
UpgradeApp::initiateCommit(UpgradeApp::Batch& batch) {
  auto ugReq = batch.getReq();
  auto reqId = ugReq->urReq.upgradeReqId;

  LOG(INFO) << folly::format(
      "Initiating commit request for nodes: {} (Req ID: {})",
      folly::join(", ", batch.getNodeNames()),
      ugReq->urReq.upgradeReqId);

  // Discard nodes that don't need to commit
  std::vector<thrift::Node> nodesToCommit;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  for (auto const& nodeName : batch.getNodeNames()) {
    // Node should exist at this point. Double check!! why not?
    auto node = lockedTopologyW->getNode(nodeName);
    if (!node) {
      LOG(INFO)
          << folly::format("{}: Missing from topology. Skipping", nodeName);
      batch.removeNode(nodeName);
      continue;
    }
    nodesToCommit.emplace_back(node.value());
  }
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // Dispatch requests to surviving nodes
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->wlock();
  for (const auto& node : nodesToCommit) {
    const auto& statusReport = minionToStatus_[node.mac_addr];
    nodeToNextImage_[node.name] = statusReport.upgradeStatus.nextImage;
    if (FLAGS_enable_simultaneous_image_and_config_upgrade) {
      auto nodeConfig = lockedConfigHelper->buildNodeConfig(
          node.name,
          statusReport.upgradeStatus.nextImage.version,
          statusReport.firmwareVersion /* TODO pass in future version?? */,
          statusReport.hardwareBoardId,
          std::nullopt,
          std::nullopt,
          std::nullopt);
      if (lockedConfigHelper->isManaged(nodeConfig)) {
        ugReq->urReq.nextNodeConfigJson_ref() =
            JsonUtils::toSortedPrettyJson(nodeConfig);
      }
    }
    sendToMinionApp(
        node.mac_addr,
        E2EConsts::kUpgradeAppMinionId,
        thrift::MessageType::UPGRADE_REQ,
        ugReq->urReq);
  }
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  // If there is a delay to commit, don't follow up
  auto delay = batch.getReq()->urReq.scheduleToCommit_ref().value_or(0L);
  if (delay > 0) {
    LOG(INFO) << folly::format(
        "Batch {}: Will commit {}s later",
        folly::join(", ", batch.getNodeNames()),
        delay);
    batch.clear();
    return;
  }

  // Initiate the batch if there are any surviving nodes
  if (batch.pending()) {
    curBatch_ = batch;

    lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

    // Take snapshot of wireless links if it's a commit req
    auto skipLinks = UpgradeAppUtil::getDeadWirelessLinkNames(*lockedTopologyW);

    // Count number of BGP peers at each POP node
    std::unordered_map<std::string, size_t> bgpPeers;
    if (!ugReq->skipPopFailure) {
      for (const auto& node : nodesToCommit) {
        const auto& statusReport = minionToStatus_[node.mac_addr];
        if (statusReport.bgpStatus_ref().has_value() &&
            statusReport.bgpStatus_ref().value().size()) {
          bgpPeers[node.name] = statusReport.bgpStatus_ref().value().size();
        }
      }
    }

    lockedTopologyW.unlock();  // lockedTopologyW -> NULL

    // Add in the links provided by the user
    skipLinks.insert(ugReq->skipLinks.begin(), ugReq->skipLinks.end());
    LOG(INFO) << "Batch will ignore: " << folly::join(",", skipLinks);
    curBatch_.initiate(skipLinks, bgpPeers);
  }
}

void
UpgradeApp::updateGoldenImageAfterUpgrade(const thrift::UpgradeGroupReq& req) {
  // Fetch current golden image config
  thrift::UpgradeParams upgradeParams =
      SharedObjects::getE2EConfigWrapper()->rlock()->getConfig()->upgradeParams;
  thrift::GoldenImage goldenImage;
  if (upgradeParams.goldenImage_ref().has_value()) {
    goldenImage = upgradeParams.goldenImage_ref().value();
  }

  // Are we in a golden image upgrade right now?
  // If so, add/remove from blacklist based on success/failure
  if (req.urReq.upgradeReqId == goldenImageUpgradeId_) {
    if (req.urReq.urType == thrift::UpgradeReqType::PREPARE_UPGRADE) {
      auto iter = std::find_if(
          imageList_.begin(), imageList_.end(), [req](const auto& pair) {
              return pair.second.upgradeImage.md5 == req.urReq.md5;
          });
      if (iter == imageList_.end()) {
        return;  // image not found
      }
      for (const auto& kv : upgradeStatus_) {
        if (!kv.second) {
          goldenImage.nodeBlacklist[kv.first] = iter->first;
        }
      }
    } else if (req.urReq.urType != thrift::UpgradeReqType::COMMIT_UPGRADE) {
      for (const auto& kv : upgradeStatus_) {
        if (kv.second) {
          goldenImage.nodeBlacklist.erase(kv.first);
        } else {
          const thrift::ImageMeta& nextImage = nodeToNextImage_[kv.first];
          if (nextImage.version.empty()) {
            continue;  // image not found
          }
          goldenImage.nodeBlacklist[kv.first] = nextImage.version;
        }
      }
    } else {
      return;
    }
    setGoldenImage(goldenImage);
    return;
  }

  // If we're in a normal PREPARE or auto-setting config is disabled, quit
  if (!FLAGS_set_golden_image_after_commits ||
      req.urReq.urType != thrift::UpgradeReqType::COMMIT_UPGRADE) {
    return;
  }

  // For successful commits, record the golden image version
  std::unordered_map<std::string, std::string> versions;
  for (const auto& kv : upgradeStatus_) {
    const thrift::ImageMeta& nextImage = nodeToNextImage_[kv.first];
    if (!kv.second /* upgrade failed */ || nextImage.version.empty()) {
      continue;
    }

    // Look up this image version...
    auto imageListIter = imageList_.find(nextImage.version);
    if (imageListIter == imageList_.end()) {
      continue;  // image not found
    }
    const thrift::UpgradeImage& img = imageListIter->second.upgradeImage;

    // Update golden version map with all supported board IDs
    for (const std::string& boardId : img.hardwareBoardIds) {
      versions[boardId] = nextImage.version;
    }
  }

  // Update config versions and blacklist
  for (const auto& kv : versions) {
    const std::string& newVersion = kv.second;
    if (goldenImage.versions[kv.first] == newVersion) {
      continue;  // no version change
    }
    goldenImage.versions[kv.first] = newVersion;

    // Wipe nodes in blacklist with this version
    for (auto iter = goldenImage.nodeBlacklist.cbegin();
         iter != goldenImage.nodeBlacklist.cend();) {
      if (iter->second == newVersion) {
        iter = goldenImage.nodeBlacklist.erase(iter);
      } else {
        ++iter;
      }
    }
  }

  setGoldenImage(goldenImage);
}

bool
UpgradeApp::setGoldenImage(const thrift::GoldenImage& goldenImage) {
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  config.upgradeParams.goldenImage_ref() = goldenImage;
  return lockedE2EConfigWrapper->setConfigFromThrift(config);
}

void
UpgradeApp::goldenImageUpgrade() {
  // Do nothing until all upgrade requests are finished
  if (curReq_ || !pendingReqs_.empty()) {
    return;
  }

  // Validate config
  thrift::UpgradeParams upgradeParams =
      SharedObjects::getE2EConfigWrapper()->rlock()->getConfig()->upgradeParams;
  if (!upgradeParams.goldenImage_ref().has_value() ||
      !upgradeParams.goldenImage_ref().value().enabled ||
      upgradeParams.goldenImage_ref().value().versions.empty()) {
    return;  // feature is disabled
  }
  try {
    if (!UpgradeAppUtil::checkTimeRestriction(
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
        upgradeParams.goldenImage_ref().value().timeRestriction)) {
      return;  // time-restricted (not allowed now)
    }
  } catch (const std::invalid_argument& ex) {
    LOG(WARNING) << folly::exceptionStr(ex);
    return;  // invalid config
  }

  // Find nodes to upgrade, grouped by the software images they need
  // (will depend on hardwareBoardId)
  std::unordered_map<
      std::string /* version */, std::vector<std::string> /* node MACs */>
          upgradesPerVersion;
  size_t totalNodesToUpgrade = 0;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  for (const auto& versionPair :
       upgradeParams.goldenImage_ref().value().versions) {
    const std::string& version = versionPair.second;
    if (!imageList_.count(version)) {
      LOG(WARNING) << "Golden image version <" << version << "> does not exist";
      continue;  // version not found (i.e. not seeded by controller)
    }

    std::vector<std::string>& nodesToUpgrade = upgradesPerVersion[version];
    auto nodes = UpgradeAppUtil::findNodesToUpgradeToVersion(
        version,
        versionPair.first,
        upgradeParams.goldenImage_ref().value().nodeBlacklist,
        *lockedTopologyW,
        minionToStatus_);
    nodesToUpgrade.insert(nodesToUpgrade.end(), nodes.begin(), nodes.end());
    totalNodesToUpgrade += nodes.size();
  }
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL
  if (totalNodesToUpgrade == 0) {
    return;  // nothing to do
  }

  VLOG(2) << "Found " << totalNodesToUpgrade
          << " candidate node(s) to upgrade to the golden image";
  std::vector<thrift::UpgradeGroupReq> upgradeRequests;

  // Build the base request
  // Reuse the same request ID for all PREPARE/COMMIT requests
  goldenImageUpgradeId_ = UuidUtils::genUuid();
  thrift::UpgradeTorrentParams torrentParams;
  torrentParams.downloadTimeout =
      upgradeParams.goldenImage_ref().value().timeoutSec;
  torrentParams.downloadLimit_ref() = -1;
  torrentParams.uploadLimit_ref() = -1;
  torrentParams.maxConnections_ref() = -1;
  thrift::UpgradeReq urReq;
  urReq.urType = thrift::UpgradeReqType::PREPARE_UPGRADE;
  urReq.upgradeReqId = goldenImageUpgradeId_;
  urReq.torrentParams_ref() = torrentParams;
  thrift::UpgradeGroupReq req;
  req.ugType = thrift::UpgradeGroupType::NODES;
  req.urReq = urReq;
  req.timeout = upgradeParams.goldenImage_ref().value().timeoutSec;
  req.skipFailure = true;
  req.skipPopFailure = false;
  req.limit = 0;

  // Pick nodes up to the configured batch limit
  // Queue PREPARE requests for each version
  std::vector<std::string> allNodesToUpgrade;
  int batchLimit = upgradeParams.goldenImage_ref().value().batchLimit;
  for (const auto& upgradePair : upgradesPerVersion) {
    // Get upgrade image info
    const std::string& version = upgradePair.first;
    const thrift::UpgradeImage& image =
        imageList_.find(version)->second.upgradeImage;

    // Pick nodes
    std::vector<std::string> nodesToUpgrade;
    bool batchLimitReached = false;
    for (const std::string& nodeName : upgradePair.second) {
      if (batchLimit > 0 && (int) nodesToUpgrade.size() >= batchLimit) {
        batchLimitReached = true;
        break;
      }
      nodesToUpgrade.push_back(nodeName);
      allNodesToUpgrade.push_back(nodeName);
    }

    // Queue PREPARE
    if (!nodesToUpgrade.empty()) {
      req.nodes = nodesToUpgrade;
      req.urReq.md5 = image.md5;
      req.urReq.imageUrl = image.magnetUri;
      req.urReq.hardwareBoardIds = image.hardwareBoardIds;
      upgradeRequests.push_back(req);
    }

    if (batchLimitReached) {
      break;
    }
  }

  // Queue one COMMIT request for all prepared nodes
  req.nodes = allNodesToUpgrade;
  req.skipFailure = false;
  req.urReq.urType = thrift::UpgradeReqType::COMMIT_UPGRADE;
  req.urReq.scheduleToCommit_ref() = 0;
  req.urReq.md5.clear();
  req.urReq.imageUrl.clear();
  req.urReq.hardwareBoardIds.clear();
  upgradeRequests.push_back(req);

  VLOG(2) << "Picking " << allNodesToUpgrade.size()
          << " node(s) for golden image upgrade: "
          << folly::join(", ", allNodesToUpgrade);

  // Actually queue the requests
  pendingReqs_.insert(
      pendingReqs_.end(), upgradeRequests.begin(), upgradeRequests.end());

  // Log event
  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_INFO,
      thrift::EventLevel::INFO,
      "Upgrading nodes to golden image",
      upgradeRequests);
}

} // namespace terragraph
} // namespace facebook
