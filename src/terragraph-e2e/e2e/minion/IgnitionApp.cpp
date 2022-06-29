/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "IgnitionApp.h"

#include <paths.h>
#include <stdexcept>
#include <sys/wait.h>
#include <unistd.h>

#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/Random.h>
#include <folly/String.h>
#include <gflags/gflags.h>

#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/IpUtil.h"
#include "e2e/common/TimeUtils.h"

using namespace fbzmq;

using apache::thrift::detail::TEnumMapFactory;

// distributed ignition parameters
DEFINE_int32(
    distributed_ignition_cooldown_duration_ms,
    5000,
    "The amount of time to wait after each distributed link ignition attempt "
    "before igniting the next link or retrying");
DEFINE_int32(
    distributed_ignition_max_offset_ms,
    1000,
    "The maximum number of milliseconds on either side of the time boundary "
    "where an ignition attempt can still occur");
DEFINE_int32(
    distributed_ignition_gps_clock_poll_ms,
    1000,
    "The polling interval used to check if the GPS clock has been enabled");
DEFINE_int32(
    distributed_ignition_attempts_before_backoff,
    3,
    "The number of distributed ignition attempts to a responder before "
    "beginning backoff");
DEFINE_int32(
    distributed_ignition_max_attempts,
    18,
    "The maximum number of distributed ignition attempts to a responder before "
    "giving up");

// wsec-related flags
DEFINE_string(
    wpa_supplicant_global_ctrl_iface,
    "/var/run/wpa_supplicant-global",
    "The global wpa_supplicant control interface (socket)");
DEFINE_string(
    wpa_supplicant_log_file,
    "/var/log/wpa_supplicant.log",
    "The global wpa_supplicant log file path (to be created");

// continuous topology scan parameters
DEFINE_int32(
    continuous_topo_scan_start_time_offset_s,
    4,
    "Delay (in seconds) before starting the first topology scan when running "
    "continuous topology scans");
DEFINE_int32(
    continuous_topo_scan_bwgd_delta,
    0,
    "The BWGD difference between consecutive topology scans. If this is unset "
    "(or zero), the delta is computed from firmware parameters.");
DEFINE_int32(
    wsec_stop_delay_ms,
    200,
    "How long to sleep (in ms) after stopping wpa_supplicant or hostapd. "
    "Reusing the interface immediately after the process dies will fail; "
    "adjust this value as needed on your platform.");
DEFINE_int32(
    wsec_cfg_delay_ms,
    250,
    "How long to sleep (in ms) after starting wpa_supplicant or hostapd, "
    "before we start configuring EAPoL secrets. Both of these programs take "
    "some time before becoming responsive to the command-line API.");

// LINK_PAUSE parameters
DEFINE_int32(
    link_pause_dissoc_delay_s,
    20 * 60,
    "Delay (in seconds) before bringing down a link stuck in LINK_PAUSE "
    "state.");

namespace {
// see gflag descriptions
const std::chrono::milliseconds kStopCmdSleepTime{FLAGS_wsec_stop_delay_ms};
const std::chrono::milliseconds kCfgStartCmdSleepTime{FLAGS_wsec_cfg_delay_ms};

// How long to delay BF_RESP_SCAN_DISABLE command to firmware.
//
// This delay is to avoid sending the command after LINK_DOWN but before FW
// has actually removed the station entry and entered BF responder mode.
// For failure reasons [TG_HB_KA_LOSS_DETECTED, TG_LINK_SHUTDOWN_RECVD,
// TG_DISASSOC_RCVD_FROM_PEER], FW sends TG_NB_DEL_LINK_REQ to the driver and
// awaits the TG_SB_DEL_LINK_RESP reply before deleting links, which typically
// takes 30~600ms, with a hardcoded 3s timeout (at which the link is deleted).
const int kBfRespScanDisableDelaySec{3};

// Returns the wpa_supplicant/hostapd CLI flags as a string for the given
// verbosity bitmask.
// 0 <default>
// 1 -d   show more debug messages (exclusive with -dd)
// 2 -dd  show even more debug messages (exclusive with -d)
// 4 -t   include timestamp in debug messages
// 8 -K   include keys (passwords, etc.) in debug output
std::string
verbosityToFlags(int verbosity) {
  std::vector<std::string> flags;

  // Can't use both -d and -dd: if both are given, -dd will take precedence.
  if (verbosity & 2) {
    flags.push_back("-dd");
  } else if (verbosity & 1) {
    flags.push_back("-d");
  }
  if (verbosity & 4) {
    flags.push_back("-t");
  }
  if (verbosity & 8) {
    flags.push_back("-K");
  }

  return folly::join(" ", flags);
}

// Command to start the global wpa_supplicant process
std::string
startGlobalSupplicantCmd(int verbosity) {
  // NOTE: We are not running a version that supports the "-f logfile" argument
  return folly::sformat(
      "wpa_supplicant {} -g {} 2>&1 >> {}",
      verbosityToFlags(verbosity),
      FLAGS_wpa_supplicant_global_ctrl_iface,
      FLAGS_wpa_supplicant_log_file);
}

// Command to stop the global wpa_supplicant process
std::string
stopGlobalSupplicantCmd() {
  return folly::sformat(
      "wpa_cli -g {} terminate", FLAGS_wpa_supplicant_global_ctrl_iface);
}

// Command to add an interface to the global wpa_supplicant process
std::string
startSupplicantCmd(const std::string& ifname) {
  return folly::sformat(
      "wpa_cli -g {} interface_add {} "
      "/var/run/wpa_supplicant/wpa_supplicant_{}.conf",
      FLAGS_wpa_supplicant_global_ctrl_iface,
      ifname,
      ifname);
}

// Command to configure wpa_supplicant on an interface
std::string
configSupplicantCmd(const std::string& ifname, const std::string& key,
      const std::string& value) {
  return folly::sformat(
      "wpa_cli -p /var/run/wpa_supplicant_{} -i {} SET_NETWORK 0 {} '\"{}\"'",
      ifname,
      ifname,
      key,
      value);
}

// Command to remove an interface from the global wpa_supplicant process
std::string
stopSupplicantCmd(const std::string& ifname) {
  return folly::sformat(
      "wpa_cli -g {} interface_remove {}",
      FLAGS_wpa_supplicant_global_ctrl_iface,
      ifname);
}

// Command to start hostapd on an interface
std::string
startHostapdCmd(const std::string& ifname, int verbosity) {
  return folly::sformat(
      "hostapd {} -B -P /var/run/hostapd/hostapd_{}.pid "
      "/var/run/hostapd/hostapd_{}.conf &>> /tmp/hostapd_{}",
      verbosityToFlags(verbosity),
      ifname,
      ifname,
      ifname);
}

// Command to configure hostapd on an interface
std::string
configHostapdCmd(const std::string& ifname, const std::string& key,
      const std::string& value) {
  return folly::sformat(
      "hostapd_cli -p /var/run/hostapd_{} -i {} set {} \"{}\"",
      ifname,
      ifname,
      key,
      value);
}

// Command to stop hostapd on an interface
std::string
stopHostapdCmd(const std::string& ifname) {
  return folly::sformat("/usr/sbin/hostapd_stop.sh {}", ifname);
}

} // namespace

namespace facebook {
namespace terragraph {
namespace minion {

IgnitionApp::IgnitionApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr,
    const std::chrono::seconds linkupRespWaitTimeout,
    int64_t wsecEnable)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kIgnitionAppMinionId),
      linkupRespWaitTimeout_(linkupRespWaitTimeout),
      wsecEnable_{wsecEnable} {

  // get list of terra interfaces on the device
  terraIfList_ = IpUtil::getTerraInterfaces();

  // prepare clearCurrRespMacTimer_
  clearCurrRespMacTimer_ = ZmqTimeout::make(this, [this]() noexcept {
    VLOG(2) << "Current ignition procedure timed out!";

    // if hostapd has been started, kill it and restart wpa_supplicant
    if (!currResponderHostapdIface_.empty()) {
      linkDownIfaceQueue_.insert(currResponderHostapdIface_);
    }

    resetCurrentIgnitionState();
  });

  // Prepare distributedIgnitionTimer_. This function will be called
  // when the minion attempts to self-ignite a link.
  distributedIgnitionTimer_ = ZmqTimeout::make(this, [this]() noexcept {
    selfIgnite();
  });

  // Don't attempt self-ignition if disabled in config.
  auto sysParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getSysParams();
  distributedIgnitionEnabled_ = sysParams->distributedIgnitionEnabled;

  // Read neighbor info from config to determine candidate links.
  auto topologyInfo =
      SharedObjects::getNodeConfigWrapper()->rlock()->getTopologyInfo();
  // Disable distributed ignition if any required information is missing from
  // config.
  if (!topologyInfo ||
      !topologyInfo->neighborInfo_ref().has_value() ||
      !topologyInfo->distributedIgnitionNumColors_ref().has_value() ||
      !topologyInfo->radioInfo_ref().has_value()) {
    distributedIgnitionEnabled_ = false;
  }
  topologyInfo_ = *topologyInfo;

  if (distributedIgnitionEnabled_) {
    // Populate candidate links.
    for (const auto& kv : topologyInfo_.neighborInfo_ref().value()) {
      std::string responderMac = kv.first;
      thrift::NeighborInfo neighborInfo = kv.second;

      // Ignore any link candidates with missing information.
      if (!neighborInfo.controlSuperframe_ref().has_value() ||
          !neighborInfo.nodeType_ref().has_value() ||
          !neighborInfo.golayIdx_ref().has_value() ||
          !neighborInfo.nodePolarity_ref().has_value()) {
        continue;
      }

      thrift::SetLinkStatus setLinkStatus;
      setLinkStatus.linkStatusType = thrift::LinkStatusType::LINK_UP;
      setLinkStatus.initiatorMac = neighborInfo.initiatorMac_ref().value();
      setLinkStatus.responderMac = responderMac;
      setLinkStatus.controlSuperframe_ref() =
          neighborInfo.controlSuperframe_ref().value();
      setLinkStatus.responderNodeType_ref() =
          neighborInfo.nodeType_ref().value();
      setLinkStatus.golayIdx_ref() = neighborInfo.golayIdx_ref().value();
      setLinkStatus.responderNodePolarity_ref() =
          neighborInfo.nodePolarity_ref().value();

      linkCandidates_.push_back(setLinkStatus);
    }

    LOG(INFO) << "Distributed ignition is enabled with "
              << linkCandidates_.size() << " configured neighbors";
  } else {
    LOG(INFO) << "Distributed ignition is disabled";
  }

  // check wsec config
  if (isWsecEnabled()) {
    // start the global wpa_supplicant instance, then add all interfaces
    // HACK: sleep for a few seconds to wait for global interface to initialize
    forkProcess(
        startGlobalSupplicantCmd(getWsecParams().supplicantVerbosity),
        false);
    scheduleTimeout(std::chrono::seconds(2), [&]() noexcept {
      VLOG(2) << "Adding wpa_supplicant interfaces...";
      std::unordered_set<std::string> ifaces(
          terraIfList_.begin(), terraIfList_.end());
      startSupplicants(ifaces, false);
    });
  } else {
    // kill all supplicants (to be safe)
    // (assume hostapd has been killed in e2e_minion_wrapper.sh startup script)
    forkProcess(stopGlobalSupplicantCmd(), false);
  }
}

IgnitionApp::~IgnitionApp() {
  // kill all supplicants and hostapds
  // (do this even if wsec is disabled, to be safe)
  forkProcess(stopGlobalSupplicantCmd(), false);
  for (const auto& ifname : terraIfList_) {
    forkProcess(stopHostapdCmd(ifname), false);
  }
}

void
IgnitionApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::SET_LINK_STATUS:
      processSetLinkStatus(senderApp, message);
      break;
    case thrift::MessageType::DR_LINK_STATUS:
      processDriverLinkStatus(senderApp, message);
      break;
    case thrift::MessageType::GET_LINK_STATUS:
      processGetLinkStatus(senderApp, message);
      break;
    case thrift::MessageType::GET_LINK_STATUS_DUMP:
      processGetLinkStatusDump(senderApp, message);
      break;
    case thrift::MessageType::BF_RESP_SCAN:
      processBfRespScan(senderApp, message);
      break;
    case thrift::MessageType::BF_RESP_SCAN_DISABLE:
      processBfRespScanDisable(senderApp, message);
      break;
    case thrift::MessageType::DR_DEV_ALLOC_RES:
      processDriverDevAllocRes(senderApp, message);
      break;
    case thrift::MessageType::DR_WSEC_STATUS:
      processDriverWsecStatus(senderApp, message);
      break;
    case thrift::MessageType::DR_WSEC_LINKUP_STATUS:
      processDriverWsecLinkupStatus(senderApp, message);
      break;
    case thrift::MessageType::DR_DEV_UPDOWN_STATUS:
      processDevUpDownStatus(senderApp, message);
      break;
    case thrift::MessageType::BF_SLOT_EXCLUSION_REQ:
      processBfSlotExclusionReq(senderApp, message);
      break;
    case thrift::MessageType::START_CONTINUOUS_TOPO_SCAN:
      processStartContinuousTopoScan(senderApp, message);
      break;
    case thrift::MessageType::GPS_ENABLED:
      processGpsEnabled(senderApp);
      break;
    case thrift::MessageType::CONTROLLER_CONNECTED:
      processControllerConnected(senderApp);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::
                     makeValuesToNamesMap(), message.mType, "UNKNOWN")
          << ") received from " << senderApp;
  }
}

inline bool
IgnitionApp::isWsecEnabled() const {
  return (wsecEnable_ != 0);
}

inline bool
IgnitionApp::isWpaPskEnabled() const {
  return (wsecEnable_ == 1);
}

inline bool
IgnitionApp::isEapolEnabled() const {
  return (wsecEnable_ == 2);
}

void
IgnitionApp::processBfSlotExclusionReq(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto bfSlotExclusionReq =
        maybeReadThrift<thrift::BfSlotExclusionReq>(message);
  if (!bfSlotExclusionReq) {
    handleInvalidMessage("BfSlotExclusionReq", senderApp);
    return;
  }

  LOG(INFO) << "Received BfSlotExclusionReq message from " << senderApp
            << " for MAC " << bfSlotExclusionReq->radioMac;
  sendToDriverApp(
      bfSlotExclusionReq->radioMac,
      thrift::MessageType::BF_SLOT_EXCLUSION_REQ,
      bfSlotExclusionReq.value());
}

void
IgnitionApp::processStartContinuousTopoScan(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto req = maybeReadThrift<thrift::StartContinuousTopoScan>(message);
  if (!req) {
    handleInvalidMessage("StartContinuousTopoScan", senderApp);
    return;
  }
  const std::string radioMac = req->radioMac;

  LOG(INFO) << "Received StartContinuousTopoScan message from " << senderApp
            << " for MAC " << radioMac << " over " << req->durationSec
            << " second(s)";

  // Cancel any existing timer
  auto iter = continuousTopoScanState_.find(radioMac);
  if (iter != continuousTopoScanState_.end()) {
    if (iter->second.timer && iter->second.timer->isScheduled()) {
      LOG(INFO) << "Stopping existing continuous topology scan timer for MAC "
                << radioMac;
      iter->second.timer->cancelTimeout();
    }
  }
  if (req->durationSec < 1) {
    // Only stop any existing scans, don't start another
    continuousTopoScanState_.erase(radioMac);
    return;
  }

  // Compute the topo scan interval
  int bwgdDelta;
  if (FLAGS_continuous_topo_scan_bwgd_delta > 0) {
    bwgdDelta = FLAGS_continuous_topo_scan_bwgd_delta;
    VLOG(5) << "processStartContinuousTopoScan(): bwgdDelta=" << bwgdDelta
            << "(" << (bwgdDelta * 25.6) << "ms)";
  } else {
    const std::string fwVersion = *SharedObjects::getFirmwareVersion()->rlock();
    const int ibfNumberOfBeams = SharedObjects::getNodeConfigWrapper()->rlock()
        ->getRadioParams(radioMac).fwParams.ibfNumberOfBeams_ref().value_or(31);
    const int topoNumSweep = fwVersion == "10.11.0.28" ? 4 : 7;
    bwgdDelta = 11 + ibfNumberOfBeams * ibfNumberOfBeams * topoNumSweep / 64;
    VLOG(5) << "processStartContinuousTopoScan(): ibfNumberOfBeams="
            << ibfNumberOfBeams << ", topoNumSweep=" << topoNumSweep
            << ", bwgdDelta=" << bwgdDelta << "(" << (bwgdDelta * 25.6)
            << "ms)";
  }

  // Initialize topo scan loop state
  ContinuousTopoScanState state;
  state.bwgdDelta = static_cast<uint64_t>(bwgdDelta);
  state.endTs =
      std::chrono::steady_clock::now() + std::chrono::seconds(req->durationSec);
  state.nextScanReqBwgd = TimeUtils::currentBwgdIdx(
      std::chrono::seconds(FLAGS_continuous_topo_scan_start_time_offset_s));
  state.timer = ZmqTimeout::make(this, [&, radioMac]() noexcept {
    continuousTopoScanStep(radioMac);
  });
  state.timer->scheduleTimeout(std::chrono::milliseconds::zero());
  continuousTopoScanState_[radioMac] = std::move(state);
}

void
IgnitionApp::continuousTopoScanStep(const std::string& radioMac) noexcept {
  auto iter = continuousTopoScanState_.find(radioMac);
  if (iter == continuousTopoScanState_.end()) {
    LOG(ERROR) << "No continuous topology scan state found for MAC: "
                << radioMac;
    return;
  }
  auto& state = iter->second;

  // Correct for scheduling errors
  uint64_t currentBwgdIdx =
      TimeUtils::currentBwgdIdx(std::chrono::seconds::zero());
  VLOG(5) << "continuousTopoScanStep(): Running step with currentBwgdIdx="
          << currentBwgdIdx << ", nextScanReqBwgd=" << state.nextScanReqBwgd
          << " (delta=" << (state.nextScanReqBwgd - currentBwgdIdx) << ")";
  if (currentBwgdIdx >= state.nextScanReqBwgd) {
    LOG(ERROR) << "Missed next scheduled BWGD index for continuous topology "
               << "scans for MAC " << radioMac << " (scheduled="
               << state.nextScanReqBwgd << ", current=" << currentBwgdIdx
               << ")";
    state.nextScanReqBwgd = TimeUtils::currentBwgdIdx(
        std::chrono::seconds(FLAGS_continuous_topo_scan_start_time_offset_s));
  }

  LOG(INFO) << "<" << radioMac
            << "> Scheduling continuous topology scan at BWGD index: "
            << state.nextScanReqBwgd;

  // Send scan request
  thrift::ScanReq scanReq;
  scanReq.radioMac_ref() = radioMac;
  scanReq.token = time(nullptr); // arbitrary unique token
  scanReq.scanType_ref() = thrift::ScanType::TOPO;
  scanReq.startBwgdIdx = state.nextScanReqBwgd;
  scanReq.rxNodeMac_ref() = "ff:ff:ff:ff:ff:ff";
  sendToDriverApp(radioMac, thrift::MessageType::SCAN_REQ, scanReq);

  // Check if we are finished
  if (std::chrono::steady_clock::now() >= state.endTs) {
    LOG(INFO) << "Stopping continuous topology scans for MAC: " << radioMac;
    return;
  }

  // Schedule the next scan
  state.nextScanReqBwgd += state.bwgdDelta;
  const int offsetBwgd =
      FLAGS_continuous_topo_scan_start_time_offset_s * 10000 / 256;
  int delayMs = std::max(0,
      (int)(256 * (state.nextScanReqBwgd - currentBwgdIdx - offsetBwgd) / 10));
  state.timer->scheduleTimeout(std::chrono::milliseconds(delayMs));
  VLOG(5) << "continuousTopoScanStep(): Scheduled next step in " << delayMs
          << "ms";
}

void
IgnitionApp::processSetLinkStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto setLinkStatus = maybeReadThrift<thrift::SetLinkStatus>(message);
  if (!setLinkStatus) {
    handleInvalidMessage("SetLinkStatus", senderApp);
    return;
  }

  LOG(INFO) << "Received SetLinkStatus message from " << senderApp
            << " for initiatorMac `" << setLinkStatus->initiatorMac
            << "` and responderMac `" << setLinkStatus->responderMac
            << "` to: "
            << folly::get_default(
                   TEnumMapFactory<thrift::LinkStatusType>::
                       makeValuesToNamesMap(),
                   setLinkStatus->linkStatusType,
                   "UNKNOWN");

  // Check if initiatorMac is recognized as a valid WLAN MAC.
  // Accept an empty initiatorMac for backward compatibility
  if (!setLinkStatus->initiatorMac.empty() &&
      !SharedObjects::getNodeInfoWrapper()->rlock()->wlanMacExists(
          setLinkStatus->initiatorMac)) {
    LOG(ERROR) << "Initiator MAC `" << setLinkStatus->initiatorMac
               << "` is not a valid WLAN MAC";
    return;
  }

  if (setLinkStatus->linkStatusType == thrift::LinkStatusType::LINK_UP) {
    handleSetLinkUpReq(setLinkStatus.value());
  } else {
    handleSetLinkDownReq(setLinkStatus.value());
  }
}

void
IgnitionApp::handleSetLinkDownReq(const thrift::SetLinkStatus& setLinkStatus) {
  if (!SharedObjects::getIgnitedNeighbors()->rlock()
          ->count(setLinkStatus.responderMac)) {
    // the link is down, send a LINK_DOWN LinkStatus
    LOG(INFO) << "Link for neighbor " << setLinkStatus.responderMac
              << " is already down.";
    sendLinkStatus(
        setLinkStatus.responderMac,
        setLinkStatus.initiatorMac,
        thrift::LinkStatusType::LINK_DOWN,
        false);
    return;
  }

  // Log an event
  std::string msg = folly::sformat(
      "Sending dissoc request for neighbor {}", setLinkStatus.responderMac);
  eventClient_->logEventThrift(
      thrift::EventCategory::IGNITION,
      thrift::EventId::MINION_SET_LINK_STATUS,
      thrift::EventLevel::INFO,
      msg,
      setLinkStatus);

  // link is up, send a dissoc to Driver App
  thrift::DriverSetLinkStatus driverSetLinkStatus;
  driverSetLinkStatus.isAssoc = false;
  driverSetLinkStatus.responderMac = setLinkStatus.responderMac;
  driverSetLinkStatus.optParams = thrift::FwOptParams();
  sendToDriverApp(
      setLinkStatus.initiatorMac,
      thrift::MessageType::DR_SET_LINK_STATUS,
      driverSetLinkStatus);
}

void
IgnitionApp::handleSetLinkUpReq(const thrift::SetLinkStatus& setLinkStatus) {
  // Check if some neighbor is currently being ignited. Only one ignition can
  // happen at a time, so just ignore it.
  if (!currResponderMac_.empty()) {
    LOG(INFO) << "Waiting for association response from firmware for "
              << currResponderMac_;
    return;
  }

  // Check if the neighbor has already been ignited. Send a LINK_UP LinkStatus
  if (SharedObjects::getIgnitedNeighbors()->rlock()->count(
          setLinkStatus.responderMac)) {
    LOG(INFO) << "Neighbor " << setLinkStatus.responderMac
              << " is already ignited";
    sendLinkStatus(
        setLinkStatus.responderMac,
        setLinkStatus.initiatorMac,
        thrift::LinkStatusType::LINK_UP,
        false);
    return;
  }

  // Log an event
  std::string msg = folly::sformat(
      "Sending assoc request for neighbor {}", setLinkStatus.responderMac);
  eventClient_->logEventThrift(
      thrift::EventCategory::IGNITION,
      thrift::EventId::MINION_SET_LINK_STATUS,
      thrift::EventLevel::INFO,
      msg,
      setLinkStatus);

  currInitiatorMac_ = setLinkStatus.initiatorMac;
  currResponderMac_ = setLinkStatus.responderMac;

  // Send the assoc request to firmware at this point.
  // The neighbor could be up and minion does not know about it. In this case fw
  // will send a LINK_UP anyway.
  auto linkParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getLinkParams(
          currResponderMac_);

  if (setLinkStatus.responderNodeType_ref().has_value()) {
    linkParams.fwParams.respNodeType_ref() =
        static_cast<int64_t>(setLinkStatus.responderNodeType_ref().value());
  }
  if (setLinkStatus.golayIdx_ref().has_value()) {
    linkParams.fwParams.txGolayIdx_ref() =
        static_cast<int64_t>(setLinkStatus.golayIdx_ref().value().txGolayIdx);
    linkParams.fwParams.rxGolayIdx_ref() =
        static_cast<int64_t>(setLinkStatus.golayIdx_ref().value().rxGolayIdx);
  }
  if (setLinkStatus.controlSuperframe_ref().has_value()) {
    linkParams.fwParams.controlSuperframe_ref() =
        setLinkStatus.controlSuperframe_ref().value();
  }
  if (setLinkStatus.responderNodePolarity_ref().has_value()) {
    linkParams.fwParams.polarity_ref() =
        static_cast<int64_t>(setLinkStatus.responderNodePolarity_ref().value());
  }

  currResponderPendingMsg_.isAssoc = true;
  currResponderPendingMsg_.responderMac = currResponderMac_;
  currResponderPendingMsg_.optParams = linkParams.fwParams;

  if (isWsecEnabled()) {
    LOG(INFO) << "Requesting terra interface for responder MAC "
              << currResponderMac_;

    // need it to start hostapd
    thrift::DriverDevAllocReq driverDevAllocReq;
    driverDevAllocReq.macAddr = currResponderMac_;
    sendToDriverApp(
        currInitiatorMac_,
        thrift::MessageType::DR_DEV_ALLOC_REQ,
        driverDevAllocReq);
  } else {
    sendToDriverApp(
        currInitiatorMac_,
        thrift::MessageType::DR_SET_LINK_STATUS,
        currResponderPendingMsg_);
  }

  setIgnitionTimer(true);

  // If we have a pending BF_RESP_SCAN request, cancel it
  auto iter = bfRespScanTimerMap_.find(setLinkStatus.initiatorMac);
  if (iter != bfRespScanTimerMap_.end()) {
    // Cancel existing timer
    if (iter->second->isScheduled()) {
      iter->second->cancelTimeout();
    }
  }
}

void
IgnitionApp::processDriverDevAllocRes(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto drMessage = maybeReadDriverMessage<thrift::DriverDevAllocRes>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverDevAllocRes", senderApp);
    return;
  }
  thrift::DriverDevAllocRes& driverDevAllocRes = drMessage->value;

  LOG(INFO) << drMessage->macPrefix() << "Received DriverDevAllocRes message";
  if (!isWsecEnabled()) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverDevAllocRes message from " << senderApp
               << " (link encryption is disabled)";
    return;
  }
  if (senderApp != E2EConsts::kDriverAppMinionId) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverDevAllocRes message from " << senderApp
               << " (expecting " << E2EConsts::kDriverAppMinionId << ")";
    return;
  }

  // Validity checks
  if (!driverDevAllocRes.success) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Could not reserve terra interface name for "
               << currResponderMac_;
    return;
  }
  if (driverDevAllocRes.macAddr != currResponderMac_) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Received DriverDevAllocRes for MAC "
               << driverDevAllocRes.macAddr << ", but currently igniting "
               << currResponderMac_;
    return;
  }

  std::string ifname = driverDevAllocRes.ifname;

  LOG(INFO) << drMessage->macPrefix()
            << "MAC `" << driverDevAllocRes.macAddr
            << "` is assigned interface " << ifname;

  // start authenticator on this interface before sending link up
  // wait until supplicant is dead, otherwise hostapd can't be started
  auto wsecParams = getWsecParams();
  if (forkProcess(stopSupplicantCmd(ifname), true)) {
    LOG(INFO) << "Stopped supplicant on " << ifname;
    /* sleep override */ std::this_thread::sleep_for(kStopCmdSleepTime);
  }
  if (forkProcess(
          startHostapdCmd(ifname, wsecParams.hostapdVerbosity), false)) {
    LOG(INFO) << "Started authenticator on " << ifname;

    if (isEapolEnabled()) {
      // Wait for process to start CLI interface
      /* sleep override */ std::this_thread::sleep_for(kCfgStartCmdSleepTime);

      if (forkProcess(configHostapdCmd(
            ifname,
            "auth_server_shared_secret",
            getValueforSecret("radius_server_shared_secret")), true)) {
        LOG(INFO) << "Configured radius_server_shared_secret for hostapd on "
                  << ifname;
      } else {
        LOG(ERROR) << "Failed to set radius_server_shared_secret on "
                      "hostapd for interface " << ifname;
      }
    }

  } else {
    LOG(ERROR) << "Failed to start hostapd for interface " << ifname;
    return;
  }

  sendToDriverApp(
      currInitiatorMac_,
      thrift::MessageType::DR_SET_LINK_STATUS,
      currResponderPendingMsg_);

  currResponderHostapdIface_ = ifname;
  setIgnitionTimer(true);
}

void
IgnitionApp::processDriverLinkStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto drMessage = maybeReadDriverMessage<thrift::DriverLinkStatus>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverLinkStatus", senderApp);
    return;
  }
  thrift::DriverLinkStatus& drLinkStatus = drMessage->value;

  LOG(INFO) << drMessage->macPrefix()
            << "Received DriverLinkStatus from driver";

  if (!drLinkStatus.valid) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Link status notification format unexpected";
    return;
  }

  // Print link status
  LOG(INFO) << drMessage->macPrefix()
            << "Status of link to " << drLinkStatus.macAddr << " on interface "
            << drLinkStatus.ifname << " is: "
            << folly::get_default(
                   TEnumMapFactory<thrift::DriverLinkStatusType>::
                       makeValuesToNamesMap(),
                   drLinkStatus.drLinkStatusType,
                   "UNKNOWN");
  if (drLinkStatus.drLinkStatusType ==
      thrift::DriverLinkStatusType::LINK_DOWN) {
    LOG(INFO) << drMessage->macPrefix() << "LINK_DOWN cause: "
              << folly::get_default(
                     TEnumMapFactory<thrift::LinkDownCause>::
                         makeValuesToNamesMap(),
                     drLinkStatus.linkDownCause,
                     "UNKNOWN");
  }

  // Perform appropriate actions
  handleDriverLinkStatus(drLinkStatus, drMessage->radioMac);
}

void
IgnitionApp::handleDriverLinkStatus(
    const thrift::DriverLinkStatus& drLinkStatus,
    const std::string& radioMac) noexcept {
  std::string responderMac = drLinkStatus.macAddr;
  std::string ifname = drLinkStatus.ifname;
  thrift::DriverLinkStatusType drStatus = drLinkStatus.drLinkStatusType;
  bool wasNbrIgnited =
      (SharedObjects::getIgnitedNeighbors()->rlock()->count(responderMac) > 0);
  auto wsecParams = getWsecParams();

  // Notify controller of link status (ignore LINK_PAUSE, etc.)
  std::optional<thrift::LinkStatusType> reportLinkStatus;
  if (drStatus == thrift::DriverLinkStatusType::LINK_UP) {
    reportLinkStatus = thrift::LinkStatusType::LINK_UP;
  } else if (drStatus == thrift::DriverLinkStatusType::LINK_DOWN) {
    reportLinkStatus = thrift::LinkStatusType::LINK_DOWN;
  }
  if (reportLinkStatus) {
    sendLinkStatus(responderMac, radioMac, reportLinkStatus.value(), true);

    // Log an event
    std::string msg = folly::sformat(
        "Received {} for neighbor {} on interface {} ({})",
        TEnumMapFactory<thrift::DriverLinkStatusType>::
            makeValuesToNamesMap().at(drStatus),
        responderMac,
        ifname,
        radioMac);
    std::string linkEntity = folly::sformat("{}-{}", radioMac, responderMac);
    eventClient_->logEventThrift(
        thrift::EventCategory::IGNITION,
        thrift::EventId::DRIVER_LINK_STATUS,
        drStatus == thrift::DriverLinkStatusType::LINK_UP
            ? thrift::EventLevel::INFO
            : thrift::EventLevel::ERROR,
        msg,
        drLinkStatus,
        std::make_optional(linkEntity));

    // Forward this link status message to StatusApp if node types are valid
    auto nodeTypeMap =
        TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap();
    if (nodeTypeMap.count(drLinkStatus.selfNodeType) &&
        nodeTypeMap.count(drLinkStatus.peerNodeType)) {
      sendToMinionApp(
          E2EConsts::kStatusAppMinionId,
          thrift::MessageType::DR_LINK_STATUS,
          drLinkStatus);
    }
  }
  if (TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap()
      .count(drLinkStatus.selfNodeType)) {
    myNodeType_ = drLinkStatus.selfNodeType;
  }

  // Update our view of ignited neighbors
  if (drStatus == thrift::DriverLinkStatusType::LINK_UP) {
    thrift::LinkStatus linkStatus;
    linkStatus.radioMac_ref() = radioMac;
    linkStatus.ifname_ref() = ifname;
    linkStatus.responderMac = responderMac;
    linkStatus.linkStatusType = thrift::LinkStatusType::LINK_UP;

    auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->wlock();
    (*lockedIgnitedNeighbors)[responderMac] = linkStatus;
    lockedIgnitedNeighbors.unlock();  // lockedIgnitedNeighbors -> NULL

    // Reset distributed ignition cooldown in case this link goes down and
    // needs to be re-ignited.
    if (distributedIgnitionResponderState_.count(responderMac)) {
      distributedIgnitionResponderState_[responderMac].cooldownDuration.reset();
    }
  } else if (drStatus == thrift::DriverLinkStatusType::LINK_DOWN) {
    // this is necessary at initiator side to remove a previously ignited
    // neighbor, otherwise when a linkup request for this neighbor comes
    // later, it will be ignored. Nothing is removed if this neighbor is not
    // ignited previously, but that's OK
    SharedObjects::getIgnitedNeighbors()->wlock()->erase(responderMac);

    // If this was an explicit dissoc, disable distributed ignition for this
    // link permanently (if it was previously enabled).
    if (distributedIgnitionEnabled_ &&
        drLinkStatus.linkDownCause ==
            thrift::LinkDownCause::DISASSOC_RCVD_FROM_PEER) {
      for (const auto& candidate : linkCandidates_) {
        if (candidate.initiatorMac == radioMac &&
            candidate.responderMac == responderMac) {
          auto& state = distributedIgnitionResponderState_[responderMac];
          if (!state.disabled) {
            VLOG(2) << "Disabling self-ignition to " << responderMac
                    << " because dissoc was received";
            state.disabled = true;
          }
          break;
        }
      }
    }
  }

  // On LINK_DOWN, revert to the original state:
  // - Stop authenticator (should only be running on initiator)
  // - Restart supplicant
  if (isWsecEnabled() && drStatus == thrift::DriverLinkStatusType::LINK_DOWN) {
    if (!ifname.empty()) {  // will be empty if link never went up
      // If we receive LINK_DOWN for a different interface while waiting for
      // DR_WSEC_LINKUP_STATUS, add this interface to a queue
      if (responderMac != currResponderMac_ &&
          !currResponderLinkUpIface_.empty()) {
        linkDownIfaceQueue_.insert(ifname);
      } else {
        startSupplicants({ifname}, true);

        // On CNs, restart supplicants on all other interfaces at this point
        // because the only link is down
        if (myNodeType_ == thrift::NodeType::CN) {
          std::unordered_set<std::string> ifaces;
          for (const auto& supplicantIfname : terraIfList_) {
            if (supplicantIfname != ifname) {
              ifaces.insert(supplicantIfname);
            }
          }
          VLOG(2) << "CN mode: "
                     "restarting all supplicants because sole link is down";
          startSupplicants({ifaces}, false);
        }
      }
    }
  }

  // If this link status is for the current ignition:
  // - If wsec=0 *or* we receive LINK_DOWN, immediately reset ignition state
  // - If wsec=1 *and* we receive LINK_UP, wait for DR_WSEC_LINKUP_STATUS
  if (responderMac == currResponderMac_ &&
      (!isWsecEnabled() ||
       drStatus == thrift::DriverLinkStatusType::LINK_DOWN)) {
    resetCurrentIgnitionState();
    setIgnitionTimer(false);
  } else if (isWsecEnabled() && !wasNbrIgnited &&
             drStatus == thrift::DriverLinkStatusType::LINK_UP) {
    currResponderLinkUpIface_ = ifname;

    // On the responder, currResponderMac_ will not have been set. Set it here
    // so that we can reuse the rest of the logic.
    if (currResponderMac_.empty()) {
      currResponderMac_ = responderMac;
      setIgnitionTimer(true);

      // Configure responder's link-specific PSK (if present)
      const auto& wpa_passphrase_override =
          wsecParams.wpaPskParams.wpa_passphrase_override_ref();
      if (isWpaPskEnabled() && wpa_passphrase_override.has_value()) {
        auto linkPskIter = wpa_passphrase_override.value().find(responderMac);
        if (linkPskIter != wpa_passphrase_override.value().end()) {
          const std::string& psk = linkPskIter->second;
          if (forkProcess(configSupplicantCmd(ifname, "psk", psk), true)) {
            LOG(INFO) << "Set link-specific passphrase for supplicant on "
                         "interface " << ifname;
          } else {
            LOG(ERROR) << "Failed to set link-specific passphrase for "
                          "supplicant on interface " << ifname;
          }
        }
      }
    }
  }

  // Handle LINK_PAUSE - dissoc the link if we're in this state for a long time
  if (drStatus == thrift::DriverLinkStatusType::LINK_PAUSE) {
    std::unique_ptr<fbzmq::ZmqTimeout>& timer = linkPauseTimers_[radioMac];
    if (!timer) {
      timer = ZmqTimeout::make(
          this, [ this, radioMac, responderMac, ifname ]() noexcept {
            LOG(INFO) << "<" << radioMac << "> Bringing down link to "
                      << responderMac << " on interface " << ifname
                      << " due to being too long in LINK_PAUSE";
            thrift::SetLinkStatus setLinkStatus;
            setLinkStatus.linkStatusType = thrift::LinkStatusType::LINK_DOWN;
            setLinkStatus.initiatorMac = radioMac;
            setLinkStatus.responderMac = responderMac;
            handleSetLinkDownReq(setLinkStatus);
            linkPauseTimers_.erase(radioMac);
          });
      timer->scheduleTimeout(
          std::chrono::seconds(FLAGS_link_pause_dissoc_delay_s));
    }
  } else {
    auto it = linkPauseTimers_.find(radioMac);
    if (it != linkPauseTimers_.end()) {
      if (it->second->isScheduled()) {
        it->second->cancelTimeout();
      }
      linkPauseTimers_.erase(it);
    }
  }

  // In distributed ignition, DNs should stay in responder mode after we receive
  // LINK_UP to guarantee that links can still be formed to this sector if
  // needed (as it could be a P2MP sector in the controller's topology).
  if (distributedIgnitionEnabled_ &&
      drStatus == thrift::DriverLinkStatusType::LINK_UP &&
      drLinkStatus.selfNodeType == thrift::NodeType::DN) {
    LOG(INFO) << "Enabling responder mode due to LINK_UP in distributed "
                 "ignition after " << kBfRespScanDisableDelaySec << "s";
    scheduleFwBfRespScan(radioMac, true, kBfRespScanDisableDelaySec);
  }
}

void
IgnitionApp::processGetLinkStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto getLinkStatus = maybeReadThrift<thrift::GetLinkStatus>(message);
  if (!getLinkStatus) {
    handleInvalidMessage("GetLinkStatus", senderApp);
    return;
  }

  thrift::LinkStatusType linkStatus = thrift::LinkStatusType::LINK_DOWN;
  std::string radioMac;

  // Look up link status
  auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
  auto iter = lockedIgnitedNeighbors->find(getLinkStatus->responderMac);
  if (iter != lockedIgnitedNeighbors->end()) {
    linkStatus = iter->second.linkStatusType;
    radioMac = iter->second.radioMac_ref().value_or("");
  }
  lockedIgnitedNeighbors.unlock();  // lockedIgnitedNeighbors -> NULL

  sendLinkStatus(getLinkStatus->responderMac, radioMac, linkStatus, false);
}

void
IgnitionApp::processGetLinkStatusDump(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto getLinkStatusDump = maybeReadThrift<thrift::GetLinkStatusDump>(message);
  if (!getLinkStatusDump) {
    handleInvalidMessage("GetLinkStatusDump", senderApp);
    return;
  }

  thrift::LinkStatusDump linkStatusDump;
  linkStatusDump.linkStatusDump = SharedObjects::getIgnitedNeighbors()->copy();
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::LINK_STATUS_DUMP,
      linkStatusDump);
}

void
IgnitionApp::sendLinkStatus(
    const std::string& responderMac,
    const std::string& radioMac,
    const thrift::LinkStatusType& linkStatusType,
    bool isEvent) {
  thrift::LinkStatus linkStatus;
  linkStatus.radioMac_ref() = radioMac;
  linkStatus.responderMac = responderMac;
  linkStatus.linkStatusType = linkStatusType;
  linkStatus.isEvent = isEvent;
  sendToCtrlApp(
      E2EConsts::kTopologyAppCtrlId,
      thrift::MessageType::LINK_STATUS,
      linkStatus);
  sendToBroadcastSock(thrift::MessageType::LINK_STATUS, linkStatus);
}

void
IgnitionApp::sendLinkAuthorized(
    const std::string& responderMac,
    const std::string& radioMac,
    const std::string& ifname,
    int64_t wsecEnable) {
  VLOG(4) << folly::format(
      "Sending LINK_AUTHORIZED (wsec={}): "
      "responderMac={}, radioMac={}, ifname={}",
      wsecEnable, responderMac, radioMac, ifname);

  thrift::LinkAuthorized linkAuthorized;
  linkAuthorized.responderMac = responderMac;
  linkAuthorized.radioMac = radioMac;
  linkAuthorized.ifname = ifname;
  linkAuthorized.wsecEnable = wsecEnable;

  sendToCtrlApp(
      E2EConsts::kTopologyAppCtrlId,
      thrift::MessageType::LINK_AUTHORIZED,
      linkAuthorized);
  sendToBroadcastSock(thrift::MessageType::LINK_AUTHORIZED, linkAuthorized);
}

void
IgnitionApp::sendFwBfRespScan(const std::string& radioMac, bool enable) {
  LOG(INFO) << folly::format(
      "<{}> {} BF responder mode...",
      radioMac, enable ? "Enabling" : "Disabling");
  thrift::BfRespScanConfig bfRespScanConfig;
  bfRespScanConfig.cfg = enable;
  bfRespScanConfig.radioMac_ref() = radioMac;
  sendToDriverApp(
      radioMac,
      thrift::MessageType::FW_BF_RESP_SCAN,
      bfRespScanConfig);
}

void
IgnitionApp::scheduleFwBfRespScan(
    const std::string& radioMac, bool enable, int delaySec) {
  // Cancel any existing timer
  auto iter = bfRespScanTimerMap_.find(radioMac);
  if (iter != bfRespScanTimerMap_.end()) {
    if (iter->second->isScheduled()) {
      iter->second->cancelTimeout();
    }
  }

  bfRespScanTimerMap_[radioMac] = ZmqTimeout::make(
      this, [&, radioMac, enable]() noexcept {
    sendFwBfRespScan(radioMac, enable);
  });
  bfRespScanTimerMap_[radioMac]->scheduleTimeout(
      std::chrono::seconds(delaySec));
}

void
IgnitionApp::processBfRespScan(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto bfRespScan = maybeReadThrift<thrift::BfRespScan>(message);
  if (!bfRespScan) {
    handleInvalidMessage("BfRespScan", senderApp);
    return;
  }
  const std::string radioMac = bfRespScan->radioMac;

  VLOG(2) << "Received BF_RESP_SCAN for MAC " << radioMac << " with timer of "
          << bfRespScan->secondsToDisable << "s";

  // Enable BF responder mode
  sendFwBfRespScan(radioMac, true);

  // Disable BF responder mode after timeout
  scheduleFwBfRespScan(radioMac, false, bfRespScan->secondsToDisable);
}

void
IgnitionApp::processBfRespScanDisable(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto bfRespScan = maybeReadThrift<thrift::BfRespScan>(message);
  if (!bfRespScan) {
    handleInvalidMessage("BfRespScan", senderApp);
    return;
  }

  VLOG(2) << "Received BF_RESP_SCAN_DISABLE for MAC " << bfRespScan->radioMac
          << ", scheduling disable after " << kBfRespScanDisableDelaySec
          << "s...";

  // Disable BF responder mode after delay
  scheduleFwBfRespScan(bfRespScan->radioMac, false, kBfRespScanDisableDelaySec);
}

void
IgnitionApp::processDriverWsecStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto drMessage = maybeReadDriverMessage<thrift::DriverWsecStatus>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverWsecStatus", senderApp);
    return;
  }
  thrift::DriverWsecStatus& driverWsecStatus = drMessage->value;
  std::string ifname = driverWsecStatus.ifname;

  LOG(INFO) << drMessage->macPrefix() << "Received DriverWsecStatus message";

  // Validity check
  if (senderApp != E2EConsts::kDriverAppMinionId) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecStatus message from " << senderApp
               << " (expecting " << E2EConsts::kDriverAppMinionId << ")";
    return;
  }
  if (driverWsecStatus.status) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecStatus for interface `" << ifname
               << "` with wsec=1";
    return;  // shouldn't happen
  }

  // send notification (with wsecEnable=0)
  // we don't always know the responder MAC, so we need to look for it
  {
    auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
    for (const auto& kv : *lockedIgnitedNeighbors) {
      if (kv.second.ifname_ref().has_value()
          && kv.second.ifname_ref().value() == ifname
          && kv.second.radioMac_ref().has_value()
          && kv.second.radioMac_ref().value() == drMessage->radioMac
          && kv.second.linkStatusType == thrift::LinkStatusType::LINK_UP) {
        sendLinkAuthorized(
            kv.second.responderMac, drMessage->radioMac, ifname, 0);
        break;
      }
    }
  }

  // we may receive this driver notification when wsec is already disabled.
  // if that's the case, drop the message here.
  if (!isWsecEnabled()) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecStatus message from " << senderApp
               << " (link encryption is disabled)";
    return;
  }

  // stop supplicant/authenticator on this interface
  LOG(INFO) << drMessage->macPrefix()
            << "Interface `" << ifname
            << "` is connecting in open mode (wsec=0). "
               "Stopping supplicant/authenticator...";
  forkProcess(stopHostapdCmd(ifname), false);
  forkProcess(stopSupplicantCmd(ifname), false);

  // reset ignition state if needed
  if (ifname == currResponderLinkUpIface_) {
    currResponderLinkUpIface_.clear();
    resetCurrentIgnitionState();
    setIgnitionTimer(false);
  }
}

void
IgnitionApp::processDriverWsecLinkupStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto drMessage =
      maybeReadDriverMessage<thrift::DriverWsecLinkupStatus>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverWsecLinkupStatus", senderApp);
    return;
  }
  thrift::DriverWsecLinkupStatus& driverWsecLinkupStatus = drMessage->value;
  std::string ifname = driverWsecLinkupStatus.ifname;

  LOG(INFO) << drMessage->macPrefix()
            << "Received DriverWsecLinkupStatus message";

  // Validity checks
  if (!isWsecEnabled()) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecLinkupStatus message from " << senderApp
               << " (link encryption is disabled)";
    return;
  }
  if (senderApp != E2EConsts::kDriverAppMinionId) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecLinkupStatus message from " << senderApp
               << " (expecting " << E2EConsts::kDriverAppMinionId << ")";
    return;
  }
  if (ifname.empty()) {
    LOG(ERROR) << drMessage->macPrefix()
               << "Ignoring DriverWsecLinkupStatus (empty interface)";
    return;
  }

  // If we received successive LINK_UP events (e.g. as responders on different
  // sectors) before the corresponding authorization event for each, our
  // internal state variables may have changed, in which case we should still
  // treat the authorization event as valid but otherwise discard it.
  // TODO: fix me if parallel multi-radio ignition support is added
  LOG(INFO) << drMessage->macPrefix()
            << "Interface `" << ifname << "` is now authorized.";

  // send notification
  std::string responderMac;
  if (currResponderLinkUpIface_ == ifname) {
    responderMac = currResponderMac_;
  } else {
    // not the current responder, so look for it
    auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
    for (const auto& kv : *lockedIgnitedNeighbors) {
      if (kv.second.ifname_ref() && kv.second.ifname_ref().value() == ifname &&
          kv.second.radioMac_ref().has_value() &&
          kv.second.radioMac_ref().value() == drMessage->radioMac &&
          kv.second.linkStatusType == thrift::LinkStatusType::LINK_UP) {
        responderMac = kv.first;
      }
    }
  }
  sendLinkAuthorized(responderMac, drMessage->radioMac, ifname, wsecEnable_);

  // end processing here if we are tracking a different link already
  if (currResponderLinkUpIface_ != ifname) {
    if (currResponderLinkUpIface_.empty()) {
      LOG(ERROR) << drMessage->macPrefix()
                << "Discarding DriverWsecLinkupStatus for interface `" << ifname
                << "` (have not yet received LINK_UP from driver)";
    } else {
      LOG(ERROR) << drMessage->macPrefix()
                << "Discarding DriverWsecLinkupStatus for interface `" << ifname
                << "` (interface is not currently being ignited)";
    }
    return;
  }

  // authorization finished, so reset ignition timers now
  currResponderLinkUpIface_.clear();
  resetCurrentIgnitionState();
  setIgnitionTimer(false);

  // on CNs, kill supplicants on all other interfaces at this point because the
  // only link is up and authorized
  if (myNodeType_ == thrift::NodeType::CN) {
    VLOG(2) << "CN mode: "
               "stopping all other supplicants because sole link is up";
    for (const auto& supplicantIfname : terraIfList_) {
      if (supplicantIfname != ifname) {
        if (forkProcess(stopSupplicantCmd(supplicantIfname), true)) {
          LOG(INFO) << "Stopped supplicant on " << supplicantIfname;
        }
      }
    }
  }
}

void
IgnitionApp::processDevUpDownStatus(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  auto drMessage =
      maybeReadDriverMessage<thrift::DriverDevUpDownStatus>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverDevUpDownStatus", senderApp);
    return;
  }
  thrift::DriverDevUpDownStatus& devUpDownStatus = drMessage->value;

  if (senderApp != E2EConsts::kStatusAppMinionId) {
    LOG(ERROR) << "Ignoring DriverDevUpDownStatus message from " << senderApp
               << " (expected minion StatusApp)";
    return;
  }
  if (devUpDownStatus.status) {
    return;  // ignore UP (shouldn't get forwarded by StatusApp)
  }

  // Trigger LINK_DOWN actions for any neighbors of the now-offline baseband
  std::vector<thrift::LinkStatus> offlineNbrs;
  auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
  for (const auto& kv : *lockedIgnitedNeighbors) {
    if (kv.second.radioMac_ref().value() == drMessage->radioMac) {
      offlineNbrs.push_back(kv.second);
    }
  }
  lockedIgnitedNeighbors.unlock();  // lockedIgnitedNeighbors -> NULL
  for (const thrift::LinkStatus& nbrInfo : offlineNbrs) {
    if (!nbrInfo.ifname_ref().has_value()) {
      LOG(ERROR) << drMessage->macPrefix() << "No ifname defined for link to "
                 << nbrInfo.responderMac;
      continue;
    }
    LOG(INFO) << drMessage->macPrefix() << "Bringing down link to "
              << nbrInfo.responderMac << " on interface "
              << nbrInfo.ifname_ref().value();
    thrift::DriverLinkStatus drLinkStatus;
    drLinkStatus.valid = true;
    drLinkStatus.macAddr = nbrInfo.responderMac;
    drLinkStatus.drLinkStatusType = thrift::DriverLinkStatusType::LINK_DOWN;
    drLinkStatus.linkDownCause = thrift::LinkDownCause::NOT_APPLICABLE;
    drLinkStatus.ifname = nbrInfo.ifname_ref().value();
    handleDriverLinkStatus(drLinkStatus, drMessage->radioMac);
  }
}

void
IgnitionApp::processGpsEnabled(const std::string& senderApp) noexcept {
  if (senderApp != E2EConsts::kStatusAppMinionId) {
    LOG(ERROR) << "Ignoring GpsEnabled message from " << senderApp
               << " (expected minion StatusApp)";
    return;
  }

  // Invoke link ignition function if it isn't already scheduled.
  if (distributedIgnitionEnabled_ &&
      !distributedIgnitionTimer_->isScheduled()) {
    LOG(INFO) << "Starting distributed ignition (GPS is enabled)";

    // Determine the amount of time until the next time boundary (distributed
    // ignition occurs at the same timestamp on each node to minimize the
    // possibility of interference). The GPS clock has likely not been
    // initialized yet. In that case, this function call will return a small
    // time duration (around a second), and polling will continue at shorter
    // intervals until the GPS clock has been enabled.
    auto waitTime = getTimeToNextIgnitionBoundary();

    // Schedule ignition to begin on a boundary. Boundaries occur every
    // FLAGS_distributed_ignition_cooldown_duration_ms milliseconds,
    // starting at a multiple of the value. For example, if the flag is
    // set to 5000 ms, boundaries occur at 0s, 5s, 10s, ...
    distributedIgnitionTimer_->scheduleTimeout(waitTime);
  }
}

void
IgnitionApp::processControllerConnected(const std::string& senderApp) noexcept {
  if (senderApp != E2EConsts::kStatusAppMinionId) {
    LOG(ERROR) << "Ignoring ControllerConnected message from " << senderApp
               << " (expected minion StatusApp)";
    return;
  }

  // Disable responder mode on all radios, as we may have enabled it during
  // distributed ignition. If a sector has no links up, this is a no-op (will
  // not actually disable responder mode).
  if (distributedIgnitionEnabled_) {
    auto radioMacToBusId =
      SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();
    if (!radioMacToBusId.empty()) {
      LOG(INFO) << "Disabling responder mode on all radios with distributed "
                   "ignition after " << kBfRespScanDisableDelaySec << "s";
      for (const auto& kv : radioMacToBusId) {
        scheduleFwBfRespScan(kv.first, false, kBfRespScanDisableDelaySec);
      }
    }
  }

  // Connected to the controller. Cancel distributed ignition and let the
  // controller determine any links to ignite.
  distributedIgnitionEnabled_ = false;
  if (distributedIgnitionTimer_->isScheduled()) {
    LOG(INFO) << "Disabling distributed ignition (connected to controller)";
    distributedIgnitionTimer_->cancelTimeout();
  }
}

std::chrono::milliseconds
IgnitionApp::getTimeToNextIgnitionBoundary() {
  auto now = GpsClock::now();
  auto boundary = std::chrono::milliseconds(
      FLAGS_distributed_ignition_cooldown_duration_ms);
  if (now.time_since_epoch().count() == 0) {
    // If the GpsClock has not been calibrated, return a default amount of
    // time to wait.
    return std::chrono::milliseconds(
        FLAGS_distributed_ignition_gps_clock_poll_ms);
  }
  // Determine the amount of time elapsed since the last boundary.
  auto timeSinceBoundary = std::chrono::milliseconds(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch()).count() % boundary.count());
  // Example: boundaries are at 0s, 5s, 10s, ..., now = 6s. The time since the
  // last boundary is 1s, so the time to the next boundary is is 5s - 6s % 5s
  // = 5s - 1s = 4s.
  return boundary - timeSinceBoundary;
}

void
IgnitionApp::selfIgnite() noexcept {
  if (!distributedIgnitionEnabled_) {
    return;
  }

  // Calculate the amount of time to wait until the next ignition attempt
  // should occur.
  auto gpsNow = GpsClock::now();
  bool gpsClockEnabled = gpsNow.time_since_epoch().count() != 0;
  auto waitTime = getTimeToNextIgnitionBoundary();
  auto cooldown = std::chrono::milliseconds(
      FLAGS_distributed_ignition_cooldown_duration_ms);
  // If the wait is less some factor of the boundary, skip the next boundary.
  // We don't want another ignition attempt to begin too soon after the
  // current one. Only skip the next boundary if the GPS clock is enabled,
  // otherwise continue periodic polling to begin ignition quicker after GPS
  // clock is enabled.
  // NOTE: This time (cooldown / 6) is arbitrary.
  if (waitTime < cooldown / 6 && gpsClockEnabled) {
    waitTime += cooldown;
  }

  // Schedule timer for next boundary.
  distributedIgnitionTimer_->scheduleTimeout(waitTime);

  // If current ignition attempt falls too far outside of the time boundary,
  // skip this ignition attempt to prevent possible interference. Also skip this
  // attempt if the GPS clock has not been set.
  if (std::abs((cooldown - waitTime).count()) >
      FLAGS_distributed_ignition_max_offset_ms ||
      !gpsClockEnabled) {
    return;
  }

  // Only one link at a time should attempt ignition to reduce interference.
  if (!currResponderMac_.empty()) {
    return;
  }

  // Calculate the ignition index. The number of colors used for distributed
  // ignition determines how many "slots" there are for ignition. Each radio
  // can only attempt ignition in its "slot". The current slot is calculated
  // by looking at the time.
  auto secondsSinceEpoch = std::chrono::duration_cast<std::chrono::seconds>(
      gpsNow.time_since_epoch()).count();
  auto cooldownDurationSeconds =
    std::chrono::duration_cast<std::chrono::seconds>(cooldown).count();
  // Length of a "boundary" in seconds. Each color will only attempt ignition
  // once per boundary duration.
  auto boundaryDuration = cooldownDurationSeconds *
      topologyInfo_.distributedIgnitionNumColors_ref().value();
  auto remainder = secondsSinceEpoch % boundaryDuration;
  int ignitionIndex = (int) (remainder / cooldownDurationSeconds);

  // Get a copy of ignited neighbors...
  std::unordered_set<std::string> ignitedNbrs;
  auto lockedIgnitedNeighbors = SharedObjects::getIgnitedNeighbors()->rlock();
  for (const auto& kv : *lockedIgnitedNeighbors) {
    ignitedNbrs.insert(kv.first);
  }
  lockedIgnitedNeighbors.unlock();  // lockedIgnitedNeighbors -> NULL

  std::optional<thrift::SetLinkStatus> setLinkStatus;
  auto now = std::chrono::steady_clock::now();

  // Grab the next available link and attempt to ignite it.
  for (std::size_t i = 0; i < linkCandidates_.size(); ++i) {
    const auto& candidate = linkCandidates_.at((
        i + distributedIgnitionLinkIterationIndex_) % linkCandidates_.size());

    // If link is already ignited, ignore it. Keep the link in the list of
    // candidates because it could go down in the future.
    if (ignitedNbrs.count(candidate.responderMac)) {
      continue;
    }

    // If information about the radio doesn't exist, cannot self ignite.
    std::unordered_map<std::string, thrift::RadioInfo> radioInfo;
    if (topologyInfo_.radioInfo_ref().has_value()) {
      radioInfo = topologyInfo_.radioInfo_ref().value();
    }
    if (!radioInfo.count(candidate.initiatorMac)) {
      continue;
    }

    int color;
    if (radioInfo[candidate.initiatorMac].distributedIgnitionColor_ref()
        .has_value()) {
      color = radioInfo[candidate.initiatorMac].distributedIgnitionColor_ref()
          .value();
    } else {
      // If the radio has no assigned color, an appropriate ignition time slot
      // cannot be determined, so self ignition should not be attempted.
      continue;
    }

    // Ignition is bucketed into slots based on the radio's color. If the
    // current ignition slot does not match the radio's color, ignore it.
    if (color != ignitionIndex) {
      continue;
    }

    // Determine time since last ignition attempt. To avoid interference,
    // unsuccessful link attempts result in more time before the link can
    // be reignited.
    if (distributedIgnitionResponderState_.count(candidate.responderMac)) {
      const auto& responderState =
          distributedIgnitionResponderState_[candidate.responderMac];

      if (responderState.disabled) {
        continue;
      }

      if (responderState.numAttempts >=
          FLAGS_distributed_ignition_max_attempts) {
        // Cap the number of ignition attempts for a link.
        continue;
      }

      if (responderState.attemptTs && responderState.cooldownDuration) {
        // There was a previous attempt to ignite this link. Don't attempt
        // reignition if the cooldown duration has not passed.
        if (now - responderState.attemptTs.value() <
            responderState.cooldownDuration.value()) {
          continue;
        }
      }
    }

    setLinkStatus = candidate;
    break;
  }
  distributedIgnitionLinkIterationIndex_ =
      (distributedIgnitionLinkIterationIndex_ + 1) % linkCandidates_.size();

  if (!setLinkStatus) {
    // No links available.
    return;
  }

  std::string responderMac = setLinkStatus->responderMac;

  // Get info about link, or create new object to store info if it doesn't
  // exist.
  if (!distributedIgnitionResponderState_.count(responderMac)) {
    distributedIgnitionResponderState_[responderMac] =
        IgnitionApp::DistributedIgnitionResponderState();
  }
  auto& responderState = distributedIgnitionResponderState_[responderMac];

  if (responderState.numAttempts >=
      FLAGS_distributed_ignition_attempts_before_backoff) {
    // After some number of ignition attempts, begin to delay future attempts
    // in case too much interference is being caused.
    if (!responderState.cooldownDuration) {
      // The boundary duration is an integer representing the boundary duration
      // in seconds. Convert it to a chrono time_point in seconds, and the
      // chrono library will handle the conversion to milliseconds for storage.
      responderState.cooldownDuration =
          std::chrono::seconds(boundaryDuration);
    } else {
      // Double the cooldown duration after each failed ignition attempt.
      *responderState.cooldownDuration *= 2;
    }

    // Randomly add an extra boundary duration to the cooldown. Note that the
    // probability of adding an extra boundary duration (25%) is arbitrary.
    if (folly::Random::rand32(4) == 1) {
      *responderState.cooldownDuration +=
          std::chrono::seconds(boundaryDuration);
    }
  }

  responderState.numAttempts++;
  responderState.attemptTs = now;

  LOG(INFO) << "Self igniting link to " << setLinkStatus->responderMac;
  handleSetLinkUpReq(setLinkStatus.value());
}

void
IgnitionApp::resetCurrentIgnitionState() {
  // If we failed a wsec handshake, the wireless link may still be alive from
  // our firmware's perspective. We must send an explicit dissoc.
  if (!currResponderLinkUpIface_.empty()) {
    if (SharedObjects::getIgnitedNeighbors()->rlock()
            ->count(currResponderMac_)) {
      LOG(INFO) << "Bringing link to " << currResponderMac_ << " down on "
                << currResponderLinkUpIface_;
      thrift::SetLinkStatus setLinkStatus;
      setLinkStatus.linkStatusType = thrift::LinkStatusType::LINK_DOWN;
      setLinkStatus.responderMac = currResponderMac_;
      setLinkStatus.initiatorMac = currInitiatorMac_;
      handleSetLinkDownReq(setLinkStatus);
    }
  }

  // Reset state
  currResponderMac_.clear();
  currInitiatorMac_.clear();
  currResponderHostapdIface_.clear();
  currResponderLinkUpIface_.clear();

  // Start supplicant processes
  startSupplicants(linkDownIfaceQueue_, true);
  linkDownIfaceQueue_.clear();
}

void
IgnitionApp::setIgnitionTimer(bool start) {
  if (start) {
    VLOG(2) << "Starting ignition timer...";
    clearCurrRespMacTimer_->scheduleTimeout(linkupRespWaitTimeout_);
  } else {
    if (clearCurrRespMacTimer_->isScheduled()) {
      VLOG(2) << "Stopping ignition timer...";
      clearCurrRespMacTimer_->cancelTimeout();
    }
  }
}

thrift::WsecParams
IgnitionApp::getWsecParams() {
  auto radioParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getRadioParams();
  return radioParams.wsecParams;
}

void
IgnitionApp::startSupplicants(
    const std::unordered_set<std::string>& ifaces, bool killFirst) {
  if (ifaces.empty()) {
    return;
  }

  for (const auto& ifname : ifaces) {
    // wait until any existing hostapd/supplicant is dead,
    // otherwise new supplicant can't be started
    if (killFirst) {
      if (forkProcess(stopHostapdCmd(ifname), true)) {
        LOG(INFO) << "Stopped authenticator on " << ifname;
        /* sleep override */ std::this_thread::sleep_for(kStopCmdSleepTime);
      } else if (forkProcess(stopSupplicantCmd(ifname), true)) {
        LOG(INFO) << "Stopped supplicant on " << ifname;
        /* sleep override */ std::this_thread::sleep_for(kStopCmdSleepTime);
      }
    }

    if (forkProcess(startSupplicantCmd(ifname), false)) {
      LOG(INFO) << "Started supplicant on " << ifname;

      if (isEapolEnabled()) {
        // Wait for process to start CLI interface
        /* sleep override */ std::this_thread::sleep_for(kCfgStartCmdSleepTime);

        if (forkProcess(configSupplicantCmd(
            ifname,
            "password",
            getValueforSecret("radius_user_password")), true)) {
          LOG(INFO) << "Configured password for supplicant on " << ifname;
        } else {
          LOG(ERROR) << "Failed to set password on wpa_supplicant for "
                     "interface " << ifname;
        }

        if (forkProcess(configSupplicantCmd(
            ifname,
            "private_key_passwd",
            getValueforSecret("private_key_password")), true)) {
          LOG(INFO) << "Configured private_key_passwd for supplicant on "
                    << ifname;
        } else {
          LOG(ERROR) << "Failed to set private_key_passwd on wpa_supplicant "
                        "for interface " << ifname;
        }
      }
    } else {
      LOG(ERROR) << "Failed to start wpa_supplicant for interface "
                 << ifname;
    }
  }
}

std::string
IgnitionApp::getValueforSecret(const std::string& key) {
  // TODO: Get secrets based on hardware, e.g. P64479831
  auto eapolParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getEAPoLParams();
  auto iter = eapolParams->secrets.find(key);
  if (iter == eapolParams->secrets.end()) {
      LOG(ERROR) << "secret " << key << " not found in config";
      return "";
  } else {
    return iter->second;
  }
}

bool
IgnitionApp::forkProcess(const std::string& command, bool waitForExit) {
  VLOG(4) << "Forking command: " << command;
  pid_t pid = fork();
  if (pid == 0) {  // child process
    if (!waitForExit) {
      // If we don't want to wait for the child to exit, double-fork a
      // grandchild to run the actual command and exit immediately in the child.
      // The parent can reap the child normally; the grandchild is orphaned and
      // reaped by the init process (preventing zombies).
      pid_t pid2 = fork();
      if (pid2 > 0) {  // same child process
        _exit(0);
      } else if (pid2 < 0) {
        _exit(EXIT_FAILURE);
      }
      // fall through as grandchild process
    }
    execl(_PATH_BSHELL, _PATH_BSHELL, "-c", command.c_str(), nullptr);
    _exit(0);
  } else if (pid > 0) {  // parent process
    int status;
    while (waitpid(pid, &status, 0) > 0);
    return (WIFEXITED(status) && WEXITSTATUS(status) == 0);
  } else {
    return false;
  }
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
