/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UpgradeApp.h"

#include <chrono>
#include <iomanip>
#include <string>
#include <thread>
#include <time.h>
#include <unordered_map>

#include <boost/filesystem.hpp>
#include <boost/lexical_cast.hpp>
#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/dynamic.h>
#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/torrent_status.hpp>

#include "e2e/common/Consts.h"
#include "e2e/common/CurlUtil.h"
#include "e2e/common/IpUtil.h"
#include "e2e/common/JsonUtils.h"
#include "e2e/common/SysUtils.h"
#include "e2e/common/UpgradeUtils.h"
#include "e2e/common/WatchdogUtils.h"

DEFINE_string(
    preloaded_config_dir,
    "/data/cfg/preload/",
    "Preloaded configuration directory");
DEFINE_string(
    image_dir, "/tmp/image_dir/", "Directory to download minion images to");
DEFINE_int32(
    bt_seeder_port,
    6881,
    "The port of the BitTorrent seeder used for image upgrades");
DEFINE_int32(
    bt_alert_mask,
    lt::alert::error_notification,
    "The libtorrent alert bitmask for logging purposes "
    "(see libtorrent::alert_category)");

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;
namespace lt = libtorrent;
namespace ft = ::facebook::terragraph;

namespace {
const int kOk = 0; // Script success

// Preload config paths
const std::string kPreloadedConfigFile =
    FLAGS_preloaded_config_dir + "node_config.json";
const std::string kPreloadedConfigVersion =
    FLAGS_preloaded_config_dir + "image_version";

// "Prepare" stage - HTTP constants
const int kDownloadAttemptUpperBound = 10;
const int kDownloadAttemptLowerBound = 1;
const int kDownloadAttemptDefault = 3;

// "Prepare" stage - torrent constants
const std::chrono::seconds kPrepareTorrentUpdateInterval{2};
const int kDefaultDownloadTimeout = 100;
const int kDefaultDownloadLimit = -1;
const int kDefaultUploadLimit = -1;
const int kDefaultMaxConnections = -1;

// Set testcode request in the u-boot environment for the boot partition that
// was flashed by the last successful PREPARE_UPGRADE.
const std::string kSetTestcodeRequestCmd{"/usr/sbin/testcode t >/dev/null"};
} // namespace

namespace facebook {
namespace terragraph {
namespace minion {

UpgradeApp::UpgradeApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr,
    bool useHttps,
    const std::string& ipv6GlobalAddressableIfname,
    UpgradeStateCache& upgradeStateCache)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kUpgradeAppMinionId),
      useHttps_(useHttps),
      ipv6GlobalAddressableIfname_(ipv6GlobalAddressableIfname),
      upgradeStatus_(upgradeStateCache.getUpgradeStatus()),
      upgradeStateCache_(upgradeStateCache) {
  prepareTorrentTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { downloadTorrentUpdate(); });
  scheduleCommitTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
    LOG(INFO) << "COMMIT_UPGRADE timeout expired. Committing...";
    commitUpgrade();
  });
}

UpgradeApp::~UpgradeApp() {
  resetPrepareTorrentState();
}

void
UpgradeApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::UPGRADE_REQ: {
      processUpgradeReq(senderApp, message);
      break;
    }
    default: {
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                     message.mType, "UNKNOWN")
          << ") received from " << senderApp;
    }
  }
}

void
UpgradeApp::processUpgradeReq(
    const std::string& senderApp, const thrift::Message& message) {
  auto upgradeReq = maybeReadThrift<thrift::UpgradeReq>(message);
  if (!upgradeReq) {
    handleInvalidMessage("UpgradeReq", senderApp);
    return;
  }

  // The cache is only valid from successful prepare/flash until commit.
  // Note also that the cache is only read once: during minion startup.
  upgradeStateCache_.invalidate();

  // Handle special reset requests
  if (upgradeReq->urType == thrift::UpgradeReqType::RESET_STATUS) {
    LOG(INFO) << "Received RESET_STATUS request from " << senderApp;
    resetStatus(upgradeReq.value());
    return;
  }

  std::string urTypeStr = folly::get_default(
      TEnumMapFactory<thrift::UpgradeReqType>::makeValuesToNamesMap(),
          upgradeReq->urType, "UNKNOWN");
  LOG(INFO) << "Received an upgrade request (" << urTypeStr << ") from "
            << senderApp << " for new image: " << upgradeReq->imageUrl;

  // Block prepare/commit requests while currently upgrading
  if (prepareTorrentTimer_->isScheduled()) {
    LOG(ERROR) << "Dropping new upgrade request (" << upgradeReq->upgradeReqId
               << ") due to ongoing 'prepare' procedure";
    return;
  }
  if (scheduleCommitTimeout_->isScheduled()) {
    LOG(ERROR) << "Dropping new upgrade request (" << upgradeReq->upgradeReqId
               << ") due to ongoing 'commit' procedure";
    return;
  }

  // Temporarily disable the TG watchdogs.
  // The TG watchdogs are re-enabled automatically by the watchdog framework
  // and also by a reboot (ie after a successful commit).
  //
  // Note that back-to-back watchdog disables are allowed, and the current
  // disable period is extended when needed, but it is never truncated.
  // Ex: Request for a 5 minute wdog suppression would add 2 minutes to
  // an existing suppression that had 3 minutes remaining, but it would not
  // alter an existing suppression that still had 10 minutes left to go.
  if (!WatchdogUtils::disableForUpgrade(upgradeReq->urType)) {
    LOG(ERROR) << "Failed to disable watchdog for upgrade";
  }

  if (upgradeReq->urType == thrift::UpgradeReqType::PREPARE_UPGRADE) {
    startPrepareUpgrade(upgradeReq.value());
  } else if (upgradeReq->urType == thrift::UpgradeReqType::COMMIT_UPGRADE) {
    scheduleCommitUpgrade(upgradeReq.value());
  } else {
    LOG(ERROR) << "Invalid upgrade type: " << urTypeStr;
  }
}

void
UpgradeApp::startPrepareUpgrade(const thrift::UpgradeReq& upgradeReq) {
  upgradeStatus_.usType = thrift::UpgradeStatusType::DOWNLOADING_IMAGE;
  upgradeStatus_.reason = "";
  upgradeStatus_.nextImage.md5 = upgradeReq.md5;
  upgradeStatus_.nextImage.version = "";
  upgradeStatus_.upgradeReqId = upgradeReq.upgradeReqId;

  sendUpgradeStatus();
  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_INFO,
      thrift::EventLevel::INFO,
      "Starting PREPARE",
      upgradeReq);

  // -- uri check --
  std::unique_ptr<folly::Uri> imageUrl;
  try {
    imageUrl = std::make_unique<folly::Uri>(upgradeReq.imageUrl);
  } catch (const std::exception& ex) {
    sendPrepareDownloadFailure("Invalid image URI");
    return;
  }

  boost::filesystem::path imagePath(FLAGS_image_dir);
  boost::system::error_code errorCode;

  // Clear download directory
  if (boost::filesystem::exists(imagePath)) {
    if (!boost::filesystem::remove_all(imagePath, errorCode)) {
      sendPrepareDownloadFailure(
          "Couldn't clear existing image download directory");
      return;
    }
  }

  // Create image directory
  if (!boost::filesystem::create_directories(imagePath, errorCode)) {
    sendPrepareDownloadFailure("Couldn't create image download directory");
    return;
  }

  // Start downloading image
  if (imageUrl->scheme() == "magnet") {
    downloadTorrent(*imageUrl, FLAGS_image_dir, upgradeReq);
  } else {
    downloadHttp(*imageUrl, FLAGS_image_dir, upgradeReq);
  }
}

void
UpgradeApp::prepareProcessImage() {
  // -- read meta info out from downloaded image --
  auto downloadedMeta = getMetaInfo();
  if (!downloadedMeta) {
    sendPrepareDownloadFailure("Unable to read and parse meta information");
    return;
  }

  // -- md5 check on downloaded image --
  // Note: this check only compares md5 in downloaded image file against md5
  // from UpgradeReq. It prevents downloading an image with right name but
  // wrong version.
  if (downloadedMeta->md5 != upgradeStatus_.nextImage.md5) {
    sendPrepareDownloadFailure("Given MD5 does not match downloaded meta MD5");
    return;
  }
  LOG(INFO) << "Passed MD5 check, downloaded meta file has expected MD5.";

  upgradeStatus_.nextImage.version = downloadedMeta->version;
  upgradeStatus_.usType = thrift::UpgradeStatusType::FLASHING_IMAGE;
  upgradeStatus_.reason = "";
  sendUpgradeStatus();

  // -- flash image --
  if (!flashMinionImage()) {
    upgradeStatus_.usType = thrift::UpgradeStatusType::FLASH_FAILED;
    upgradeStatus_.reason = "Error flashing node with new image";
    sendUpgradeStatusAndLogEvent(
        thrift::EventId::UPGRADE_PREPARE, thrift::EventLevel::ERROR);
    return;
  }

  upgradeStatus_.usType = thrift::UpgradeStatusType::FLASHED;
  upgradeStatus_.reason = "";
  sendUpgradeStatus();

  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_PREPARE,
      thrift::EventLevel::INFO,
      "Image flashed",
      upgradeStatus_);

  // Cache current upgrade state
  upgradeStateCache_.save(upgradeStatus_);
}

void
UpgradeApp::resetStatus(const thrift::UpgradeReq& upgradeReq) {
  LOG(INFO) << "Resetting upgrade status...";
  if (prepareTorrentTimer_->isScheduled()) {
    prepareTorrentTimer_->cancelTimeout();
  }
  resetPrepareTorrentState();
  if (scheduleCommitTimeout_->isScheduled()) {
    scheduleCommitTimeout_->cancelTimeout();
  }
  std::string prevReq = upgradeStatus_.upgradeReqId;
  upgradeStatus_.usType = thrift::UpgradeStatusType::NONE;
  upgradeStatus_.nextImage.md5 = "";
  upgradeStatus_.nextImage.version = "";
  upgradeStatus_.upgradeReqId = upgradeReq.upgradeReqId;
  upgradeStatus_.reason = "";
  upgradeStatus_.whenToCommit = 0;

  sendUpgradeStatus();
}

void
UpgradeApp::resetPrepareTorrentState() {
  if (prepareTorrentState_) {
    prepareTorrentState_->ltSession->abort();
    prepareTorrentState_.reset();
  }
}

void
UpgradeApp::sendUpgradeStatus() {
  sendToMinionApp(
      E2EConsts::kStatusAppMinionId,
      thrift::MessageType::SET_UPGRADE_STATUS,
      upgradeStatus_);
}

void
UpgradeApp::sendUpgradeStatusAndLogEvent(
    const thrift::EventId eventId, const thrift::EventLevel level) {
  sendUpgradeStatus();
  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      eventId,
      level,
      upgradeStatus_.reason,
      upgradeStatus_);
}

void
UpgradeApp::sendPrepareDownloadFailure(const std::string& reason) {
  upgradeStatus_.usType = thrift::UpgradeStatusType::DOWNLOAD_FAILED;
  upgradeStatus_.reason = !reason.empty() ? reason : "Download failed";
  sendUpgradeStatusAndLogEvent(
      thrift::EventId::UPGRADE_PREPARE, thrift::EventLevel::ERROR);
}

bool
UpgradeApp::downloadHttp(
    const folly::Uri& imageUrl,
    const std::string& localDir,
    const thrift::UpgradeReq& upgradeReq) {
  // -- https check --
  if (useHttps_ && imageUrl.scheme() == "http") {
    sendPrepareDownloadFailure("Server allows only HTTPS sessions");
    return false;
  }

  // get image name from uri and construct minion local path
  std::vector<std::string> imageRemotePathParts;
  folly::split("/", imageUrl.path(), imageRemotePathParts);
  minionImageLocalPath_ = localDir + imageRemotePathParts.back();

  int downloadAttempts = kDownloadAttemptDefault;
  if (upgradeReq.downloadAttempts_ref().has_value()) {
    if (upgradeReq.downloadAttempts_ref().value() >= kDownloadAttemptLowerBound
        && upgradeReq.downloadAttempts_ref().value() <=
        kDownloadAttemptUpperBound) {
      downloadAttempts = upgradeReq.downloadAttempts_ref().value();
    } else {
      LOG(ERROR) << "Got invalid download attempts: "
                 << upgradeReq.downloadAttempts_ref().value()
                 << " (valid range: " << kDownloadAttemptLowerBound << "-"
                 << kDownloadAttemptUpperBound << "), using default "
                 << kDownloadAttemptDefault << ".";
    }
  }

  // Try to download image
  std::string err;
  for (int i = 0; i < downloadAttempts; i++) {
    LOG(INFO) << "Start downloading " << upgradeReq.imageUrl;
    err = "";
    if (CurlUtil::download(upgradeReq.imageUrl, minionImageLocalPath_, err)) {
      LOG(INFO) << "Successfully downloaded " << upgradeReq.imageUrl << " to "
                << minionImageLocalPath_;
      prepareProcessImage();
      return true;
    } else {
      LOG(ERROR) << err;
    }
  }

  sendPrepareDownloadFailure(err);
  return false;
}

bool
UpgradeApp::downloadTorrent(
    const folly::Uri& imageUrl,
    const std::string& localDir,
    const thrift::UpgradeReq& upgradeReq) {
  // Validity checks on some config parameters
  auto torrentConfig = upgradeReq.torrentParams_ref();
  if (!torrentConfig.has_value()) {
    sendPrepareDownloadFailure("Torrent config missing");
    return false;
  }
  int downloadTimeout = torrentConfig->downloadTimeout;
  int downloadLimit =
      torrentConfig->downloadLimit_ref().value_or(kDefaultDownloadLimit);
  int uploadLimit =
      torrentConfig->uploadLimit_ref().value_or(kDefaultUploadLimit);
  int maxConnections =
      torrentConfig->maxConnections_ref().value_or(kDefaultMaxConnections);
  if (downloadTimeout <= 0) {
    LOG(ERROR) << "Invalid value for downloadTimeout. Using default.";
    downloadTimeout = kDefaultDownloadTimeout;
  } else if (downloadLimit != -1 && downloadLimit <= 0) {
    LOG(ERROR) << "Invalid value for downloadLimit. Using default.";
    downloadLimit = kDefaultDownloadLimit;
  } else if (uploadLimit != -1 && uploadLimit <= 0) {
    LOG(ERROR) << "Invalid value for uploadLimit. Using default.";
    uploadLimit = kDefaultUploadLimit;
  } else if (maxConnections != -1 && maxConnections < 2) {
    LOG(ERROR) << "Invalid value for maxConnections. Using default.";
    maxConnections = kDefaultMaxConnections;
  }

  lt::settings_pack pack;

  // Set alert mask
  pack.set_int(lt::settings_pack::alert_mask, FLAGS_bt_alert_mask);
  // Connect to all trackers specified
  pack.set_bool(lt::settings_pack::announce_to_all_trackers, true);
  // Don't use public DHT (Distributed Hash Table) - limit to trackers specified
  pack.set_bool(lt::settings_pack::enable_dht, false);
  // Don't use UPnP/NAT-PMP (used for NAT port forwarding on consumer routers)
  pack.set_bool(lt::settings_pack::enable_upnp, false);
  pack.set_bool(lt::settings_pack::enable_natpmp, false);

  // Set listen interfaces
  std::string listenInterfaces = folly::sformat(
      "0.0.0.0:{},[::]:{}", FLAGS_bt_seeder_port, FLAGS_bt_seeder_port);
  std::string ipv6Address =
      IpUtil::getIpv6GlobalAddress(ipv6GlobalAddressableIfname_);
  if (!ipv6Address.empty()) {
    // Make sure a global IP is being used (??)
    // In libtorrent >= v1.2.4, the defaults sometimes only pick local IP addrs
    listenInterfaces +=
        folly::sformat(",[{}]:{}", ipv6Address, FLAGS_bt_seeder_port);
  }
  VLOG(2) << "Listening on \"" << listenInterfaces
          << "\" for torrent connections.";
  pack.set_str(libtorrent::settings_pack::listen_interfaces, listenInterfaces);

  // Use upgrade request parameters if present/valid
  int64_t peerTos =
      torrentConfig->peerTos_ref().value_or(E2EConsts::kTorrentTos);
  if (peerTos < 0 || peerTos > 255) {
    LOG(ERROR) << "Ignoring invalid value for peerTos: " << peerTos;
  } else {
    pack.set_int(lt::settings_pack::peer_tos, peerTos);
  }
  if (torrentConfig->maxFailCount_ref().has_value()) {
    int64_t val = torrentConfig->maxFailCount_ref().value();
    if (val <= 0) {
      LOG(ERROR) << "Ignoring invalid value for maxFailCount: " << val;
    } else {
      pack.set_int(lt::settings_pack::max_failcount, val);
    }
  }
  if (torrentConfig->minReconnectTimeSec_ref().has_value()) {
    int64_t val = torrentConfig->minReconnectTimeSec_ref().value();
    if (val <= 0) {
      LOG(ERROR) << "Ignoring invalid value for minReconnectTimeSec: " << val;
    } else {
      pack.set_int(lt::settings_pack::min_reconnect_time, val);
    }
  }
  if (torrentConfig->peerConnectTimeoutSec_ref().has_value()) {
    int64_t val = torrentConfig->peerConnectTimeoutSec_ref().value();
    if (val <= 0) {
      LOG(ERROR) << "Ignoring invalid value for peerConnectTimeoutSec: " << val;
    } else {
      pack.set_int(lt::settings_pack::peer_connect_timeout, val);
    }
  }
  if (torrentConfig->handshakeTimeoutSec_ref().has_value()) {
    int64_t val = torrentConfig->handshakeTimeoutSec_ref().value();
    if (val <= 0) {
      LOG(ERROR) << "Ignoring invalid value for handshakeTimeoutSec (" << val;
    } else {
      pack.set_int(lt::settings_pack::handshake_timeout, val);
    }
  }

  // Set torrent parameters
  lt::add_torrent_params atp = lt::parse_magnet_uri(imageUrl.str());
  atp.save_path = localDir;
  atp.download_limit = downloadLimit;
  atp.upload_limit = uploadLimit;
  atp.max_connections = maxConnections;

  // Start downloading
  LOG(INFO) << "Starting torrent download using trackers: "
            << folly::join(", ", atp.trackers);
  auto session = std::make_shared<lt::session>(pack);
  lt::torrent_handle handle = session->add_torrent(atp);
  std::chrono::steady_clock::time_point endTs =
      std::chrono::steady_clock::now() + std::chrono::seconds(downloadTimeout);
  prepareTorrentState_ = {session, handle, localDir, endTs};

  // Track torrent status
  if (prepareTorrentTimer_->isScheduled()) {
    prepareTorrentTimer_->cancelTimeout();
  }
  downloadTorrentUpdate();
  return true;
}

bool
UpgradeApp::downloadTorrentUpdate() {
  if (!prepareTorrentState_) {
    return false;
  }

  // Check current download/seed progress
  lt::torrent_status status = prepareTorrentState_->ltHandle.status();
  if (std::chrono::steady_clock::now() < prepareTorrentState_->endTs) {
    if (status.is_seeding && status.num_peers == 0) {
      // No further seeding required
    } else {
      // Check for alerts
      std::vector<lt::alert*> alerts;
      prepareTorrentState_->ltSession->pop_alerts(&alerts);
      for (const auto& alert : alerts) {
        VLOG(2) << alert->what() << ": " << alert->message();
        if (lt::alert_cast<lt::torrent_error_alert>(alert)) {
          sendPrepareDownloadFailure(alert->message());
          resetPrepareTorrentState();
          return false;
        }
      }

      // Log current torrent status
      std::string downloadStatus;
      switch (status.state) {
        case lt::torrent_status::checking_files:
          downloadStatus = "Checking: ";
          break;
        case lt::torrent_status::downloading_metadata:
          downloadStatus = "Downloading metadata: ";
          break;
        case lt::torrent_status::downloading:
          downloadStatus = "Downloading: ";
          break;
        case lt::torrent_status::finished:
          downloadStatus = "Finished: ";
          break;
        case lt::torrent_status::seeding:
          downloadStatus = "Seeding: ";
          break;
        default:
          downloadStatus = "<>: ";
          break;
      }

      downloadStatus += folly::sformat(
          "{:.2f}% "
          "(down: {:.1f} kB/s, up: {:.1f} kB/s, seeds: {:d}, peers: {:d})",
          status.progress * 100,
          (float)status.download_rate / 1000,
          (float)status.upload_rate / 1000,
          status.num_seeds,
          status.num_peers);
      VLOG(2) << downloadStatus;

      upgradeStatus_.usType = thrift::UpgradeStatusType::DOWNLOADING_IMAGE;
      upgradeStatus_.reason = downloadStatus;
      sendUpgradeStatus();

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

      // Check peer status
      if (status.num_peers > 0) {
        std::vector<libtorrent::peer_info> allPeerInfo{};
        prepareTorrentState_->ltHandle.get_peer_info(allPeerInfo);
        for (const auto& peerInfo : allPeerInfo) {
          VLOG(2) << folly::sformat(
              "Peer status for \"{}\", client id: \"{}\", local addr: \"{}\", "
              "total down: {:.1f} kB, up: {:.1f} kB",
              boost::lexical_cast<std::string>(peerInfo.ip),
              peerInfo.client,
              boost::lexical_cast<std::string>(peerInfo.local_endpoint),
              (float)peerInfo.total_download / 1000,
              (float)peerInfo.total_upload / 1000);
        }
      }

      // Check again later
      prepareTorrentTimer_->scheduleTimeout(kPrepareTorrentUpdateInterval);
      return false;
    }
  }

  if (!status.is_finished) {
    LOG(ERROR) << "Download timed out";
    sendPrepareDownloadFailure("Download timed out");
    resetPrepareTorrentState();
    return false;
  }

  // Get file name
  if (std::shared_ptr<const lt::torrent_info> info =
          prepareTorrentState_->ltHandle.torrent_file()) {
    auto /* lt::file_storage */ fileStorage = info->files();
    if (fileStorage.num_files() != 1) {
      sendPrepareDownloadFailure("Torrent contents are invalid");
      resetPrepareTorrentState();
      return false;
    } else {
      std::string fileName(fileStorage.file_name(0));
      minionImageLocalPath_ = prepareTorrentState_->localDir + fileName;
    }
  } else {
    sendPrepareDownloadFailure("Torrent download failed");
    resetPrepareTorrentState();
    return false;
  }
  resetPrepareTorrentState();

  // Fix file permission
  if (chmod(minionImageLocalPath_.c_str(), ACCESSPERMS) < 0) {
    LOG(ERROR) << "Error chmod " << minionImageLocalPath_;
    sendPrepareDownloadFailure("Chmod failed on image");
    return false;
  }
  VLOG(2) << "Successfully chmod on " << minionImageLocalPath_;

  prepareProcessImage();
  return true;
}

std::optional<thrift::ImageMeta>
UpgradeApp::getMetaInfo() {
  // -- run image.bin -m and pipe stdout --
  auto getMetaCmd = minionImageLocalPath_ + " -m";
  FILE* fd = popen(getMetaCmd.c_str(), "r");
  if (!fd) {
    LOG(ERROR) << "popen for `" << getMetaCmd << "` failed";
    return std::nullopt;
  }
  SCOPE_EXIT { pclose(fd); };

  // -- read meta information --
  std::string contents;
  if (!folly::readFile(fileno(fd), contents)) {
    LOG(ERROR) << "Unable to read meta info";
    return std::nullopt;
  }

  // -- parse meta information --
  thrift::ImageMeta downloadedMeta;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.deserialize(contents, downloadedMeta);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Unable to pass meta info into thrift";
    return std::nullopt;
  }

  // Verify the image
  try {
    UpgradeUtils::verifyImage(minionImageLocalPath_, downloadedMeta.md5);
  } catch (std::exception& e) {
    LOG(ERROR) << folly::exceptionStr(e);
    return std::nullopt;
  }

  return downloadedMeta;
}

bool
UpgradeApp::flashMinionImage() {
  LOG(INFO) << "Flashing minion image.";
  auto flashCmd = minionImageLocalPath_ + " -w >/dev/null";
  const bool ok = SysUtils::system(flashCmd, kOk);
  if (ok) {
    LOG(INFO) << "Successfully flashed minion image.";
  }
  return ok;
}

void
UpgradeApp::commitUpgrade() {
  if (upgradeStatus_.usType != thrift::UpgradeStatusType::FLASHED) {
    upgradeStatus_.usType = thrift::UpgradeStatusType::COMMIT_FAILED;
    upgradeStatus_.reason = "Not yet ready to commit upgrade";
    sendUpgradeStatusAndLogEvent(
        thrift::EventId::UPGRADE_COMMIT, thrift::EventLevel::ERROR);
    return;
  }

  LOG(INFO) << "Upgrade commit: set testcode request in u-boot environment";
  if (!SysUtils::system(kSetTestcodeRequestCmd, kOk)) {
    upgradeStatus_.usType = thrift::UpgradeStatusType::COMMIT_FAILED;
    upgradeStatus_.reason = "Unable to set testcode request in u-boot env";
    sendUpgradeStatusAndLogEvent(
        thrift::EventId::UPGRADE_COMMIT, thrift::EventLevel::ERROR);
    return;
  }

  // Tell watchdog to not revert unverified config (if any) on the next startup.
  LOG(INFO) << "Upgrade commit: prevent config fallback on reboot";
  if (!WatchdogUtils::preventConfigFallbackOnReboot()) {
    LOG(ERROR) << "Failed to prevent config fallback on reboot in watchdog";
  }

  LOG(INFO) << "Upgrade commit: rebooting";
  if (!SysUtils::system("reboot", kOk)) {
    // NB: Would be nice to undo the testcode here, but trying to
    // rewrite the u-boot env when we are in a strange state is not safe.
    LOG(FATAL) << "reboot failed";
  }
}

bool
UpgradeApp::savePreloadedConfig(
    const std::string& version,
    const std::string& preloadedJson) {
  if (preloadedJson.length() == 0) {
    LOG(ERROR) << "Config JSON is empty!";
    return false;
  }
  try {
    JsonUtils::writeString2JsonFile(preloadedJson, kPreloadedConfigFile);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Can't write " << kPreloadedConfigFile
               << " due to: " << folly::exceptionStr(ex);
    return false;
  }
  const std::string versionWithNewline = version + "\n";
  if (!folly::writeFile(versionWithNewline, kPreloadedConfigVersion.c_str())) {
    LOG(ERROR) << "Failed to write new version to " << kPreloadedConfigVersion;
    return false;
  }
  return true;
}

void
UpgradeApp::scheduleCommitUpgrade(const thrift::UpgradeReq& upgradeReq) {
  if (upgradeStatus_.usType != thrift::UpgradeStatusType::FLASHED) {
    upgradeStatus_.usType = thrift::UpgradeStatusType::COMMIT_FAILED;
    upgradeStatus_.reason = "Not yet ready to commit upgrade";
    sendUpgradeStatusAndLogEvent(
        thrift::EventId::UPGRADE_COMMIT, thrift::EventLevel::ERROR);
    return;
  }
  if (upgradeReq.nextNodeConfigJson_ref().has_value()) {
    savePreloadedConfig(
        upgradeStatus_.nextImage.version,
        upgradeReq.nextNodeConfigJson_ref().value());
  }

  upgradeStatus_.whenToCommit =
      std::time(nullptr) + upgradeReq.scheduleToCommit_ref().value_or(0);
  sendUpgradeStatus();
  eventClient_->logEventThrift(
      thrift::EventCategory::UPGRADE,
      thrift::EventId::UPGRADE_COMMIT,
      thrift::EventLevel::INFO,
      "Scheduling COMMIT",
      upgradeReq);

  scheduleCommitTimeout_->scheduleTimeout(
      std::chrono::seconds(upgradeReq.scheduleToCommit_ref().value_or(0)),
      false /* isPeriodic */);
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
