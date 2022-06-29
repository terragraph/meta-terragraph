/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ScanApp.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/gen/String.h>
#include <utility>

#include "ScanScheduler.h"
#include "SchedulerApp.h"
#include "SharedObjects.h"
#include "algorithms/PolarityHelper.h"
#include "e2e/common/Consts.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/JsonUtils.h"
#include "e2e/common/TimeUtils.h"
#include "topology/TopologyWrapper.h"
#include "time/ChronoFlags.h"

using apache::thrift::detail::TEnumMapFactory;

using namespace fbzmq;

namespace facebook {
namespace terragraph {

static const auto kTimeSyncWarnInterval = std::chrono::hours(1);
static const double kSecondsPerBwgd = 0.0256;
static const int kMaxBeamIdx = 255;
static const int kInvalidBeamIdx = -1;
static const int kMaxBeamImScan = 63;
static const int kInvalidTrxOffset = -100;
static const std::vector<float> kMcsToSnr{
    0.0, 1.0, 2.5, 3.0, 4.5, 5.0, 5.5, 7.5, 9.0, 12.0, 14.0, 16.0, 18.0};

DEFINE_uint32(
    scan_max_results,
    5000,
    "Hold at most this many scan results in memory. Remove oldest result if "
    "new result goes above this limit. A result holds measurements of a "
    "single initiator and all corresponding responder nodes");

DEFINE_bool(
    scan_disable_periodic,
    false,
    "Kill switch to disable automatic periodic PBF and IM scans by the "
    "controller. Manual scans initiated with `tg` will still work");

DEFINE_bool(
    vbs_rx_enable,
    true,
    "Enable RX VBS when RTCAL is enabled for scheduled scans");

DEFINE_bool(
    vbs_tx_enable,
    false,
    "Enable TX VBS when RTCAL is enabled for scheduled scans");

DEFINE_bool(
    scan_topo_always_use_new_data,
    false,
    "Always use new IM scan data even when older data at higher power and with "
    "more routes is available");

DEFINE_int32(
    pbf_relative_range,
    5,
    "One-sided range to use for periodic relative PBF scans");

DEFINE_int32(
    pbf_relative_bwgd_len,
    4,
    "Duration of periodic relative PBF scan in number of BWGD");

DEFINE_bool(
    cbf_config_apply,
    true,
    "Set apply to true when generating CBF config from scan data");

DEFINE_bool(
    cbf_config_periodic_update_enable,
    true,
    "Update CBF config during periodic scans if CBF enabled");

DEFINE_bool(
    cbf_use_im_for_link_state,
    true,
    "Use IM data to determine beams if actual beams unknown");

DEFINE_bool(
    cbf_rx_pwr_assignment_enable,
    true,
    "Set TX power for RX CBF nulling scans based on target SNR/INR/SIR");

DEFINE_bool(
    cbf_tx_pwr_assignment_enable,
    false,
    "Set TX power for TX CBF nulling scans based on target SNR/INR/SIR");

DEFINE_int32(cbf_max_tx_pwr, 21, "Max TX power index used for CBF scans");

DEFINE_double(cbf_target_snr, 20, "Target SNR for CBF scans");

DEFINE_double(cbf_target_inr, 10, "Target INR for CBF scans");

DEFINE_double(cbf_min_target_sir, 3, "Minimum target SIR for CBF scans");

DEFINE_double(
    cbf_inr_tx_pwr_offset_nulling,
    30,
    "Offset to current TX power used to compute INR for determining if CBF "
    "should be enabled for a link. Set to max power to generate CBF config "
    "using worst-case interference.");

DEFINE_double(
    cbf_inr_tx_pwr_offset_aux_set,
    0,
    "Offset to current TX power used to compute INR for determining if link "
    "should be included in the aux set.");

DEFINE_double(
    cbf_inr_thresh_nulling, 0, "CBF INR threshold for enabling nulling");

DEFINE_double(
    cbf_inr_thresh_aux_set,
    -10,
    "CBF INR threshold for including link in aux set.");

DEFINE_bool(
    cbf_allow_same_site_aux,
    false,
    "Include same site aux links in CBF scan if they meet INR threshold (RX "
    "CBF: ATX at same site as VTX, TX CBF: VRX at same site as ARX).");

DEFINE_double(
    cbf_sinr_change_thresh,
    0.9,
    "CBF threshold for SINR improvement required to use nulling beam");

DEFINE_double(
    cbf_snr_change_thresh,
    -2.0,
    "CBF threshold for maximum SNR loss allowed to use nulling beam");

DEFINE_double(
    cbf_victim_count_scale,
    0.5,
    "CBF scale factor for victim link packet count threshold");

DEFINE_double(
    cbf_main_count_scale,
    0.75,
    "CBF scale factor for main link packet count threshold");

DEFINE_bool(
    scan_cbf_use_ref_beam,
    false,
    "Use CBF reference beam if no nulling beam found (useful for testing)");

DEFINE_time_s(
    scheduled_scans_start_time_offset,
    15_s,
    "Minimum time delay from now to start first scheduled scan (in seconds).");

DEFINE_time_s(
    scans_start_time_offset,
    5_s,
    "Time advance from start time to send command to minion (in seconds).");

DEFINE_time_s(
    scan_all_responses_timeout,
    30_s,
    "Timer is set after the scheduled scan time to check if all responses "
    "have been received (in seconds).");

// Large negative SNR value
static const auto kScanSnrMin = -1000.0;
// Reference beam used to measure performance of nulling beams
static const auto kCbfRefBeamIdx = 0;
// Number of CBF beams expected in CBF scan response
static const auto kCbfNumBeams = 152;
// Starting index for CBF beams
static const auto kCbfBeamOffset = 64;
// Maximum packet index expected in CBF scan response
static const auto kCbfMaxPktIdx = 1;

namespace {

int
scanDurationBwgd(const thrift::StartScan& startScan) {
  // Scan duration is defined by the firmware
  switch (startScan.scanMode) {
    case thrift::ScanMode::COARSE:
      return 28;
    case thrift::ScanMode::FINE:
      return 76;
    case thrift::ScanMode::SELECTIVE:
      return startScan.bwgdLen_ref().value_or(0) + 12;
    case thrift::ScanMode::RELATIVE:
      return startScan.bwgdLen_ref().value_or(0) + 12;
    default:
      CHECK(false);
      assert(false);
  }
}

thrift::ScanData
makeScanData(
    const std::string& txMac,
    uint64_t bwgd,
    const thrift::StartScan& startScan,
    const int nResponsesWaiting,
    const int groupId,
    bool nodeByName = false) {
  thrift::ScanData data;
  data.txNode = txMac;
  data.startBwgdIdx = bwgd;
  data.type = startScan.scanType;
  if (startScan.subType_ref().has_value()) {
    data.subType_ref() = startScan.subType_ref().value();
  }
  data.mode = startScan.scanMode;
  data.nResponsesWaiting_ref() = nResponsesWaiting;
  data.respId = 0;
  if (startScan.apply_ref().has_value()) {
    data.apply_ref() = startScan.apply_ref().value();
  }
  if (startScan.mainTxNode_ref().has_value()) {
    data.mainTxNode_ref() = startScan.mainTxNode_ref().value();
  }
  if (startScan.mainRxNode_ref().has_value()) {
    data.mainRxNode_ref() = startScan.mainRxNode_ref().value();
  }
  if (startScan.auxTxNodes_ref().has_value()) {
    data.auxTxNodes_ref() = startScan.auxTxNodes_ref().value();
  }
  if (startScan.auxRxNodes_ref().has_value()) {
    data.auxRxNodes_ref() = startScan.auxRxNodes_ref().value();
  }
  if (startScan.nullAngle_ref().has_value()) {
    data.nullAngle_ref() = startScan.nullAngle_ref().value();
  }
  if (startScan.cbfBeamIdx_ref().has_value()) {
    data.cbfBeamIdx_ref() = startScan.cbfBeamIdx_ref().value();
  }
  if (startScan.applyBwgdIdx_ref().has_value()) {
    data.applyBwgdIdx_ref() = startScan.applyBwgdIdx_ref().value();
  }
  if (startScan.rxNodes_ref().has_value()) {
    data.rxNodes_ref() = startScan.rxNodes_ref().value();
  }
  data.groupId_ref() = groupId;
  if (nodeByName) {
    data.convertMacToName_ref() = true;
  }
  return data;
}

bool
isHybridPolarity(const std::optional<thrift::PolarityType> polarity) {
  return polarity && (polarity == thrift::PolarityType::HYBRID_ODD ||
                        polarity == thrift::PolarityType::HYBRID_EVEN);
}

// This is like folly::get_default(), except when not found,
// the result string contains the integer value of the argument.
std::string
scanTypeToStr(thrift::ScanType scanType) {
  auto scanTypeMap = TEnumMapFactory<thrift::ScanType>::makeValuesToNamesMap();
  auto it = scanTypeMap.find(scanType);
  if (it != scanTypeMap.end()) {
    return it->second;
  } else {
    return folly::sformat("UNKNOWN({})", (int)scanType);
  }
}

} // namespace

ScanApp::ScanApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    SchedulerApp& schedulerApp)
    : CtrlApp(
          zmqContext, routerSockUrl, monitorSockUrl, E2EConsts::kScanAppCtrlId),
      schedulerApp_(schedulerApp) {
  // Schedule periodic scans (if enabled)
  if (!FLAGS_scan_disable_periodic) {
    imScanTimeout_ =
        ZmqTimeout::make(this, [this]() noexcept { imScanStart(); });
    combinedScanTimeout_ = ZmqTimeout::make(this, [this]() noexcept {
      thrift::ScanSchedule scanSchedule = SharedObjects::getE2EConfigWrapper()
                                              ->rlock()
                                              ->getConfig()
                                              ->scanParams.scanSchedule;
      combinedScanStart(scanSchedule);
    });

    updateScanTimers();
  }
}

void
ScanApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::START_SCAN:
      if (auto request = maybeReadThrift<thrift::StartScan>(message)) {
        VLOG(2) << "Received start scan request from " << senderApp;
        processStartScan(senderApp, *request);
      } else {
        handleInvalidMessage("StartScan", senderApp, minion);
      }
      break;
    case thrift::MessageType::SCAN_RESP:
      if (auto scanResp = maybeReadThrift<thrift::ScanResp>(message)) {
        VLOG(5) << "Got scan response for scan id " << scanResp->token
                << " from radio "
                << scanResp->radioMac_ref().value_or("<Unset>")
                << ", minion " << minion;
        processScanResp(minion, std::move(*scanResp));
      } else {
        handleInvalidMessage("ScanResp", senderApp, minion, false);
      }
      break;
    case thrift::MessageType::GET_SCAN_STATUS:
      if (auto request = maybeReadThrift<thrift::GetScanStatus>(message)) {
        VLOG(4) << "Received request for scan result from " << senderApp;
        processGetScanStatus(senderApp, *request);
      } else {
        handleInvalidMessage("GetScanStatus", senderApp, minion);
      }
      break;
    case thrift::MessageType::RESET_SCAN_STATUS:
      VLOG(4) << "Received request to reset scan result from " << senderApp;
      processResetScanStatus(senderApp);
      break;
    case thrift::MessageType::GET_CBF_CONFIG:
      if (auto request = maybeReadThrift<thrift::GetCbfConfig>(message)) {
        VLOG(4) << "Received request to get CBF config from " << senderApp;
        processGetCbfConfig(senderApp, *request);
      } else {
        handleInvalidMessage("GetCbfConfig", senderApp, minion);
      }
      break;
    case thrift::MessageType::SET_CBF_CONFIG:
      VLOG(4) << "Received request to set CBF config from " << senderApp;
      processSetCbfConfig(senderApp);
      break;
    case thrift::MessageType::RESET_CBF_CONFIG:
      VLOG(4) << "Received request to reset CBF config from " << senderApp;
      processResetCbfConfig(senderApp);
      break;
    case thrift::MessageType::GET_RF_STATE:
      VLOG(4) << "Received request to get RF state from " << senderApp;
      processGetRfState(senderApp);
      break;
    case thrift::MessageType::SET_RF_STATE:
      if (auto request = maybeReadThrift<thrift::SetRfState>(message)) {
        VLOG(4) << "Received request to set RF state from " << senderApp;
        processSetRfState(senderApp, *request);
      } else {
        handleInvalidMessage("SetRfState", senderApp, minion);
      }
      break;
    case thrift::MessageType::RESET_RF_STATE:
      VLOG(4) << "Received request to reset RF state from " << senderApp;
      processResetRfState(senderApp);
      break;
    case thrift::MessageType::SET_LATPC_PARAMS:
      VLOG(4) << "Received request to set LA/TPC params from RF state from "
              << senderApp;
      processSetLaTpcParams(senderApp);
      break;
    case thrift::MessageType::GET_SCAN_SCHEDULE:
      if (auto request = maybeReadThrift<thrift::GetScanSchedule>(message)) {
        VLOG(4) << "Received request to get scan schedule from " << senderApp;
        processGetScanSchedule(senderApp, *request);
      } else {
        handleInvalidMessage("GetScanSchedule", senderApp, minion);
      }
      break;
    case thrift::MessageType::SET_SCAN_SCHEDULE:
      if (auto request = maybeReadThrift<thrift::ScanSchedule>(message)) {
        VLOG(2) << "Received request to set scan schedule from " << senderApp;
        processSetScanSchedule(senderApp, *request);
      } else {
        handleInvalidMessage("ScanSchedule", senderApp, minion);
      }
      break;
    case thrift::MessageType::SCAN_SCHEDULE_UPDATED:
      processScanConfigUpdated();
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
ScanApp::addScan(int scanId, thrift::ScanData&& data) {
  // Schedule a timer scan_all_responses_timeout seconds after the
  // scheduled scan time to check if all responses have been received
  std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
  std::chrono::system_clock::time_point timeoutTime = (data.startBwgdIdx > 0)
    ? TimeUtils::bwgdToUnixTime(data.startBwgdIdx)
    : std::chrono::system_clock::now();
  scheduleTimeout(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          timeoutTime + FLAGS_scan_all_responses_timeout_s - now),
      [ this, scanId ]() noexcept {
        auto it = scanStatus_.scans.find(scanId);
        if (it == scanStatus_.scans.end()) {
          LOG(ERROR) << "Unknown scan id " << scanId
                     << " in CheckAllResponses timer";
          return;
        }
        if (!it->second.nResponsesWaiting_ref().value_or(0)) {
          // All nodes have already responded, nothing to do
          // This is the expected and normal case
          return;
        }
        VLOG(3) << it->second.nResponsesWaiting_ref().value()
                << " nodes never responded for scanId " << scanId;
        it->second.respId = ++scanRespCounter_;
        handleAllResponsesReceived(scanId, it->second);
      });

  // Store this scan structure
  scanStatus_.scans[scanId] = std::move(data);

  // Purge old scan results if we're already storing the max number
  if (scanStatus_.scans.size() > FLAGS_scan_max_results) {
    auto it = scanStatus_.scans.begin();
    // erase the first scan on the list if all responses were not received
    // or erase the scan with the smallest respId
    if (it->second.respId == 0) {
      // this case will not happen normally because a
      // scan that times out will have a respId
      scanStatus_.scans.erase(it);
    } else {
      // note that for loop has no body
      for (; it->second.respId != scanRespCounterLo_ &&
             it != scanStatus_.scans.end();
           ++it);
      CHECK(it != scanStatus_.scans.end());
      VLOG(3) << "Erasing scan with token " << it->first << " and respId "
              << scanRespCounterLo_;
      scanStatus_.scans.erase(it);
      scanRespCounterLo_++; // we erased the lowest so now lowest++
    }
    bumpCounter("e2e_controller.purged_scans");
  }
}

void
ScanApp::processStartScan(
    const std::string& senderApp, const thrift::StartScan& startScan) {
  switch (startScan.scanType) {
    case thrift::ScanType::PBF:
    case thrift::ScanType::IM:
    case thrift::ScanType::RTCAL:
      // Handle below
      break;
    case thrift::ScanType::CBF_TX:
    case thrift::ScanType::CBF_RX:
      // Check CBF scan parameters
      if (!startScan.mainTxNode_ref().has_value() ||
          !startScan.mainRxNode_ref().has_value() ||
          !startScan.auxTxNodes_ref().has_value() ||
          !startScan.auxRxNodes_ref().has_value()) {
        sendStartScanResp(senderApp, false, "No nodes specified for CBF scan");
        return;
      }
      if (startScan.auxTxNodes_ref().value().size() !=
          startScan.auxRxNodes_ref().value().size()) {
        sendStartScanResp(senderApp, false, "Different number of aux nodes");
        return;
      }
      if (startScan.auxTxPwrIndex_ref().has_value() &&
          (!startScan.txPwrIndex_ref().has_value() ||
           (startScan.auxTxPwrIndex_ref().value().size() !=
           startScan.auxTxNodes_ref().value().size()))) {
        sendStartScanResp(
            senderApp, false, "TX power specified for some but not all nodes");
        return;
      }

      // Determine whether to start a CBF scan or only set CBF config
      if (startScan.setConfig_ref().value_or(false)) {
        setCbfLinkConfig(senderApp, startScan);
      } else {
        groupCounter_++;
        initiateCbfScan(senderApp, startScan);
      }
      return;
    case thrift::ScanType::TOPO:
      // Start a topology scan
      groupCounter_++;
      initiateTopoScan(senderApp, startScan);
      return;
    case thrift::ScanType::TEST_UPD_AWV:
      // Do nothing, this can be initiated from r2d2 only
      return;
  }

  // Check PBF/RTCAL/IM scan parameters
  if (startScan.txNode_ref().has_value() != startScan.rxNodes_ref().has_value())
  {
    std::string error = "txNode and rxNodes should be either both present "
                        "or both absent. Not starting scan.";
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }
  if (startScan.beams_ref().has_value()) {
    if (!startScan.txNode_ref().has_value()) {
      std::string error = "beams can only be present if txNode is present";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    if (startScan.beams_ref().value().size() !=
        1 + startScan.rxNodes_ref().value().size()) {
      std::string error = "There must be as many beams as tx and rx nodes";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    for (const thrift::BeamIndices& b : startScan.beams_ref().value()) {
      if (b.low < 0 || b.low > kMaxBeamIdx || b.high < 0 ||
          b.high > kMaxBeamIdx || b.low > b.high) {
        std::string error = folly::sformat(
            "Beam indices should be within [0, {}]", kMaxBeamIdx);
        LOG(ERROR) << error;
        sendStartScanResp(senderApp, false, error);
        return;
      }
    }
  }
  if (startScan.scanType == thrift::ScanType::RTCAL) {
    if (startScan.scanMode != thrift::ScanMode::SELECTIVE &&
        startScan.scanMode != thrift::ScanMode::RELATIVE) {
      std::string error = "RTCAL supports only selective or relative mode";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    if (!startScan.bwgdLen_ref().has_value()) {
      std::string error = "RTCAL Selective/Relative mode needs a valid bwgdLen";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
  }
  if (startScan.bwgdLen_ref().has_value()) {
    // bwgdLen has to be a power of 2 between 2 and 64
    const auto x = startScan.bwgdLen_ref().value();
    if (((x & (x - 1)) != 0) || x < 2 || x > 64) {
      std::string error = "bwgdLen must be a power of 2 between 2 and 64";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
  }

  // Start PBF/RTCAL/IM scan on a selected node
  if (startScan.txNode_ref().has_value()) {
    groupCounter_++;
    initiateManualScan(senderApp, startScan);
    return;
  }

  // Start PBF/RTCAL/IM scan on full network
  const int firstScanId = scanCounter_ + 1;
  if (startScan.scanType == thrift::ScanType::PBF ||
      startScan.scanType == thrift::ScanType::RTCAL) {
    groupCounter_++;
    uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
    std::vector<thrift::StartScan> startScanConfig = {startScan};
    doPbfRtcalScan(startScanConfig, bwgdIdx);
  } else if (startScan.scanType == thrift::ScanType::IM) {
    groupCounter_++;
    uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
    doImScan(startScan, bwgdIdx);
  } else {
    sendStartScanResp(
        senderApp, false, "Bad scan type " + scanTypeToStr(startScan.scanType));
    return;
  }

  const int lastScanId = scanCounter_;
  std::string scanMsg = folly::sformat(
      "Scheduled {} scans {}-{} with groupId {}",
      scanTypeToStr(startScan.scanType),
      firstScanId,
      lastScanId,
      groupCounter_);
  sendStartScanResp(senderApp, true, scanMsg, firstScanId, lastScanId);
  eventClient_->logEventThrift(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_REQ,
      thrift::EventLevel::INFO,
      scanMsg,
      startScan);
}

void
ScanApp::sendStartScanResp(
    const std::string& senderApp,
    bool success,
    const std::string& message,
    const std::optional<int> token,
    const std::optional<int> lastToken) {
  thrift::StartScanResp startScanResp;
  startScanResp.success_ref() = success;
  startScanResp.message_ref() = message;
  if (token.has_value()) {
    startScanResp.token_ref() = token.value();
  }
  if (lastToken.has_value()) {
    startScanResp.lastToken_ref() = lastToken.value();
  }
  sendToCtrlApp(senderApp, thrift::MessageType::START_SCAN_RESP, startScanResp);
}

void
ScanApp::setCbfLinkConfig(
    const std::string& senderApp, const thrift::StartScan& startScan) {
  auto scanKey = getScanKey(
      startScan.scanType,
      startScan.mainTxNode_ref().value(),
      startScan.mainRxNode_ref().value());

  // Modify controller config
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  thrift::CbfConfig cbfConfig =
      deserializeCbfConfig(config.scanParams.cbfConfigJson);
  cbfConfig.config[scanKey] = startScan;
  config.scanParams.cbfConfigJson = serializeCbfConfig(cbfConfig);
  bool success = lockedE2EConfigWrapper->setConfigFromThrift(config);
  lockedE2EConfigWrapper.unlock(); // lockedE2EConfigWrapper -> NULL

  if (success) {
    VLOG(3) << "Set CBF config for key:" << scanKey
            << " numConfig:" << cbfConfig.config.size();
    sendStartScanResp(
        senderApp,
        true,
        folly::sformat("Set CBF config for key {}", scanKey));
  } else {
    sendStartScanResp(
        senderApp,
        false,
        folly::sformat("Failed to set CBF config for key {}", scanKey));
  }
}

std::string
ScanApp::serializeCbfConfig(const thrift::CbfConfig& config) const {
  return JsonUtils::serializeToJson(config);
}

thrift::CbfConfig
ScanApp::deserializeCbfConfig(const std::string& configJson) const {
  if (!configJson.empty()) {
    std::optional<thrift::CbfConfig> config =
        JsonUtils::deserializeFromJson<thrift::CbfConfig>(configJson);
    if (config.has_value()) {
      return config.value();
    } else {
      LOG(ERROR) << "Could not deserialize CBF config JSON";
    }
  }
  return thrift::CbfConfig();
}

uint64_t
ScanApp::doImScan(const thrift::StartScan& startScan, uint64_t bwgdIdx) {
  VLOG(2) << "Initiating IM scan: bwgdIdx:" << bwgdIdx;

  // Get polarities for all nodes from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities();

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  ScanScheduler scheduler(*lockedTopologyW);
  const std::vector<std::string> macs = scheduler.getAllMacs();

  for (const auto& schedGroup : scheduler.getSchedGroups()) {
    uint64_t bwgdEndMaxInGroup = bwgdIdx;
    for (const size_t txMacIdx : schedGroup) {
      const std::string txMac = macs[txMacIdx];
      const int scanId = scanCounter_ + 1;

      const std::vector<size_t> rxMacIdxsOrig =
          scheduler.getHearabilityNeighbors(txMacIdx);
      std::unordered_set<size_t> rxMacIdxs(
          rxMacIdxsOrig.begin(), rxMacIdxsOrig.end());

      // For the tx node and for CN nodes in the rx set, add all their adjacency
      // neighbors
      for (size_t n : rxMacIdxsOrig) {
        auto maybeNode = lockedTopologyW->getNodeByMac(macs[n]);
        if (maybeNode && maybeNode->node_type == thrift::NodeType::CN) {
          const std::vector<size_t> neighbors =
              scheduler.getAdjacencyNeighbors(n);
          rxMacIdxs.insert(neighbors.begin(), neighbors.end());
        }
      }
      const std::vector<size_t> neighbors =
          scheduler.getAdjacencyNeighbors(txMacIdx);
      rxMacIdxs.insert(neighbors.begin(), neighbors.end());
      // Remove txMacIdx itself from the rx set in case it made it there
      rxMacIdxs.erase(txMacIdx);
      if (rxMacIdxs.empty()) {
        VLOG(4) << "Skipping IM scan on txnode=" << txMac << " with no rxnodes";
        continue;
      }
      scanCounter_++;

      const std::vector<std::string> rxNodes =
          folly::gen::from(rxMacIdxs) |
          folly::gen::map([&](size_t neigh) { return macs[neigh]; }) |
          folly::gen::as<std::vector>();
      uint64_t actualBwgd = sendScanToTxAndRxNodes(
          txMac, rxNodes, scanId, bwgdIdx, startScan, nodePolarities);
      thrift::StartScan scan = startScan;
      scan.rxNodes_ref() = rxNodes;
      addScan(
          scanId,
          makeScanData(
              txMac, actualBwgd, scan, rxNodes.size() + 1, groupCounter_));

      const uint64_t bwgdEnd = actualBwgd + scanDurationBwgd(startScan);
      if (bwgdEndMaxInGroup < bwgdEnd) {
        bwgdEndMaxInGroup = bwgdEnd;
      }

      VLOG(4) << "IM Scan id=" << scanId << ", bwgd=" << actualBwgd
              << ", txnode=" << txMac << ", rxnodes="
              << (folly::gen::from(rxNodes) | folly::gen::unsplit(","));
    }
    bwgdIdx = bwgdEndMaxInGroup;
  }
  return bwgdIdx;
}

std::optional<thrift::Node>
ScanApp::getNodeByNameOrMac(
    const TopologyWrapper& topologyW,
    const std::string& nameOrMac,
    std::optional<bool>& nodeByName,
    std::string& error) {
  std::optional<thrift::Node> node;
  if (!nodeByName || !*nodeByName) {
    // Search by MAC
    node = topologyW.getNodeByMac(nameOrMac);
    if (node) {
      nodeByName = false;
      return node;
    }
  }
  if (!nodeByName || *nodeByName) {
    // Search by name
    node = topologyW.getNode(nameOrMac);
    if (node) {
      nodeByName = true;
      if (node->wlan_mac_addrs.size() <= 1) {
        return node;
      }
      // Node found by name, but has > 1 MACs
      error = folly::sformat(
          "Node {} has {} MACs (expected <=1)",
          nameOrMac,
          node->wlan_mac_addrs.size());
      return std::nullopt;
    }
  }
  error = folly::sformat("Node {} not found", nameOrMac);
  return std::nullopt;
}

std::string
ScanApp::getNodeMac(
    const std::optional<bool>& nodeByName,
    const thrift::Node& node,
    const std::string& nameOrMac) {
  if (!nodeByName) {
    // This shouldn't happen
    LOG(ERROR) << "Logic error, empty nodeByName for " << nameOrMac;
    return node.mac_addr;
  }
  if (*nodeByName) {
    if (node.wlan_mac_addrs.size() == 1) {
      return node.wlan_mac_addrs[0];
    } else if (node.wlan_mac_addrs.size() == 0) {
      return node.mac_addr;
    } else {
      // Shouldn't happen
      LOG(ERROR) << "Logic error, " << node.wlan_mac_addrs.size()
                 << " wlan macs for " << nameOrMac;
      return node.mac_addr;
    }
  } else {
    return nameOrMac;
  }
}

void
ScanApp::initiateManualScan(
    const std::string& senderApp, const thrift::StartScan& startScan) {
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities();

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  std::optional<bool> nodeByName = std::nullopt;
  std::string error;
  std::optional<thrift::Node> tx = getNodeByNameOrMac(
      *lockedTopologyW, startScan.txNode_ref().value_or(""), nodeByName, error);
  if (!tx) {
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }
  const std::string txMac =
      getNodeMac(nodeByName, *tx, startScan.txNode_ref().value_or(""));
  if (txMac.empty()) {
    error = folly::sformat(
        "Not starting manual scan for tx node '{}' with empty MAC", tx->name);
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }

  // Handle PBF/RTCAL scans
  if (startScan.scanType == thrift::ScanType::PBF ||
      startScan.scanType == thrift::ScanType::RTCAL) {
    if (!startScan.rxNodes_ref().has_value() ||
        startScan.rxNodes_ref().value().size() != 1) {
      error = folly::sformat(
          "{} scan requires a single rx node, {} given",
          scanTypeToStr(startScan.scanType),
          startScan.rxNodes_ref().has_value() ?
              startScan.rxNodes_ref().value().size() : 0);
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    std::optional<thrift::Node> rx = getNodeByNameOrMac(
        *lockedTopologyW, startScan.rxNodes_ref().value()[0], nodeByName,
        error);
    if (!rx) {
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    const std::string rxMac =
        getNodeMac(nodeByName, *rx, startScan.rxNodes_ref().value()[0]);
    if (rxMac.empty()) {
      error = folly::sformat(
          "Not starting manual PBF/RTCAL scan for rx node '{}' with empty MAC",
          rx->name);
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }

    std::optional<std::string> linkName =
        lockedTopologyW->getLinkName(tx->name, rx->name);
    lockedTopologyW.unlock(); // lockedTopologyW -> NULL
    if (!linkName) {
      error = folly::sformat(
          "Nodes {} and {} are not connected by a link", txMac, rxMac);
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    if (startScan.scanMode == thrift::ScanMode::SELECTIVE &&
        (isHybridPolarity(nodePolarities[txMac]) ||
         isHybridPolarity(nodePolarities[rxMac]))) {
      error = folly::sformat(
          "Can't run selective scan on hybrid nodes {} and/or {}",
          txMac,
          rxMac);
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }

    const int scanId = ++scanCounter_;
    const uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
    uint64_t actualBwgd = sendScanToTxAndRxNodes(
        txMac, {rxMac}, scanId, bwgdIdx, startScan, nodePolarities);
    addScan(
        scanId,
        makeScanData(
            txMac, actualBwgd, startScan, 2, groupCounter_, *nodeByName));
    VLOG(2) << "Scheduled " << scanTypeToStr(startScan.scanType)
            << " scan from " << txMac << " to " << rxMac << " at bwgd "
            << actualBwgd;

    std::string scanMsg = folly::sformat(
        "Scheduled {} scan {}", scanTypeToStr(startScan.scanType), scanId);
    sendStartScanResp(senderApp, true, scanMsg, scanId);
    eventClient_->logEventThrift(
        thrift::EventCategory::SCAN,
        thrift::EventId::SCAN_REQ,
        thrift::EventLevel::INFO,
        scanMsg,
        startScan);
    return;
  }

  // Handle IM scans
  if (startScan.scanType == thrift::ScanType::IM) {
    if (startScan.rxNodes_ref().value().empty()) {
      error = "IM scan requires a non-zero rx nodes.";
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return;
    }
    std::vector<std::string> rxMacs;
    for (const std::string& nodeMac : startScan.rxNodes_ref().value()) {
      std::optional<thrift::Node> rx =
          getNodeByNameOrMac(*lockedTopologyW, nodeMac, nodeByName, error);
      if (!rx) {
        LOG(ERROR) << error;
        sendStartScanResp(senderApp, false, error);
        return;
      }
      const std::string rxMac = getNodeMac(nodeByName, *rx, nodeMac);
      if (rxMac.empty()) {
        error = folly::sformat(
            "Not starting manual IM scan because rx node '{}' has empty MAC",
            rx->name);
        LOG(ERROR) << error;
        sendStartScanResp(senderApp, false, error);
        return;
      }
      rxMacs.push_back(rxMac);
    }
    lockedTopologyW.unlock(); // lockedTopologyW -> NULL
    if (rxMacs.empty()) {
      LOG(ERROR) << "Not running IM scan from " << txMac << " (no rx nodes)";
      return;
    }

    const int scanId = ++scanCounter_;
    const uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
    uint64_t actualBwgd = sendScanToTxAndRxNodes(
        txMac, rxMacs, scanId, bwgdIdx, startScan, nodePolarities);
    addScan(
        scanId,
        makeScanData(
            txMac,
            actualBwgd,
            startScan,
            startScan.rxNodes_ref().value().size() + 1,
            groupCounter_,
            *nodeByName));
    VLOG(2) << "Scheduled IM scan from " << txMac << " to "
            << startScan.rxNodes_ref().value().size() << " rx nodes at bwgd "
            << actualBwgd;

    std::string scanMsg = folly::sformat("Scheduled IM scan {}", scanId);
    sendStartScanResp(senderApp, true, scanMsg, scanId);
    eventClient_->logEventThrift(
        thrift::EventCategory::SCAN,
        thrift::EventId::SCAN_REQ,
        thrift::EventLevel::INFO,
        scanMsg,
        startScan);
    return;
  }

  // Unsupported scan type
  error = folly::sformat("Unimplemented scan type {}", (int)startScan.scanType);
  LOG(ERROR) << error;
  sendStartScanResp(senderApp, false, error);
}

uint64_t
ScanApp::sendScanToTxAndRxNodes(
    const std::string& txMac,
    const std::vector<std::string>& rxMacs,
    int scanId,
    uint64_t bwgdIdx,
    const thrift::StartScan& startScan,
    std::unordered_map<std::string, std::optional<thrift::PolarityType>>&
        nodePolarities) {
  if (rxMacs.empty()) {
    LOG(ERROR) << "sendScanToTxAndRxNodes(): rxMacs is empty!!";
    return bwgdIdx;
  }

  const auto txPolarity = nodePolarities[txMac];
  const auto rx0Polarity = nodePolarities[rxMacs[0]];

  // For PBF, a hybrid link is one where the tx or rx node is hybrid
  const bool isHybrid =
      startScan.scanType == thrift::ScanType::PBF &&
      (isHybridPolarity(txPolarity) || isHybridPolarity(rx0Polarity));

  // Adjust starting bwgd index accorting to scheduler slot map
  thrift::SlotPurpose purpose;
  switch (startScan.scanType) {
    case thrift::ScanType::PBF:
      purpose = isHybrid ? thrift::SlotPurpose::SP_HYBRID_PBF
                         : thrift::SlotPurpose::SP_PBF;
      break;
    case thrift::ScanType::RTCAL:
      purpose = thrift::SlotPurpose::SP_RTAC;
      break;
    case thrift::ScanType::IM:
      purpose = thrift::SlotPurpose::SP_IM;
      break;
    case thrift::ScanType::CBF_TX:
    case thrift::ScanType::CBF_RX:
      purpose = thrift::SlotPurpose::SP_NULLING;
      break;
    default:
      CHECK(false);
  }
  if (startScan.applyBwgdIdx_ref().has_value() &&
      startScan.cbfBeamIdx_ref().has_value()) {
    // Use BWGD index reserved during initial scan
    bwgdIdx = startScan.applyBwgdIdx_ref().value();
  } else {
    // Find next available BWGD index
    bwgdIdx = schedulerApp_.adjustBwgd(
        purpose,
        bwgdIdx,
        scanDurationBwgd(startScan) * (isHybrid ? 2 : 1),
        txMac,
        rxMacs);
  }

  thrift::ScanReq req;
  req.token = scanId;
  req.scanType_ref() = startScan.scanType;
  req.startBwgdIdx = bwgdIdx;
  if (startScan.scanType != thrift::ScanType::IM) {
    if (startScan.apply_ref().has_value()) {
      req.apply_ref() = startScan.apply_ref().value();
    }
  }
  req.scanMode = startScan.scanMode;
  req.bfScanInvertPolarity = false; // don't revert for tx node
  // req.routes = ; // don't set, meaning full scan
  if (startScan.beams_ref().has_value()) {
    req.beams_ref() = startScan.beams_ref().value()[0];
  }

  if ((startScan.scanMode == thrift::ScanMode::SELECTIVE) ||
      (startScan.scanMode == thrift::ScanMode::RELATIVE)) {
    if (startScan.subType_ref().has_value()) {
      req.subType_ref() = startScan.subType_ref().value();
    }
    if (startScan.bwgdLen_ref().has_value()) {
      req.bwgdLen_ref() = startScan.bwgdLen_ref().value();
    }
  }

  // Fill in and send to tx node
  if (startScan.scanType != thrift::ScanType::IM) {
    req.rxNodeMac_ref() = rxMacs[0];
  } else {
    req.rxNodeMac_ref() = "ff:ff:ff:ff:ff:ff";
  }
  if (startScan.txPwrIndex_ref().has_value()) {
    req.txPwrIndex_ref() = startScan.txPwrIndex_ref().value();
  }
  // For CBF
  if (startScan.nullAngle_ref().has_value()) {
    req.nullAngle_ref() = startScan.nullAngle_ref().value();
  }
  if (startScan.cbfBeamIdx_ref().has_value()) {
    req.cbfBeamIdx_ref() = startScan.cbfBeamIdx_ref().value();
  }
  if (startScan.scanType == thrift::ScanType::CBF_TX ||
      startScan.scanType == thrift::ScanType::CBF_RX) {
    bool mainIsTx = startScan.mainTxNode_ref().value() == txMac;
    if (!mainIsTx) {
      // They might be inequal because startScan.mainTxNode is a node name.
      // Convert txMac to a node name and compare
      auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
      std::optional<std::string> txNodeName =
          lockedTopologyW->getNodeNameByMac(txMac);
      std::string txNodeNameStr = startScan.mainTxNode_ref().has_value() ?
          startScan.mainTxNode_ref().value() : "";
      mainIsTx = txNodeName.value() == txNodeNameStr;

    }
    if (startScan.scanType == thrift::ScanType::CBF_TX) {
      if (mainIsTx) {
        req.subType_ref() = thrift::ScanSubType::TX_CBF_AGGRESSOR;
      } else {
        req.subType_ref() = thrift::ScanSubType::TX_CBF_VICTIM;
      }
    } else if (startScan.scanType == thrift::ScanType::CBF_RX) {
      if (mainIsTx) {
        req.subType_ref() = thrift::ScanSubType::RX_CBF_VICTIM;
      } else {
        req.subType_ref() = thrift::ScanSubType::RX_CBF_AGGRESSOR;
      }
    }
    if (startScan.bwgdLen_ref().has_value()) {
      req.bwgdLen_ref() = startScan.bwgdLen_ref().value();
    }
    if (!startScan.cbfBeamIdx_ref().has_value()) {
      // Don't send apply true without cbfBeamIdx
      req.apply_ref() = false;
    }
  }

  VLOG(4) << "Sending to tx node " << txMac << " at bwgd " << bwgdIdx
          << ", token=" << scanId;
  req.radioMac_ref() = txMac;
  scheduleSendToMinion(txMac, req);

  // Fill in and send to rx nodes
  req.rxNodeMac_ref().reset();
  req.txNodeMac_ref() = txMac;
  // Index in beams array. Starting from 1 because item 0 is for tx node
  size_t i = 1;
  for (const std::string& rxMac : rxMacs) {
    VLOG(4) << "Sending to rx node " << rxMac << " at bwgd " << bwgdIdx
            << ", token=" << scanId;
    const std::optional<thrift::PolarityType> rxPolarity =
        nodePolarities[rxMac];
    req.bfScanInvertPolarity =
        txPolarity && rxPolarity && *txPolarity == *rxPolarity;
    if (startScan.beams_ref().has_value()) {
      req.beams_ref() = startScan.beams_ref().value()[i];
      i++;
    }
    req.radioMac_ref() = rxMac;
    scheduleSendToMinion(rxMac, req);
  }
  return bwgdIdx;
}

void
ScanApp::processScanResp(
    const std::string& minion, thrift::ScanResp&& scanResp) {
  // Multi-radio nodes set the radio mac, old nodes don't, so use minion mac
  const std::string senderMac = scanResp.radioMac_ref().value_or(minion);
  const std::optional<std::string> nodeName =
      SharedObjects::getTopologyWrapper()->rlock()->getNodeNameByMac(senderMac);
  if (!nodeName) {
    LOG(ERROR) << "Unknown node " << senderMac;
    return;
  }
  auto it = scanStatus_.scans.find(scanResp.token);
  if (it == scanStatus_.scans.end()) {
    LOG(ERROR) << "Unexpected scan id " << scanResp.token << " reply from "
               << senderMac;
    return;
  }

  // If flag is set, convert MAC to node name for the sake of older scan result
  // parsing tools
  const std::string responseKey = (it->second.convertMacToName_ref().has_value()
      && it->second.convertMacToName_ref().value()) ? *nodeName : senderMac;

  if (it->second.responses.find(responseKey) != it->second.responses.end()) {
    LOG(ERROR) << "Unexpected second (or subsequent) reply from " << responseKey
               << " for scan id " << scanResp.token;
    return;
  }

  if (it->second.nResponsesWaiting_ref().value_or(0) == 0) {
    LOG(ERROR) << "Unexpected response: all nodes have already responded; "
               << "scan id " << scanResp.token << " reply from " << responseKey;
    return;
  }

  // Check if controller time is synchronized with node
  checkAndWarnTimeSkew(*nodeName, scanResp.curSuperframeNum / 16);

  if (it->second.respId != 0) {
    LOG(ERROR) << "Unexpected response after timeout: scan id "
               << scanResp.token << " reply from " << responseKey;
    return;
  }

  if (it->second.convertMacToName_ref().value_or(false)) {
    if (it->second.txNode == senderMac) {
      it->second.txNode = responseKey;
    }
    if (it->second.rxNodes_ref().has_value()) {
      for (std::string& r : it->second.rxNodes_ref().value()) {
        if (r == senderMac) {
          r = responseKey;
        }
      }
    }
  }

  it->second.responses[responseKey] = std::move(scanResp);

  if (--it->second.nResponsesWaiting_ref().value() != 0) {
    return;
  }

  // all responses have been received
  it->second.respId = ++scanRespCounter_;
  it->second.nResponsesWaiting_ref().reset(); // no need to report it
  it->second.convertMacToName_ref().reset();
  VLOG(3) << "Received all responses from token " << it->first
          << " (responseId = " << it->second.respId << ")";

  handleAllResponsesReceived(it->first, it->second);
}

void
ScanApp::handleAllResponsesReceived(
    int token, const thrift::ScanData& scanData) {
  // Record an event
  eventClient_->logEventDynamic(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_COMPLETE,
      thrift::EventLevel::INFO,
      folly::sformat(
          "{} scans completed for ID {}", scanTypeToStr(scanData.type), token),
      folly::dynamic::object("type", static_cast<int>(scanData.type))(
          "token", token));

  // Record the full scan result (NOTE: this can be very large!)
  {
    thrift::ScanResult scanResult;
    scanResult.token = token;
    scanResult.data = scanData;
    eventClient_->sendScanData(std::move(scanResult));
  }

  // Call scan-specific handlers
  switch (scanData.type) {
    case thrift::ScanType::PBF:
      processPbfResp(token, scanData);
      break;
    case thrift::ScanType::RTCAL:
      break;
    case thrift::ScanType::IM:
      if (scanData.mode == thrift::ScanMode::RELATIVE) {
        processRelImResp(token, scanData);
        checkRelImGroupComplete();
      } else {
        processImResp(token, scanData);
      }
      break;
    case thrift::ScanType::CBF_RX:
    case thrift::ScanType::CBF_TX:
      processCbfResp(token, scanData);
      break;
    case thrift::ScanType::TOPO:
      processTopoResp(token, scanData);
      break;
    case thrift::ScanType::TEST_UPD_AWV:
      break;
  }
}

void
ScanApp::generateCbfConfig(
    const thrift::ScanType& scanType, thrift::CbfConfig& cbfConfig) {
  // For RX nulling:
  //
  // For each wireless link (VTX --> VRX) and for each link with same polarity
  // (ATX --> ARX), check if: (1) current beam state and TX power are known and
  // if (2) IM scan results for the ATX --> VRX pair are available and (3) if
  // interference was detected for the current beams. If so, add to interference
  // set.
  //
  // Sort interference set in descending order of INR at current TX power in two
  // groups: those where ATX site != VTX site (candidates for RX nulling), and
  // those where ATX site == VTX site (not candidate for RX nulling, but may
  // need to be included in scan to avoid bursty/unknown interference during
  // scan).
  //
  // Select the strongest interferer from each P2MP sector and remove others
  // from the interference set. Since only one link on a P2MP sector can TX or
  // RX at a time, only a single link can participate in the nulling scan.
  //
  // Filter interference set by thresholds for (1) including aux link in scan as
  // a nulling candidate and (2) including aux link in scan but not as a nulling
  // candidate (e.g. for ATX slot exclusion). If no nulling candidates remain,
  // the victim link isn't eligible for RX nulling, so continue to next victim
  // link. If multiple nulling candidates remain, the first one is the primary
  // nulling candidate (primary ATX) and others are secondary nulling candidates
  // (secondary ATX).
  //
  // Set VTX and primary ATX power for target SNR and INR, adhering to min/max
  // power constraints and min target SIR. Set ATX power for secondary ATX links
  // relative primary ATX power trying to keep same relative power between ATX
  // nodes. Set ATX power for other nodes to min power to simulate slot
  // exclusion.
  //
  // Use primary ATX null angle for nulling config.
  //
  // For TX nulling:
  //
  // TX nulling is done similarly but the order or victim/aggressor loops are
  // reversed, i.e. for each aggressor link, search for all victim links and
  // determine if aggressor link is a candidate for TX nulling.
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities();

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  // For each main link (victim link for RX CBF, aggressor link for TX CBF)
  for (const thrift::Link& mainLink : lockedTopologyW->getAllLinks()) {
    if (mainLink.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }
    for (bool revMain : {false, true}) {
      std::string mainTx, mainRx;
      if (revMain) {
        mainTx = mainLink.z_node_mac;
        mainRx = mainLink.a_node_mac;
      } else {
        mainTx = mainLink.a_node_mac;
        mainRx = mainLink.z_node_mac;
      }
      std::string mainKey = getLinkKey(mainTx, mainRx);
      auto linkStateIt = rfState_.link.find(mainKey);
      if (linkStateIt == rfState_.link.end()) {
        continue;
      }
      const thrift::RfLinkState& mainState = linkStateIt->second;
      const std::optional<thrift::Node> mainTxNode =
          lockedTopologyW->getNodeByMac(mainTx);
      const std::optional<thrift::Node> mainRxNode =
          lockedTopologyW->getNodeByMac(mainRx);
      const std::optional<thrift::PolarityType> mainTxNodePolarity =
          nodePolarities[mainTx];
      const std::optional<thrift::PolarityType> mainRxNodePolarity =
          nodePolarities[mainRx];
      if (!mainTxNodePolarity || !mainRxNodePolarity) {
        VLOG(4) << "No polarity: mainTx:" << mainTx << " mainRx:" << mainRx;
        continue;
      }

      auto imDataIt = rfState_.im.find(mainKey);
      if (imDataIt == rfState_.im.end()) {
        VLOG(5) << "No IM data for key: " << mainKey;
        continue;
      }
      const thrift::RfImData& mainImData = imDataIt->second;
      int32_t beams = getBeamKey(mainState.txBeam, mainState.rxBeam);
      auto imRoutesIt = mainImData.routes.find(beams);
      if (imRoutesIt == mainImData.routes.end()) {
        VLOG(4)
            << "No routes for current beams in IM data for main link: mainTx:"
            << mainTx << " mainRx:" << mainRx << " txBeam:" << (beams & 0xff)
            << " rxBeam:" << (beams >> 16);
        continue;
      }
      float mainOffsetDb = imRoutesIt->second;

      // For each aux link (aggressor link for RX CBF, victim link for TX CBF)
      std::set<CbfAuxConfig, CbfAuxCompare> auxSet;
      bool haveValidAux = false;
      for (const thrift::Link& auxLink : lockedTopologyW->getAllLinks()) {
        if (auxLink.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        for (bool revAux : {false, true}) {
          std::string auxTx, auxRx;
          if (revAux) {
            auxTx = auxLink.z_node_mac;
            auxRx = auxLink.a_node_mac;
          } else {
            auxTx = auxLink.a_node_mac;
            auxRx = auxLink.z_node_mac;
          }
          if (auxTx == mainTx || auxRx == mainRx) {
            continue;
          }
          linkStateIt = rfState_.link.find(getLinkKey(auxTx, auxRx));
          if (linkStateIt == rfState_.link.end()) {
            VLOG(5) << "No link state for auxTx:" << auxTx
                    << " auxRx:" << auxRx;
            continue;
          }
          const thrift::RfLinkState& auxState = linkStateIt->second;
          const std::optional<thrift::Node> auxTxNode =
              lockedTopologyW->getNodeByMac(auxTx);
          const std::optional<thrift::Node> auxRxNode =
              lockedTopologyW->getNodeByMac(auxRx);
          const std::optional<thrift::PolarityType> auxTxNodePolarity =
              nodePolarities[auxTx];
          const std::optional<thrift::PolarityType> auxRxNodePolarity =
              nodePolarities[auxRx];
          if (!auxTxNodePolarity || !auxRxNodePolarity) {
            VLOG(4) << "No polarity: auxTx:" << auxTx << " auxRx:" << auxRx;
            continue;
          }
          if (auxTxNodePolarity != mainTxNodePolarity ||
              auxRxNodePolarity != mainRxNodePolarity) {
            continue;
          }

          const thrift::RfLinkState& victimState =
              (scanType == thrift::ScanType::CBF_RX) ? mainState : auxState;
          const thrift::RfLinkState& aggState =
              (scanType == thrift::ScanType::CBF_RX) ? auxState : mainState;

          // Links have same polarity: check for aggressor->victim interference
          std::string inrKey = (scanType == thrift::ScanType::CBF_RX)
                                   ? getLinkKey(auxTx, mainRx)
                                   : getLinkKey(mainTx, auxRx);
          imDataIt = rfState_.im.find(inrKey);
          if (imDataIt == rfState_.im.end()) {
            continue;
          }
          const thrift::RfImData& inrImData = imDataIt->second;
          if (inrImData.bestTxBeam == kInvalidBeamIdx ||
              inrImData.bestRxBeam == kInvalidBeamIdx) {
            continue;
          }
          beams = getBeamKey(aggState.txBeam, victimState.rxBeam);
          imRoutesIt = inrImData.routes.find(beams);
          if (imRoutesIt == inrImData.routes.end()) {
            VLOG(5) << "No routes in IM data for current beams: key:" << inrKey
                    << " txBeam:" << (beams & 0xff)
                    << " rxBeam:" << (beams >> 16);
            continue;
          }
          float inrOffsetDb = imRoutesIt->second;

          // Found agressor causing measurable interference with current beams
          float inrNulling = inrOffsetDb + aggState.txPower +
                             FLAGS_cbf_inr_tx_pwr_offset_nulling;
          float inrAuxSet = inrOffsetDb + aggState.txPower +
                            FLAGS_cbf_inr_tx_pwr_offset_aux_set;
          VLOG(4) << scanTypeToStr(scanType)
                  << " interference: mainTx:" << mainTx << " mainRx:" << mainRx
                  << " auxTx:" << auxTx << " auxRx:" << auxRx
                  << " atxBeam:" << aggState.txBeam
                  << " vrxBeam:" << victimState.rxBeam
                  << " inrCurrent:" << inrOffsetDb + aggState.txPower
                  << " inrNulling:" << inrNulling << " inrAuxSet:" << inrAuxSet;

          // Add to aux set if INR exceeds threshold
          if (inrNulling < FLAGS_cbf_inr_thresh_nulling &&
              inrAuxSet < FLAGS_cbf_inr_thresh_aux_set) {
            continue;
          }
          // RX CBF: VTX site == ATX site, TX CBF: VRX site == ARX site
          bool sameSite = false;
          bool nullingEligible = false;
          int16_t nullAngle = 0;
          if (scanType == thrift::ScanType::CBF_RX) {
            sameSite = mainTxNode && auxTxNode &&
                mainTxNode->site_name == auxTxNode->site_name;
            nullAngle = inrImData.bestRxBeam;
          } else {
            sameSite = mainRxNode && auxRxNode &&
                mainRxNode->site_name == auxRxNode->site_name;
            nullAngle = inrImData.bestTxBeam;
          }
          if (sameSite && !FLAGS_cbf_allow_same_site_aux) {
            VLOG(5) << "Skipping same site aux link: auxTx:" << auxTx
                    << " auxRx:" << auxRx;
            continue;
          }
          if (sameSite || inrNulling < FLAGS_cbf_inr_thresh_nulling) {
            nullingEligible = false;
          } else {
            nullingEligible = true;
            haveValidAux = true;
          }
          CbfAuxConfig config = {auxTx,
                                 auxRx,
                                 nullAngle,
                                 aggState.txPower,
                                 inrOffsetDb,
                                 nullingEligible};
          auxSet.insert(std::move(config));
        }
      }
      if (!haveValidAux) {
        continue;
      }

      // Remove all but strongest interferer from each unique auxTx/auxRx site
      std::unordered_set<std::string> auxTxNodes;
      std::unordered_set<std::string> auxRxNodes;
      for (auto auxSetIt = auxSet.begin(); auxSetIt != auxSet.end();) {
        if (auxTxNodes.find(auxSetIt->txNode) != auxTxNodes.end() ||
            auxRxNodes.find(auxSetIt->rxNode) != auxRxNodes.end()) {
          auxSetIt = auxSet.erase(auxSetIt);
        } else {
          auxTxNodes.insert(auxSetIt->txNode);
          auxRxNodes.insert(auxSetIt->rxNode);
          ++auxSetIt;
        }
      }
      VLOG(5) << "After P2MP pruning: " << auxSet.size() << " aux configs";

      auto clamp = [](float txPower) -> int16_t {
        return std::max(
            1, std::min(FLAGS_cbf_max_tx_pwr, (int)std::round(txPower)));
      };

      // TX power assignment (meant for RX CBF but could be used for TX CBF)
      int16_t mainTxPwr = clamp(FLAGS_cbf_target_snr - mainOffsetDb);
      float mainSnr = mainOffsetDb + (float)mainTxPwr;
      float targetInr =
          std::min(FLAGS_cbf_target_inr, mainSnr - FLAGS_cbf_min_target_sir);
      float auxPwrOffsetDb = 0;
      std::vector<int16_t> auxTxPwr;
      for (const CbfAuxConfig& config : auxSet) {
        if (auxTxPwr.empty()) {
          // Aux set is sorted so first item is the primary aux link
          // Handle primary aux power saturation when computing aux power offset
          auxPwrOffsetDb =
              clamp(targetInr - config.trxOffsetDb) - config.txPower;
        }
        if (config.nullingEligible) {
          auxTxPwr.push_back(clamp(config.txPower + auxPwrOffsetDb));
        } else {
          auxTxPwr.push_back(0);
        }
      }

      bool setScanPower = false;
      if ((scanType == thrift::ScanType::CBF_RX &&
           FLAGS_cbf_rx_pwr_assignment_enable) ||
          (scanType == thrift::ScanType::CBF_TX &&
           FLAGS_cbf_tx_pwr_assignment_enable)) {
        setScanPower = true;
      }

      // Generate CBF config for link
      thrift::StartScan startScan;
      startScan.scanType = scanType;
      startScan.scanMode = thrift::ScanMode::FINE;
      startScan.apply_ref() = FLAGS_cbf_config_apply;
      startScan.mainTxNode_ref() = mainTx;
      startScan.mainRxNode_ref() = mainRx;
      startScan.auxTxNodes_ref() = std::vector<std::string>();
      startScan.auxRxNodes_ref() = std::vector<std::string>();
      for (const CbfAuxConfig& config : auxSet) {
        if (!startScan.nullAngle_ref().has_value()) {
          startScan.nullAngle_ref() = config.nullAngle;
        }
        startScan.auxTxNodes_ref().value().push_back(config.txNode);
        startScan.auxRxNodes_ref().value().push_back(config.rxNode);
      }
      if (setScanPower) {
        startScan.txPwrIndex_ref() = mainTxPwr;
        startScan.auxTxPwrIndex_ref() = std::move(auxTxPwr);
      }

      // Add to CBF config
      std::string scanKey = getScanKey(scanType, mainTx, mainRx);
      cbfConfig.config[scanKey] = std::move(startScan);
    }
  }
}

void
ScanApp::processPbfResp(int scanId, const thrift::ScanData& scanData) {
  thrift::RfLinkState linkState;
  std::string txNode, rxNode;

  // Check for complete response from TX and RX node
  for (const auto& it : scanData.responses) {
    if (it.second.status_ref().value() != thrift::ScanFwStatus::COMPLETE ||
        !it.second.azimuthBeam_ref().has_value()) {
      VLOG(5) << "Skipping PBF scan processing: token:" << scanId
              << " node:" << it.first << " status:" << (int)it.second.status;
      return;
    }
    if (it.first == scanData.txNode) {
      if (!it.second.txPwrIndex_ref().has_value()) {
        VLOG(4) << "Skipping PBF scan processing: TX node power missing: token:"
                << scanId << " node:" << it.first;
        return;
      }
      linkState.txPower = it.second.txPwrIndex_ref().value();
      linkState.txBeam = it.second.azimuthBeam_ref().value();
      txNode = it.first;
    } else {
      linkState.rxBeam = it.second.azimuthBeam_ref().value();
      rxNode = it.first;
    }
  }

  if (txNode.empty() || rxNode.empty()) {
    VLOG(4) << "Skipping PBF scan processing: token:" << scanId
            << " haveTx:" << txNode.empty() << " haveRx:" << rxNode.empty();
    return;
  }

  // Update scan link state
  rfState_.link[getLinkKey(txNode, rxNode)] = std::move(linkState);
  rfState_.dirty = true;
}

void
ScanApp::processImResp(int scanId, const thrift::ScanData& scanData) {
  const std::string& txNode = scanData.txNode;

  // Check for valid response from TX node
  const auto& it = scanData.responses.find(txNode);
  if (it == scanData.responses.end()) {
    VLOG(4) << "Skipping IM scan processing: TX node response missing: token:"
            << scanId << " node:" << txNode;
    return;
  }
  const thrift::ScanResp& txResp = it->second;
  if (txResp.status != thrift::ScanFwStatus::COMPLETE) {
    VLOG(4) << "Skipping IM scan processing: TX node error response: token:"
            << scanId << " node:" << txNode << " status:" << (int)txResp.status;
    return;
  }
  if (!txResp.txPwrIndex_ref().has_value()) {
    VLOG(4) << "Skipping IM scan processing: TX node power missing: token:"
            << scanId << " node:" << txNode;
    return;
  }

  // Process each RX response
  float txPower;
  txPower = txResp.txPwrIndex_ref().value();
  for (const auto& it : scanData.responses) {
    thrift::RfImData data;
    if (it.first == txNode) {
      continue;
    }
    const std::string& rxNode = it.first;
    if (it.second.status != thrift::ScanFwStatus::COMPLETE) {
      VLOG(5) << "Skipping IM scan RX response: token:" << scanId
              << " node:" << rxNode << " status:" << (int)it.second.status;
      continue;
    }

    // Aggregate route responses for same tx,rx beams
    std::vector<float> snr[kMaxBeamImScan + 1][kMaxBeamImScan + 1];
    for (const thrift::RouteInfo& route : it.second.routeInfoList) {
      int16_t txBeam = route.route.tx;
      int16_t rxBeam = route.route.rx;
      if (txBeam > kMaxBeamImScan || rxBeam > kMaxBeamImScan) {
        LOG(ERROR) << "Unexpected route in IM scan: token:" << scanId
                   << " txNode:" << txNode << " rxNode:" << rxNode
                   << " txBeam:" << txBeam << " rxBeam:" << rxBeam;
        continue;
      }
      snr[txBeam][rxBeam].push_back(route.snrEst);
    }

    // Average routes and find best route
    float bestSnr = kScanSnrMin;
    data.scanId = scanId;
    data.scanPower = txPower;
    data.bestTxBeam = kInvalidBeamIdx;
    data.bestRxBeam = kInvalidBeamIdx;
    for (int16_t tx = 0; tx <= kMaxBeamImScan; tx++) {
      for (int16_t rx = 0; rx <= kMaxBeamImScan; rx++) {
        const std::vector<float>& vec = snr[tx][rx];
        if (vec.empty()) {
          continue;
        }
        int32_t beams = getBeamKey(tx, rx);
        float avg = std::accumulate(vec.begin(), vec.end(), 0.0) / vec.size();
        // Store scaled pathloss as offset between TX power and SNR
        data.routes[beams] = avg - txPower;
        if (avg > bestSnr) {
          bestSnr = avg;
          data.bestTxBeam = tx;
          data.bestRxBeam = rx;
        }
      }
    }

    // Skip update if old scan at higher power with more routes is available
    std::string key = getLinkKey(txNode, rxNode);
    if (!FLAGS_scan_topo_always_use_new_data) {
      auto it = rfState_.im.find(key);
      if (it != rfState_.im.end()) {
        if (it->second.scanPower > data.scanPower &&
            it->second.routes.size() > data.routes.size()) {
          VLOG(4) << "Skipping IM scan update for key:" << key
                  << " oldToken: " << it->second.scanId
                  << " oldScanPower:" << it->second.scanPower
                  << " oldNumRoutes:" << it->second.routes.size()
                  << " newToken: " << data.scanId
                  << " newScanPower:" << data.scanPower
                  << " newNumRoutes:" << data.routes.size();
          continue;
        }
      }
    }

    // Update RF state
    rfState_.im[key] = std::move(data);
    rfState_.dirty = true;
  }
}

std::unordered_map<int16_t, std::unordered_set<std::string>>
ScanApp::getBeamMap(
    const TopologyWrapper& topologyW,
    std::vector<thrift::BeamInfo> beamInfoList) const {
  std::unordered_map<int16_t, std::unordered_set<std::string>> beamToNodes;
  for (const auto& beamInfo : beamInfoList) {
    // Verify mac exists
    std::optional<thrift::Node> node = topologyW.getNodeByMac(beamInfo.addr);
    if (!node) {
      continue;
    }
    beamToNodes[beamInfo.beam].insert(beamInfo.addr);
  }
  return beamToNodes;
}

void
ScanApp::checkRelImGroupComplete() {
  if (relImGroupIdx_ && relImRemaining_ == 0) {
    setLaTpcParams();
    // Clear group index to indicate processing for the group is finished
    relImGroupIdx_ = 0;
  }
}

void
ScanApp::processRelImResp(int scanId, const thrift::ScanData& scanData) {
  const std::string& txNode = scanData.txNode;

  // Check if scan is part of current periodic combined scan group
  if (scanData.groupId_ref().has_value() && scanData.groupId_ref().value() ==
      relImGroupIdx_)
  {
    relImRemaining_--;
  }

  // Skip processing if apply flag isn't set
  if (!scanData.apply_ref().value_or(false)) {
    return;
  }

  // Check for valid response from TX node
  const auto& it = scanData.responses.find(txNode);
  if (it == scanData.responses.end()) {
    VLOG(4) << "Skipping IM scan processing: TX node response missing: token:"
            << scanId << " node:" << txNode;
    return;
  }
  const thrift::ScanResp& txResp = it->second;
  if (txResp.status != thrift::ScanFwStatus::COMPLETE) {
    VLOG(4) << "Skipping IM scan processing: TX node error response: token:"
            << scanId << " node:" << txNode << " status:" << (int)txResp.status;
    return;
  }
  if (!txResp.txPwrIndex_ref().has_value()) {
    VLOG(4) << "Skipping IM scan processing: TX node power missing: token:"
            << scanId << " node:" << txNode;
    return;
  }
  if (!txResp.beamInfoList_ref().has_value()) {
    VLOG(4) << "Skipping IM scan processing: beamInfoList missing: token:"
            << scanId << " node:" << txNode;
    return;
  }
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  const auto txBeamMap =
      getBeamMap(*lockedTopologyW, txResp.beamInfoList_ref().value());

  // Initialize data map for the scan
  thrift::RfRelImData relImData;
  relImData.scanId = scanId;
  relImData.scanPower = txResp.txPwrIndex_ref().value();
  std::unordered_map<std::string, thrift::RfRelImData> data;
  for (const auto& txBeamIt : txBeamMap) {
    for (const auto& txLinkRxNode : txBeamIt.second) {
      const auto txKey = getLinkKey(txNode, txLinkRxNode);
      data[txKey] = relImData;
    }
  }

  // Process each RX response
  for (const auto& it : scanData.responses) {
    if (it.first == txNode) {
      continue;
    }
    const std::string& rxNode = it.first;
    if (it.second.status != thrift::ScanFwStatus::COMPLETE) {
      VLOG(5) << "Skipping rel IM scan RX response: token:" << scanId
              << " node:" << rxNode << " status:" << (int)it.second.status;
      continue;
    }
    if (it.second.routeInfoList.empty()) {
      continue;
    }
    if (!it.second.beamInfoList_ref().has_value()) {
      VLOG(4) << "Skipping rel IM scan RX response: token:" << scanId
              << " node:" << rxNode << " beamInfoList missing!";
      continue;
    }
    const auto rxBeamMap =
        getBeamMap(*lockedTopologyW, it.second.beamInfoList_ref().value());

    // Aggregate route responses for same tx,rx beams
    std::unordered_map<int16_t, std::unordered_map<int16_t, std::vector<float>>>
        snr;
    for (const auto& txBeamIt : txBeamMap) {
      snr[txBeamIt.first] = {};
      for (const auto& rxBeamIt : rxBeamMap) {
        snr[txBeamIt.first][rxBeamIt.first] = {};
      }
    }
    for (const thrift::RouteInfo& route : it.second.routeInfoList) {
      int16_t txBeam = route.route.tx;
      int16_t rxBeam = route.route.rx;
      if (snr.find(txBeam) == snr.end()) {
        VLOG(4) << "IM scan TX beam not in beamInfoList! token:" << scanId
                << " txBeam:" << txBeam;
        continue;
      }
      if (snr[txBeam].find(rxBeam) == snr[txBeam].end()) {
        VLOG(4) << "IM scan RX beam not in beamInfoList! token:" << scanId
                << " rxBeam:" << rxBeam;
        continue;
      }
      snr[txBeam][rxBeam].push_back(route.snrEst);
    }

    // Average routes
    for (const auto& txBeamIt : txBeamMap) {
      for (const auto& rxBeamIt : rxBeamMap) {
        const std::vector<float>& vec = snr[txBeamIt.first][rxBeamIt.first];
        if (vec.empty()) {
          continue;
        }
        float avg = std::accumulate(vec.begin(), vec.end(), 0.0) / vec.size();
        // Add route for all (TX link, RX link) pairs that use these tx,rx beams
        for (const auto& txLinkRxNode : txBeamIt.second) {
          const std::string txKey = getLinkKey(txNode, txLinkRxNode);
          for (const auto& rxLinkTxNode : rxBeamIt.second) {
            const std::string rxKey = getLinkKey(rxLinkTxNode, rxNode);
            // Store scaled pathloss as offset between TX power and SNR
            data[txKey].routes[rxKey] = avg - txResp.txPwrIndex_ref().value();
          }
        }
      }
    }
  }
  lockedTopologyW.unlock(); // lockedTopologyW -> NULL

  // Update RF state
  for (const auto& it : data) {
    rfState_.relIm[it.first] = std::move(it.second);
  }
}

void
ScanApp::processCbfResp(int scanId, const thrift::ScanData& scanData) {
  if (!scanData.mainRxNode_ref().has_value()) {
    LOG(ERROR) << "CBF error: No mainRxNode specified.";
    return;
  }
  if (!scanData.mainTxNode_ref().has_value()) {
    LOG(ERROR) << "CBF error: No mainTxNode specified.";
    return;
  }
  // Check if CBF beam selection required for this scan
  if (scanData.cbfBeamIdx_ref().has_value()) {
    VLOG(5) << "Nothing to do for complete CBF scan response: token:" << scanId
            << " apply:" << scanData.apply_ref().value_or(false)
            << " cbfBeamIdx:" << scanData.cbfBeamIdx_ref().value_or(false);
    return;
  } else {
    VLOG(5) << "Received CBF scan response from all nodes: token:" << scanId
            << " mainRxNode:" << scanData.mainRxNode_ref().value_or("");
  }

  // Check status of response from all nodes
  auto check = [&](const std::string& node) {
    auto it = scanData.responses.find(node);
    if (it == scanData.responses.end()) {
      LOG(ERROR) << "CBF error: token:" << scanId << " no response from "
                 << node;
      return false;
    }
    if (it->second.status != thrift::ScanFwStatus::COMPLETE) {
      VLOG(5) << "CBF error response: token:" << scanId << " node:" << node
              << " status:" << (int)it->second.status;
      return false;
    }
    return true;
  };

  // Verify that mainRx, mainTx, auxRx[0], auxTx[0] completed the scan
  // successfully. If not, print a message and return.
  // If auxRx[1:end] or auxTx[1:end] did not complete the scan successfully,
  // print a message, but don't abort, since their result is not used below.
  bool res = true, first = true;
  res &= check(scanData.mainRxNode_ref().value());
  res &= check(scanData.mainTxNode_ref().value());
  if (scanData.auxTxNodes_ref().has_value()) {
    for (const std::string& node : scanData.auxTxNodes_ref().value()) {
      bool ok = check(node);
      if (first) {
        res &= ok;
        first = false;
      }
    }
  }
  first = true;
  if (scanData.auxRxNodes_ref().has_value()) {
    for (const std::string& node : scanData.auxRxNodes_ref().value()) {
      bool ok = check(node);
      if (first) {
        res &= ok;
        first = false;
      }
    }
  }
  if (!res) {
    return;
  }

  // Nulling beam selection for TX and RX nulling are done similarly:
  //
  // Determine candidate beams that satisfy:
  // - Victim RX packet count at least 50% of ref beam packet count and victim
  // SINR increases by at least cbf_sinr_change_thresh dB for first victim link
  // - Main RX packet count at least 75% of ref beam packet count and main RX
  // SNR decreases by at most cbf_snr_change_thresh dB
  //
  // The nulling beam is selected from candidate beams by choosing the one with
  // the largest improvement in SINR for the first victim link. For RX nulling
  // there is only one victim link. For TX nulling with multiple victim links
  // the algorithm below only considers SINR for the first victim link.

  // Aggregate route responses
  std::vector<float> mainSnr[kCbfNumBeams];
  std::vector<float> victimSinr[kCbfNumBeams];
  auto process = [&](const std::vector<thrift::RouteInfo>& routes,
                     std::vector<float>* snr,
                     int16_t pktIdx) {
    for (const thrift::RouteInfo& route : routes) {
      if (route.packetIdx != pktIdx) {
        continue;
      }
      int16_t sweepIdx = route.sweepIdx;
      int16_t beam = (scanData.type == thrift::ScanType::CBF_TX)
                         ? route.route.tx
                         : route.route.rx;
      if (beam >= kCbfBeamOffset && beam < kCbfBeamOffset + kCbfNumBeams &&
          beam != sweepIdx + kCbfBeamOffset) {
        LOG(ERROR) << "CBF error: skipping invalid route: token:" << scanId
                   << " sweepIdx:" << sweepIdx << " beam:" << beam;
        continue;
      }
      snr[sweepIdx].push_back(route.snrEst);
    }
  };

  // Use packet index 1 for main RX to measure main lobe loss (SNR)
  // Use packet index 0 for victim RX to measure interference suppression (SINR)
  const thrift::ScanResp& mainResp =
      scanData.responses.find(scanData.mainRxNode_ref().value())->second;
  const thrift::ScanResp& auxResp =
      scanData.responses.find(scanData.auxRxNodes_ref().value()[0])->second;
  process(mainResp.routeInfoList, mainSnr, 1);
  if (scanData.type == thrift::ScanType::CBF_TX) {
    process(auxResp.routeInfoList, victimSinr, 0);
  } else {
    process(mainResp.routeInfoList, victimSinr, 0);
  }

  // Determine thresholds from reference beam metrics
  int bestBeamIdx = kCbfRefBeamIdx;
  size_t victimCntThresh, mainCntThresh;
  float refSinr, bestSinr, refSnr, bestSnr, sinrThresh, snrThresh;

  auto cbfVal = [](const std::vector<float>& vec) {
    // Currently just take the average, in the future a percentile could be used
    if (vec.empty()) {
      return kScanSnrMin;
    } else {
      return std::accumulate(vec.begin(), vec.end(), 0.0) / vec.size();
    }
  };

  refSinr = bestSinr = cbfVal(victimSinr[kCbfRefBeamIdx]);
  refSnr = bestSnr = cbfVal(mainSnr[kCbfRefBeamIdx]);
  sinrThresh = refSinr + FLAGS_cbf_sinr_change_thresh;
  snrThresh = refSnr + FLAGS_cbf_snr_change_thresh;

  auto scale = [](size_t ref_cnt, double cnt_scale) {
    return std::max(1.0, std::floor(ref_cnt * cnt_scale));
  };
  victimCntThresh =
      scale(victimSinr[kCbfRefBeamIdx].size(), FLAGS_cbf_victim_count_scale);
  mainCntThresh =
      scale(mainSnr[kCbfRefBeamIdx].size(), FLAGS_cbf_main_count_scale);

  VLOG(5) << "CBF thresholds: token:" << scanId
          << " victimCntThresh:" << victimCntThresh
          << " mainCntThresh:" << mainCntThresh << " sinrThresh:" << sinrThresh
          << " snrThresh:" << snrThresh;

  // Loop through results and pick best beam that meets thresholds
  for (int16_t beam = 0; beam < kCbfNumBeams; beam++) {
    float sinrVal = cbfVal(victimSinr[beam]);
    float snrVal = cbfVal(mainSnr[beam]);
    float sinrApprox = sinrVal;
    if (scanData.type == thrift::ScanType::CBF_TX) {
      // For TX CBF the aggressor will increase TX power to compensate for any
      // main lobe loss, so victim SINR measurement needs to be adjusted
      // accordingly. Since victim SNR/INR isn't known, just offset SINR by main
      // lobe loss, which gives a lower bound on victim SINR after aggressor TX
      // power increase due to main lobe loss. Still use sinrVal for logging.
      sinrApprox = sinrVal - (refSnr - snrVal);
    }
    if (victimSinr[beam].size() >= victimCntThresh && sinrApprox > sinrThresh &&
        mainSnr[beam].size() >= mainCntThresh && snrVal > snrThresh) {
      sinrThresh = sinrApprox;
      bestBeamIdx = beam;
      bestSinr = sinrVal;
      bestSnr = snrVal;
    }
  }

  std::string scanKey = getScanKey(
      scanData.type,
      scanData.mainTxNode_ref().value_or(""),
      scanData.mainRxNode_ref().value_or(""));

  if (bestBeamIdx == kCbfRefBeamIdx && !FLAGS_scan_cbf_use_ref_beam) {
    VLOG(4) << "CBF result: token:" << scanId << " key:" << scanKey
            << " no nulling beam found";
    return;
  }

  VLOG(4) << "CBF result: token:" << scanId << " key:" << scanKey
          << " bestBeamIdx:" << bestBeamIdx << " refSinr:" << refSinr
          << " refSnr:" << refSnr << " bestSinr:" << bestSinr
          << " bestSnr:" << bestSnr << " sinrChange:" << bestSinr - refSinr
          << " snrChange:" << bestSnr - refSnr;

  if (!scanData.apply_ref().value_or(false)) {
    return;
  }

  // Generate scan command to apply new beam
  thrift::StartScan startScan;
  startScan.scanType = scanData.type;
  startScan.scanMode = thrift::ScanMode::FINE;
  startScan.startTime = std::chrono::system_clock::to_time_t(
      std::chrono::system_clock::now() +
      FLAGS_scans_start_time_offset_s);
  if (scanData.mainTxNode_ref().has_value()) {
    startScan.mainTxNode_ref() = scanData.mainTxNode_ref().value();
  }
  if (scanData.mainRxNode_ref().has_value()) {
    startScan.mainRxNode_ref() = scanData.mainRxNode_ref().value();
  }
  if (scanData.nullAngle_ref().has_value()) {
    startScan.nullAngle_ref() = scanData.nullAngle_ref().value();
  }
  if (scanData.applyBwgdIdx_ref().has_value()) {
    startScan.applyBwgdIdx_ref() = scanData.applyBwgdIdx_ref().value();
  }
  // Set apply false and specify cbfBeamIdx so firmware will apply CBF beam only
  // if CBF scan has been done since last beam reset
  startScan.apply_ref() = false;
  startScan.cbfBeamIdx_ref() = bestBeamIdx;

  // Get node objects
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  std::optional<thrift::Node> mainTxNode =
      lockedTopologyW->getNode(startScan.mainTxNode_ref().value_or(""));
  std::optional<thrift::Node> mainRxNode =
      lockedTopologyW->getNode(startScan.mainRxNode_ref().value_or(""));
  auto allNodes = lockedTopologyW->getAllNodes();
  lockedTopologyW.unlock(); // lockedTopologyW -> NULL
  if (!mainTxNode.has_value() || !mainRxNode.has_value()) {
    VLOG(3) << "Skipping CBF apply: node not found: token:" << scanId;
    return;
  }

  const std::string mainTx = startScan.mainTxNode_ref().value_or("");
  const std::string mainRx = startScan.mainRxNode_ref().value_or("");

  // Get polarities for all nodes from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities(allNodes);

  // Send beam index to main link nodes
  const int scanIdApply = ++scanCounter_;
  const uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
  const uint64_t actualBwgd = sendScanToTxAndRxNodes(
      mainTx, {mainRx}, scanIdApply, bwgdIdx, startScan, nodePolarities);
  // Set apply true in scan data so scan status shows expected outcome
  startScan.apply_ref() = true;
  addScan(
      scanIdApply,
      makeScanData(mainTx, actualBwgd, startScan, 2,
          scanData.groupId_ref().value_or(0)));
  VLOG(3) << "Scheduled CBF beam apply: scanId:" << scanId
          << " scanIdApply:" << scanIdApply << " key:" << scanKey
          << " cbfBeamIdx:" << startScan.cbfBeamIdx_ref().value()
          << " bwgd:" << actualBwgd << " sinrGain:" << bestSinr - refSinr;
}

void
ScanApp::processTopoResp(int scanId, const thrift::ScanData& scanData) {
  // Forward results to TopologyBuilderApp
  thrift::ScanResult scanResult;
  scanResult.token = scanId;
  scanResult.data = scanData;
  sendToCtrlApp(
      E2EConsts::kTopologyBuilderAppCtrlId,
      thrift::MessageType::TOPOLOGY_SCAN_RESULT,
      scanResult);
}

void
ScanApp::processGetScanStatus(
    const std::string& senderApp, const thrift::GetScanStatus& getScanStatus) {
  if (!getScanStatus.isConcise &&
      !getScanStatus.tokenFrom_ref().has_value() &&
      !getScanStatus.tokenTo_ref().has_value() &&
      !getScanStatus.respIdFrom_ref().has_value() &&
      !getScanStatus.respIdTo_ref().has_value()) {
    // Full results
    sendToCtrlApp(senderApp, thrift::MessageType::SCAN_STATUS, scanStatus_);
    return;
  }

  if (!getScanStatus.tokenFrom_ref().has_value() && getScanStatus
      .tokenTo_ref().has_value()) {
    sendE2EAck(
        senderApp, false, "tokenFrom is required if tokenTo is supplied");
    return;
  }

  if (!getScanStatus.respIdFrom_ref().has_value() != !getScanStatus
      .respIdTo_ref().has_value()) {
    sendE2EAck(senderApp, false, "respIdFrom and respIdTo are both required");
    return;
  }

  if (getScanStatus.tokenFrom_ref().has_value() && getScanStatus
          .respIdFrom_ref().has_value()) {
    sendE2EAck(senderApp, false, "can not specify both token and respId range");
    return;
  }

  // if respIdFrom/To is set, use that instead of tokens
  // if the respIdFrom > 1 larger than the largest available respId or
  // if respIdTo < smallest respId, return the smallest available respId
  // (this is done to handle E2E and/or requester restarts)
  int respIdFromLoc = 0, respIdToLoc = 0;
  if (getScanStatus.respIdFrom_ref().has_value() && getScanStatus.respIdTo_ref()
      .has_value()) {
    respIdFromLoc = getScanStatus.respIdFrom_ref().value();
    respIdToLoc = getScanStatus.respIdTo_ref().value();

    if (respIdToLoc < scanRespCounterLo_ ||
        respIdFromLoc > (scanRespCounter_ + 1)) {
      // if requester and E2E are out of sync (either because E2E or requester
      // restarted for example) then return the smallest available response ID
      // so that they can get back in sync
      respIdToLoc = scanRespCounterLo_;
      respIdFromLoc = scanRespCounterLo_;
    }
    VLOG(5) << "respIds requested " << getScanStatus.respIdFrom_ref().value()
            << ":" << getScanStatus.respIdTo_ref().value() << " actual range "
            << respIdFromLoc << ":" << respIdToLoc;
  }

  // Returns true if the token or respId is within the requested range
  // (or equal to tokenFrom if there is no tokenTo)
  auto filterByTokenOrRespId =
      [&getScanStatus, &respIdFromLoc, &respIdToLoc](
          const std::pair<int, thrift::ScanData>& pair) {
        if (getScanStatus.respIdFrom_ref().has_value()) {
          int respId = pair.second.respId;
          return respId >= respIdFromLoc && respId <= respIdToLoc;
        } else if (!getScanStatus.tokenFrom_ref().has_value()) {
          return true;
        } else if (!getScanStatus.tokenTo_ref().has_value()) {
          return pair.first == getScanStatus.tokenFrom_ref().value();
        } else {
          return pair.first >= getScanStatus.tokenFrom_ref().value() &&
                 pair.first <= getScanStatus.tokenTo_ref().value();
        }
      };

  auto createConciseResp =
      [](const std::pair<std::string, thrift::ScanResp>& resp) {
        thrift::ScanResp scanResp;
        scanResp.token = resp.second.token;
        scanResp.curSuperframeNum = resp.second.curSuperframeNum;
        if (resp.second.txPwrIndex_ref().has_value()) {
          scanResp.txPwrIndex_ref() = resp.second.txPwrIndex_ref().value();
        }
        scanResp.status = resp.second.status;
        if (resp.second.azimuthBeam_ref().has_value()) {
          scanResp.azimuthBeam_ref() = resp.second.azimuthBeam_ref().value();
        }
        if (resp.second.oldBeam_ref().has_value()) {
          scanResp.oldBeam_ref() = resp.second.oldBeam_ref().value();
        }
        if (resp.second.newBeam_ref().has_value()) {
          scanResp.newBeam_ref() = resp.second.newBeam_ref().value();
        }
        return std::make_pair(resp.first, scanResp);
      };

  // if isConcise: Remove the RSSI and SNR measurements, retaining only the
  // metadata.  Useful to get a concise view of the scans performed so far.
  // the response is in the form {token: ScanData}
  auto mapConcise = [&getScanStatus, &createConciseResp](
                        const std::pair<int, thrift::ScanData>& pair) {
    if (!getScanStatus.isConcise) {
      return pair;
    }
    auto emptyResponses =
        folly::gen::from(pair.second.responses) |
        folly::gen::map(createConciseResp) |
        folly::gen::as<std::unordered_map<std::string, thrift::ScanResp>>();
    thrift::ScanData scanData;
    scanData.txNode = pair.second.txNode;
    scanData.startBwgdIdx = pair.second.startBwgdIdx;
    scanData.type = pair.second.type;
    if (pair.second.subType_ref().has_value()) {
      scanData.subType_ref() = pair.second.subType_ref().value();
    }
    scanData.mode = pair.second.mode;
    scanData.responses_ref() = std::move(emptyResponses);
    if (pair.second.apply_ref().has_value()) {
      scanData.apply_ref() = pair.second.apply_ref().value();
    }
    if (pair.second.nResponsesWaiting_ref().has_value()) {
        scanData.nResponsesWaiting_ref() =
            pair.second.nResponsesWaiting_ref().value();
    }
    scanData.respId = pair.second.respId;
    return std::make_pair(pair.first, std::move(scanData));
  };

  std::map<int, thrift::ScanData> result;
  result = folly::gen::from(scanStatus_.scans) |
           folly::gen::filter(filterByTokenOrRespId) |
           folly::gen::map(mapConcise) |
           folly::gen::as<std::map<int, thrift::ScanData>>();
  thrift::ScanStatus scanStatus;
  scanStatus.scans = std::move(result);
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::SCAN_STATUS,
      scanStatus);
}

void
ScanApp::processResetScanStatus(const std::string& senderApp) {
  // Clear all
  scanStatus_.scans.clear();
  scanRespCounterLo_ = scanRespCounter_ + 1;
  sendE2EAck(senderApp, true, "Removed all tokens");
}

void
ScanApp::processGetCbfConfig(
    const std::string& senderApp,
    const thrift::GetCbfConfig& /* getCbfConfig */) {
  std::string cbfConfigJson = SharedObjects::getE2EConfigWrapper()
                                  ->rlock()
                                  ->getConfig()
                                  ->scanParams.cbfConfigJson;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::CBF_CONFIG,
      deserializeCbfConfig(cbfConfigJson));
}

void
ScanApp::processSetCbfConfig(const std::string& senderApp) {
  if (rfState_.im.empty()) {
    sendE2EAck(
        senderApp, false, "Failed to update CBF config (no IM scan data).");
    return;
  } else {
    sendE2EAck(senderApp, true, "Updating CBF config from scan data.");
  }
  setCbfConfig();
}

void
ScanApp::processResetCbfConfig(const std::string& senderApp) {
  // Modify controller config
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  config.scanParams.cbfConfigJson = "";
  bool success = lockedE2EConfigWrapper->setConfigFromThrift(config);
  lockedE2EConfigWrapper.unlock(); // lockedE2EConfigWrapper -> NULL

  if (success) {
    sendE2EAck(senderApp, true, "Reset CBF config for all links.");
  } else {
    sendE2EAck(senderApp, false, "Failed to reset CBF config.");
  }
}

void
ScanApp::setCbfConfig() {
  // Don't update CBF config if scan data not available
  if (rfState_.im.empty()) {
    VLOG(3) << "Not updating CBF config: haveImState:" << !rfState_.im.empty()
            << " haveLinkState:" << !rfState_.link.empty();
    return;
  }

  // If link state is empty, use IM topology data to populate beams
  if (FLAGS_cbf_use_im_for_link_state) {
    setLinkStateFromImData();
  }

  // Generate new CBF config from scan data
  thrift::CbfConfig cbfConfig;
  generateCbfConfig(thrift::ScanType::CBF_RX, cbfConfig);
  generateCbfConfig(thrift::ScanType::CBF_TX, cbfConfig);

  VLOG(3) << "RF state: numLinkState:" << rfState_.link.size()
          << " numImData:" << rfState_.im.size();

  // Overwrite controller config
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  config.scanParams.cbfConfigJson = serializeCbfConfig(cbfConfig);
  bool success = lockedE2EConfigWrapper->setConfigFromThrift(config);
  lockedE2EConfigWrapper.unlock(); // lockedE2EConfigWrapper -> NULL

  if (success) {
    VLOG(3) << "Generated CBF config: numConfig:" << cbfConfig.config.size();
    rfState_.dirty = false;
  } else {
    LOG(ERROR) << "Error writing CBF config to controller config! numConfig:"
               << cbfConfig.config.size();
  }
}

void
ScanApp::setLinkStateFromImData() {
  // Loop through all links, if link state unknown try to populate using IM data
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  for (const thrift::Link& link : lockedTopologyW->getAllLinks()) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }
    for (bool revLink : {false, true}) {
      const std::string txNode = revLink ? link.z_node_mac : link.a_node_mac;
      const std::string rxNode = revLink ? link.a_node_mac : link.z_node_mac;
      std::string linkKey = getLinkKey(txNode, rxNode);
      if (rfState_.link.find(linkKey) != rfState_.link.end()) {
        // Already have link state
        continue;
      }
      auto it = rfState_.im.find(linkKey);
      if (it == rfState_.im.end()) {
        VLOG(5) << "No IM data for link in topology: txNode:" << txNode
                << " rxNode:" << rxNode;
        continue;
      }
      const thrift::RfImData& imData = it->second;

      // All link state entries should have valid beams
      if (imData.bestTxBeam == kInvalidBeamIdx ||
          imData.bestRxBeam == kInvalidBeamIdx) {
        continue;
      }

      thrift::RfLinkState linkState;
      linkState.txBeam = imData.bestTxBeam;
      linkState.rxBeam = imData.bestRxBeam;
      linkState.txPower = FLAGS_cbf_max_tx_pwr;
      rfState_.link[linkKey] = std::move(linkState);
    }
  }
  VLOG(5) << "Generated link state from IM topo data: numState:"
          << rfState_.link.size();
}

void
ScanApp::getTrxOffset(
    const std::string& tx_node,
    const std::string& rx_node,
    std::unordered_map<std::string, float>& trxOffset) {
  std::string linkKey = getLinkKey(tx_node, rx_node);
  if (rfState_.relIm.find(linkKey) == rfState_.relIm.end()) {
    VLOG(5) << "No rel IM data for link in topology: " << linkKey;
    trxOffset[linkKey] = kInvalidTrxOffset;
    return;
  }
  thrift::RfRelImData& imData = rfState_.relIm[linkKey];
  if (imData.routes.find(linkKey) == imData.routes.end()) {
    VLOG(5) << "No route in rel IM data for link in topology: " << linkKey;
    trxOffset[linkKey] = kInvalidTrxOffset;
    return;
  }
  trxOffset[linkKey] = imData.routes[linkKey];
}

void
ScanApp::setLaTpcParams() {
  thrift::CentralLaTpcConfig laTpcConfig = SharedObjects::getE2EConfigWrapper()
                                               ->rlock()
                                               ->getConfig()
                                               ->scanParams.centralLaTpcConfig;

  VLOG(5) << "laTpcConfig: maxMcsAutoEnable:" << laTpcConfig.maxMcsAutoEnable
          << " maxMcsLow:" << laTpcConfig.maxMcsLow
          << " maxMcsHigh:" << laTpcConfig.maxMcsHigh
          << " maxMcsInrLimit:" << laTpcConfig.maxMcsInrLimit
          << " maxMcsInrAlpha:" << laTpcConfig.maxMcsInrAlpha
          << " maxMcsTxPowerMin:" << laTpcConfig.maxMcsTxPowerMin
          << " maxMcsTxPowerMax:" << laTpcConfig.maxMcsTxPowerMax;

  LOCK_TOPOLOGY_AND_CONFIG(lockedTopologyW, rlock, lockedConfigHelper, wlock);

  // Get polarity for all radios from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      radioPolarities;
  for (const auto& node : lockedTopologyW->getAllNodes()) {
    for (const auto& macAddr : node.wlan_mac_addrs) {
      radioPolarities[macAddr] =
          lockedConfigHelper->getRadioPolarity(node.name, macAddr, false);
    }
  }

  // Get TX-RX coupling for each direction of each link in topology
  std::unordered_map<std::string, float> trxOffset;
  for (const thrift::Link& link : lockedTopologyW->getAllLinks()) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }
    getTrxOffset(link.a_node_mac, link.z_node_mac, trxOffset);
    getTrxOffset(link.z_node_mac, link.a_node_mac, trxOffset);
  }

  // Loop through all links and check for cross-link interference from an
  // aggressor link (ATX-ARX) to a victim link (VTX-VRX). Update max MCS for
  // aggressor links based on INR limit for each victim link.
  for (const thrift::Link& atxLink : lockedTopologyW->getAllLinks()) {
    if (atxLink.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }
    // For each direction of the link
    for (bool a2z : {true, false}) {
      std::string atxNode, arxNode;
      std::string atxMac, arxMac;
      if (a2z) {
        // A --> Z direction of link
        atxNode = atxLink.z_node_name;
        arxNode = atxLink.a_node_name;
        atxMac = atxLink.z_node_mac;
        arxMac = atxLink.a_node_mac;
      } else {
        // Z --> A direction of link
        atxNode = atxLink.a_node_name;
        arxNode = atxLink.z_node_name;
        atxMac = atxLink.a_node_mac;
        arxMac = atxLink.z_node_mac;
      }
      std::string linkKey = getLinkKey(atxMac, arxMac);
      if (trxOffset[linkKey] == kInvalidTrxOffset) {
        // No IM scan results for this link --> don't update laMaxMcs config
        continue;
      }
      float atxArxOffset = trxOffset[linkKey];
      thrift::RfRelImData& imData = rfState_.relIm[linkKey];

      const std::optional<thrift::PolarityType> atxPolarity =
          radioPolarities[atxMac];

      // Loop through candidate victim links and decrease max MCS for aggressor
      // link until INR threshold met for each victim link or estimated agressor
      // power saturates at min power
      int maxMcs = laTpcConfig.maxMcsHigh;
      for (const thrift::Link& vrxLink : lockedTopologyW->getAllLinks()) {
        if (vrxLink.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        // Determine which direction of vrxLink may be subject to
        // interference from ATX, if any
        std::string vtxNode, vrxNode;
        std::string vtxMac, vrxMac;
        if (PolarityHelper::isValidLinkPolarity(
                atxPolarity, radioPolarities[vrxLink.z_node_mac])) {
          vtxNode = vrxLink.a_node_name;
          vrxNode = vrxLink.z_node_name;
          vtxMac = vrxLink.a_node_mac;
          vrxMac = vrxLink.z_node_mac;
        } else if (PolarityHelper::isValidLinkPolarity(
                       atxPolarity, radioPolarities[vrxLink.a_node_mac])) {
          vtxNode = vrxLink.z_node_name;
          vrxNode = vrxLink.a_node_name;
          vtxMac = vrxLink.z_node_mac;
          vrxMac = vrxLink.a_node_mac;
        } else {
          VLOG(3) << "Unknown polarity (or hybrid-hybrid) for " << vrxLink.name;
          continue;
        }
        if (atxNode == vtxNode || arxNode == vrxNode) {
          // P2MP --> no interference
          continue;
        }
        linkKey = getLinkKey(vtxMac, vrxMac);
        if (imData.routes.find(linkKey) == imData.routes.end()) {
          continue;
        }
        float atxVrxOffset = imData.routes[linkKey];
        float vtxVrxOffset = trxOffset[linkKey];

        // Determine INR limit for victim link
        float inrLimit = laTpcConfig.maxMcsInrLimit;
        if (vtxVrxOffset != kInvalidTrxOffset) {
          float txPwrHeadroom = laTpcConfig.maxMcsTxPowerMax + vtxVrxOffset -
                                kMcsToSnr[laTpcConfig.maxMcsHigh];
          txPwrHeadroom = std::max(txPwrHeadroom, (float)0);
          inrLimit += laTpcConfig.maxMcsInrAlpha * txPwrHeadroom;
        }

        // Decrease max MCS until INR limit satisfied or MCS/power saturates
        float txPwrEst, inrEst;
        while (maxMcs > laTpcConfig.maxMcsLow) {
          txPwrEst = std::ceil(kMcsToSnr[maxMcs] - atxArxOffset);
          txPwrEst = std::max(
              (float)laTpcConfig.maxMcsTxPowerMin,
              std::min((float)laTpcConfig.maxMcsTxPowerMax, txPwrEst));
          inrEst = txPwrEst + atxVrxOffset;
          if (inrEst <= inrLimit || txPwrEst == laTpcConfig.maxMcsTxPowerMin) {
            break;
          }
          maxMcs--;
        }
      }
      std::string errorMsg;
      lockedConfigHelper->setLinkMaxMcs(
          atxNode, atxMac, arxMac, maxMcs, errorMsg);
      VLOG(5) << "Updated laMaxMcs: atxNode:" << atxNode << " arxMac:" << arxMac
              << " laMaxMcs:" << maxMcs << " errorMsg:" << errorMsg;
    }
  }
  VLOG(5) << "Updated LA/TPC config for all links";
}

void
ScanApp::processGetRfState(const std::string& senderApp) {
  sendToCtrlApp(senderApp, thrift::MessageType::RF_STATE, rfState_);
}

void
ScanApp::processSetRfState(
    const std::string& senderApp, const thrift::SetRfState& setRfState) {
  if (setRfState.rfState_ref().has_value()) {
    rfState_ = setRfState.rfState_ref().value();
    rfState_.dirty = true;
    sendE2EAck(senderApp, true, "Set RF state.");
  } else if (setRfState.scanStatus_ref().has_value()) {
    sendE2EAck(senderApp, true, "Setting RF state from scan data.");
    for (const auto& it : setRfState.scanStatus_ref().value().scans) {
      switch (it.second.type) {
        case thrift::ScanType::PBF:
          processPbfResp(it.first, it.second);
          break;
        case thrift::ScanType::IM:
          if (it.second.mode == thrift::ScanMode::RELATIVE) {
            processRelImResp(it.first, it.second);
          } else {
            processImResp(it.first, it.second);
          }
          break;
        default:
          break;
      }
    }
  } else {
    sendE2EAck(senderApp, false, "No RF state or scan data given.");
  }
}

void
ScanApp::processResetRfState(const std::string& senderApp) {
  rfState_.im.clear();
  rfState_.link.clear();
  rfState_.relIm.clear();
  sendE2EAck(senderApp, true, "Reset RF state.");
}

void
ScanApp::processSetLaTpcParams(const std::string& senderApp) {
  setLaTpcParams();
  sendE2EAck(senderApp, true, "Set LA/TPC params from RF state.");
}

void
ScanApp::imScanStart() {
  uint64_t bwgdIdx =
      TimeUtils::currentBwgdIdx(FLAGS_scheduled_scans_start_time_offset_s);

  if (bwgdIdx < nextBwgdIdxIm_) {
    // Last scan hasn't finished, skip this cycle
    VLOG(3) << "Skipping periodic IM scan: bwgdIdx " << bwgdIdx
            << " < nextBwgdIdxIm_ " << nextBwgdIdxIm_;
    return;
  }

  groupCounter_++;

  const int firstScanId = scanCounter_ + 1;
  thrift::StartScan startScan;
  startScan.scanType = thrift::ScanType::IM;
  startScan.scanMode = thrift::ScanMode::FINE;
  nextBwgdIdxIm_ = doImScan(startScan, bwgdIdx);

  VLOG(2) << "Finished scheduling IM scans: startBwgdIdx:" << bwgdIdx
          << " endBwgdIdx:" << nextBwgdIdxIm_;

  const int lastScanId = scanCounter_;
  std::string scanMsg = folly::sformat(
      "Scheduled periodic IM scans {}-{} with groupId {}",
      firstScanId,
      lastScanId,
      groupCounter_);
  eventClient_->logEventThrift(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_REQ,
      thrift::EventLevel::INFO,
      scanMsg,
      startScan);
}

void
ScanApp::processGetScanSchedule(
    const std::string& senderApp,
    const thrift::GetScanSchedule& /* getScanSchedule */) {
  thrift::ScanSchedule scanSchedule = SharedObjects::getE2EConfigWrapper()
      ->rlock()->getConfig()->scanParams.scanSchedule;
  thrift::GetScanScheduleResp getScanScheduleResp;
  getScanScheduleResp.scanSchedule = scanSchedule;
  getScanScheduleResp.nextBwgdIdx = (int64_t)nextBwgdIdx_;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::SCAN_SCHEDULE,
      getScanScheduleResp);
}

void
ScanApp::processSetScanSchedule(
    const std::string& senderApp, const thrift::ScanSchedule& setScanSchedule) {
  if (!setScanSchedule.imScanTimeoutSec_ref().has_value() &&
      !setScanSchedule.combinedScanTimeoutSec_ref().has_value()) {
    sendE2EAck(senderApp, false, "No timeout value given");
    return;
  }

  // Modify controller config
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  thrift::ScanSchedule& scanSchedule = config.scanParams.scanSchedule;
  if (setScanSchedule.imScanTimeoutSec_ref().has_value()) {
    scanSchedule.imScanTimeoutSec_ref() =
        setScanSchedule.imScanTimeoutSec_ref().value();
  }
  if (setScanSchedule.combinedScanTimeoutSec_ref().has_value()) {
    scanSchedule.combinedScanTimeoutSec_ref() =
        setScanSchedule.combinedScanTimeoutSec_ref().value();
    if (setScanSchedule.combinedScanTimeoutSec_ref().value() > 0) {
      scanSchedule.pbfEnable = setScanSchedule.pbfEnable;
      scanSchedule.rtcalEnable = setScanSchedule.rtcalEnable;
      scanSchedule.cbfEnable = setScanSchedule.cbfEnable;
      scanSchedule.imEnable = setScanSchedule.imEnable;
    } else {
      scanSchedule.pbfEnable = false;
      scanSchedule.rtcalEnable = false;
      scanSchedule.cbfEnable = false;
      scanSchedule.imEnable = false;
    }
  }
  lockedE2EConfigWrapper->setConfigFromThrift(config);
  lockedE2EConfigWrapper.unlock(); // lockedE2EConfigWrapper -> NULL

  // Send immediate ACK since the actual operation -- specifically
  // combinedScanStart() -- could take a while to complete
  // NOTE: Proceeding even if writing the new controller config file failed...
  sendE2EAck(senderApp, true, "Scan schedule set");

  if (FLAGS_scan_disable_periodic) {
    return;
  }

  // Timeout values...
  //   > 0 : schedule scan at x
  //  == 0 : run one-time scan now (combined scans only)
  //   < 0 : cancel scheduled scan
  if (setScanSchedule.imScanTimeoutSec_ref().has_value()) {
    if (setScanSchedule.imScanTimeoutSec_ref().value() > 0) {
      VLOG(2) << "Scheduling IM scans every "
              << scanSchedule.imScanTimeoutSec_ref().value() << " seconds";
      imScanTimeout_->scheduleTimeout(
          std::chrono::seconds(setScanSchedule.imScanTimeoutSec_ref().value()),
          true /* isPeriodic */);
    } else {
      if (imScanTimeout_->isScheduled()) {
        imScanTimeout_->cancelTimeout();
      }
    }
  }
  if (setScanSchedule.combinedScanTimeoutSec_ref().has_value()) {
    if (setScanSchedule.combinedScanTimeoutSec_ref().value() > 0) {
      VLOG(2) << "Scheduling combined scans every "
              << scanSchedule.combinedScanTimeoutSec_ref().value()
              << " seconds";
      combinedScanTimeout_->scheduleTimeout(
          std::chrono::seconds(setScanSchedule.combinedScanTimeoutSec_ref()
              .value()),
          true /* isPeriodic */);
    } else {
      if (combinedScanTimeout_->isScheduled()) {
        combinedScanTimeout_->cancelTimeout();
      }
      if (setScanSchedule.combinedScanTimeoutSec_ref().value() == 0) {
        // Start a scan using the given settings
        combinedScanStart(setScanSchedule);
      }
    }
  }
}

void
ScanApp::processScanConfigUpdated() {
  if (!FLAGS_scan_disable_periodic) {
    updateScanTimers();
  }
}

void
ScanApp::updateScanTimers() {
  // Update scan timers
  // NOTE: This will reset all current timers even if the values haven't changed
  thrift::ScanSchedule scanSchedule = SharedObjects::getE2EConfigWrapper()
      ->rlock()->getConfig()->scanParams.scanSchedule;
  if (scanSchedule.imScanTimeoutSec_ref().has_value() &&
      scanSchedule.imScanTimeoutSec_ref().value() > 0) {
    VLOG(2) << "Scheduling IM scans every "
            << scanSchedule.imScanTimeoutSec_ref().value() << " seconds";
    imScanTimeout_->scheduleTimeout(
        std::chrono::seconds(scanSchedule.imScanTimeoutSec_ref().value()),
        true /* isPeriodic */);
  } else {
    if (imScanTimeout_->isScheduled()) {
      imScanTimeout_->cancelTimeout();
    }
  }
  if (scanSchedule.combinedScanTimeoutSec_ref().has_value() &&
      scanSchedule.combinedScanTimeoutSec_ref().value() > 0) {
    VLOG(2) << "Scheduling combined scans every "
            << scanSchedule.combinedScanTimeoutSec_ref().value() << " seconds";
    combinedScanTimeout_->scheduleTimeout(
        std::chrono::seconds(scanSchedule.combinedScanTimeoutSec_ref().value()),
        true /* isPeriodic */);
  } else {
    if (combinedScanTimeout_->isScheduled()) {
      combinedScanTimeout_->cancelTimeout();
    }
  }
}

void
ScanApp::initiateCbfScan(
    const std::string& senderApp, const thrift::StartScan& origStartScan) {
  thrift::StartScan startScan = origStartScan;

  // Acquire a read lock here due to locking in the anonymous functions below.
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  std::optional<bool> nodeByName = std::nullopt;

  // Helper functions
  auto getNode = [&senderApp, &lockedTopologyW, &nodeByName, this](
                     const std::string& name) {
    std::string error;
    std::optional<thrift::Node> node =
        getNodeByNameOrMac(*lockedTopologyW, name, nodeByName, error);
    if (!node) {
      LOG(ERROR) << error;
      sendStartScanResp(senderApp, false, error);
      return node;
    }
    return node;
  };
  auto checkLink = [&senderApp, &lockedTopologyW, this](
                       const thrift::Node& a, const thrift::Node& b) {
    std::optional<std::string> linkName =
        lockedTopologyW->getLinkName(a.name, b.name);
    if (!linkName) {
      sendStartScanResp(
          senderApp, false, folly::sformat("No link {}-{}", a.name, b.name));
      return false;
    }
    return true;
  };

  // Check startScan parameters
  std::optional<thrift::Node> mainTx;
  std::optional<thrift::Node> mainRx;
  if (!(mainTx = getNode(startScan.mainTxNode_ref().value_or(""))) ||
      !(mainRx = getNode(startScan.mainRxNode_ref().value_or(""))) ||
      !checkLink(mainTx.value(), mainRx.value())) {
    return;
  }
  std::string mainTxMac =
      getNodeMac(nodeByName, *mainTx, startScan.mainTxNode_ref().value());
  if (mainTxMac.empty()) {
    std::string error = folly::sformat(
        "Not starting CBF scan for mainTxNode '{}' with empty MAC",
        mainTx->name);
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }
  std::string mainRxMac =
      getNodeMac(nodeByName, *mainRx, startScan.mainRxNode_ref().value());
  if (mainRxMac.empty()) {
    std::string error = folly::sformat(
        "Not starting CBF scan for mainRxNode '{}' with empty MAC",
        mainRx->name);
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }
  if (startScan.bwgdLen_ref() && startScan.bwgdLen_ref().value() < 4) {
    sendStartScanResp(
        senderApp, false, "Minimum scan duration for CBF is 4 BWGD");
    return;
  }
  std::vector<thrift::Node> auxTx, auxRx;
  std::vector<std::string> auxTxMacs, auxRxMacs;
  if (startScan.auxTxNodes_ref().has_value()) {
    for (const std::string& n : startScan.auxTxNodes_ref().value()) {
      std::optional<thrift::Node> node = getNode(n);
      if (!node) {
        return;
      }
      std::string auxTxMac = getNodeMac(nodeByName, *node, n);
    if (auxTxMac.empty()) {
        std::string error = folly::sformat(
            "Not starting CBF scan for auxTxNode '{}' with empty MAC",
            node->name);
        LOG(ERROR) << error;
        sendStartScanResp(senderApp, false, error);
        return;
      }
      auxTxMacs.push_back(auxTxMac);
      auxTx.push_back(std::move(*node));
    }
  }
  if (startScan.auxRxNodes_ref().has_value()) {
    for (const std::string& n : startScan.auxRxNodes_ref().value()) {
      std::optional<thrift::Node> node = getNode(n);
      if (!node) {
        return;
      }
      std::string auxRxMac = getNodeMac(nodeByName, *node, n);
      if (auxRxMac.empty()) {
        std::string error = folly::sformat(
            "Not starting CBF scan for auxRxNode '{}' with empty MAC",
            node->name);
        LOG(ERROR) << error;
        sendStartScanResp(senderApp, false, error);
        return;
      }
      auxRxMacs.push_back(auxRxMac);
      auxRx.push_back(std::move(*node));
    }
  }
  for (size_t i = 0; i < auxTx.size(); i++) {
    if (!checkLink(auxTx[i], auxRx[i])) {
      return;
    }
  }

  auto allNodes = lockedTopologyW->getAllNodes();
  lockedTopologyW.unlock(); // lockedTopologyW -> NULL

  // Get polarities for all nodes from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities(allNodes);

  // Schedule the CBF scan
  const int scanId = ++scanCounter_;
  const uint64_t bwgdIdx = TimeUtils::unixTimeToBwgd(startScan.startTime);
  const uint64_t actualBwgd = sendScanToTxAndRxNodes(
      mainTxMac,
      {mainRxMac},
      scanId,
      bwgdIdx,
      startScan,
      nodePolarities);
  addScan(
      scanId,
      makeScanData(
          mainTxMac,
          actualBwgd,
          startScan,
          2 + auxTx.size() * 2,
          groupCounter_,
          *nodeByName));
  VLOG(2) << "Initiating CBF scan " << scanId << " from "
          << startScan.mainTxNode_ref().value() << " to "
          << startScan.mainRxNode_ref().value() << " at "
          << actualBwgd;

  thrift::StartScan auxStartScan = startScan;
  for (size_t i = 0; i < auxTx.size(); i++) {
    if (startScan.auxTxPwrIndex_ref().has_value()) {
      auxStartScan.txPwrIndex_ref() = startScan.auxTxPwrIndex_ref().value()[i];
    }
    const uint64_t actualBwgd2 = sendScanToTxAndRxNodes(
        auxTxMacs[i],
        {auxRxMacs[i]},
        scanId,
        bwgdIdx,
        auxStartScan,
        nodePolarities);
    if (startScan.auxRxNodes_ref().has_value() &&
        startScan.auxTxNodes_ref().has_value()) {
      VLOG(3) << "Initiating aux CBF scan " << scanId << " from "
              << startScan.auxTxNodes_ref().value()[i] << " to "
              << startScan.auxRxNodes_ref().value()[i] << " at " << actualBwgd2;
      if (actualBwgd != actualBwgd2) {
        LOG(ERROR) << "CBF main and aux scans (" << scanId
                   << ") at different bwgds: " << actualBwgd << " "
                   << actualBwgd2;
      }
    }
  }

  std::string scanMsg = folly::sformat("Scheduled CBF scan {}", scanId);
  sendStartScanResp(senderApp, true, scanMsg, scanId);
  eventClient_->logEventThrift(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_REQ,
      thrift::EventLevel::INFO,
      scanMsg,
      startScan);
}

void
ScanApp::scheduleSendToMinion(
    const std::string& mac, const thrift::ScanReq& req) {
  if (mac.empty()) {
    LOG(ERROR) << "Dropping scan request with empty MAC address!";
    return;
  }

  // Schedule sending scan request to minion scans_start_time_offset before
  // actual scan time
  std::chrono::system_clock::time_point when =
      TimeUtils::bwgdToUnixTime(req.startBwgdIdx) -
      FLAGS_scans_start_time_offset_s;

  // Timeout will self-destroy after firing
  scheduleTimeout(
      std::chrono::duration_cast<std::chrono::milliseconds>(
          when - std::chrono::system_clock::now()),
      [ this, mac, req ]() noexcept {
        std::string minionMac;
        {
          auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
          std::optional<thrift::Node> minionNode =
              lockedTopologyW->getNodeByMac(mac);
          if (!minionNode) {
            LOG(ERROR) << "Trying to send scan to unknown node " << mac;
            return;
          }
          minionMac = minionNode->mac_addr;
        }

        VLOG(5) << "Sending scan with token=" << req.token
                << ", bwgdIdx=" << req.startBwgdIdx << " to node " << mac
                << " at minion " << minionMac;
        sendToMinionApp(
            minionMac,
            E2EConsts::kStatusAppMinionId,
            thrift::MessageType::SCAN_REQ,
            req);
      });
}

std::string
ScanApp::getScanKey(
    const thrift::ScanType& scanType,
    const std::string& txNode,
    const std::string& rxNode) const {
  return scanTypeToStr(scanType) + "-" + txNode + "-" + rxNode;
}

std::string
ScanApp::getLinkKey(
    const std::string& txNode, const std::string& rxNode) const {
  return txNode + "-" + rxNode;
}

int32_t
ScanApp::getBeamKey(int16_t txBeam, int16_t rxBeam) const {
  return (int32_t)rxBeam << 16 | (int32_t)txBeam;
}

uint64_t
ScanApp::doCbfScan(
    const thrift::ScanType& scanType, uint64_t bwgdIdx) {
  VLOG(2) << "Initiating " << scanTypeToStr(scanType) << " scan at BWGD "
          << bwgdIdx << "...";

  std::string cbfConfigJson = SharedObjects::getE2EConfigWrapper()
                                  ->rlock()
                                  ->getConfig()
                                  ->scanParams.cbfConfigJson;
  thrift::CbfConfig cbfConfig = deserializeCbfConfig(cbfConfigJson);

  // Get polarities for all nodes from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities();

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  ScanScheduler scheduler(*lockedTopologyW);
  const std::vector<std::string> macs = scheduler.getAllMacs();

  for (const auto& schedGroup : scheduler.getSchedGroups()) {
    uint64_t bwgdEndMaxInGroup = bwgdIdx;
    for (const size_t txNodeId : schedGroup) {
      const std::string mainTxMac = macs[txNodeId];
      for (const thrift::Link& link :
           lockedTopologyW->getLinksByRadioMac(mainTxMac)) {
        if (link.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        if (link.a_node_mac.empty() || link.z_node_mac.empty()) {
          continue;
        }
        if (link.a_node_mac != mainTxMac && link.z_node_mac != mainTxMac) {
          continue;
        }
        const std::string mainRxMac =
            link.a_node_mac == mainTxMac ? link.z_node_mac : link.a_node_mac;
        auto scanKey = getScanKey(scanType, mainTxMac, mainRxMac);
        auto it = cbfConfig.config.find(scanKey);
        if (it == cbfConfig.config.end()) {
          continue;
        }
        VLOG(5) << "Found CBF config for key:" << scanKey;
        thrift::StartScan startScan = it->second;

        // Check for valid aux links
        std::vector<thrift::Node> auxTx, auxRx;
        bool auxValid = true;
        if (startScan.auxTxNodes_ref().has_value()) {
          for (const std::string& n : startScan.auxTxNodes_ref().value()) {
            std::optional<thrift::Node> node = lockedTopologyW->getNodeByMac(n);
            if (!node) {
              auxValid = false;
              break;
            }
          }
        }
        if (startScan.auxRxNodes_ref().has_value()) {
          for (const std::string& n : startScan.auxRxNodes_ref().value()) {
            std::optional<thrift::Node> node = lockedTopologyW->getNodeByMac(n);
            if (!node) {
              auxValid = false;
              break;
            }
            auxTx.push_back(std::move(*node));
          }
        }
        if (auxTx.size() != auxRx.size()) {
          auxValid = false;
        }
        if (auxValid) {
          for (size_t i = 0; i < auxTx.size(); i++) {
            std::optional<std::string> linkName =
                lockedTopologyW->getLinkName(auxTx[i].name, auxRx[i].name);
            if (!linkName) {
              auxValid = false;
              break;
            }
          }
        }
        if (!auxValid) {
          VLOG(5) << "Invalid aux links for CBF config key:" << scanKey;
          continue;
        }

        auto mainTxPolarity = nodePolarities[mainTxMac];
        auto mainRxPolarity = nodePolarities[mainRxMac];
        // CBF for hybrid nodes not supported --> skip
        bool hybridScan = isHybridPolarity(mainTxPolarity) ||
                          isHybridPolarity(mainRxPolarity);
        if (startScan.auxTxNodes_ref().has_value()) {
          for (const std::string& node : startScan.auxTxNodes_ref().value()) {
            if (isHybridPolarity(nodePolarities[node])) {
              hybridScan = true;
              break;
            }
          }
        }
        if (startScan.auxRxNodes_ref().has_value()) {
          for (const std::string& node : startScan.auxRxNodes_ref().value()) {
            if (isHybridPolarity(nodePolarities[node])) {
              hybridScan = true;
              break;
            }
          }
        }
        if (hybridScan) {
          VLOG(5) << "Skipping CBF scan with hybrid node: key:" << scanKey;
          continue;
        }

        // TODO: Find next BWGD that main and aux links are available

        // Schedule scan for main link
        const int scanId = ++scanCounter_;
        const uint64_t mainBwgd = sendScanToTxAndRxNodes(
            mainTxMac, {mainRxMac}, scanId, bwgdIdx, startScan, nodePolarities);
        const uint64_t bwgdEnd = mainBwgd + scanDurationBwgd(startScan);
        const uint64_t applyBwgd = schedulerApp_.adjustBwgd(
            thrift::SlotPurpose::SP_NULLING_APPLY,
            bwgdEnd,
            1,
            mainTxMac,
            {mainRxMac});
        startScan.applyBwgdIdx_ref() = applyBwgd;
        if (bwgdEndMaxInGroup < applyBwgd) {
          bwgdEndMaxInGroup = applyBwgd;
        }
        addScan(
            scanId,
            makeScanData(
                mainTxMac,
                mainBwgd,
                startScan,
                2 + auxTx.size() * 2,
                groupCounter_));
        VLOG(3) << "Scheduled CBF scan for main link: scanId:" << scanId
                << " key:" << scanKey << " bwgd:" << mainBwgd
                << " applyBwgd:" << applyBwgd;

        // Schedule scan for aux links
        thrift::StartScan auxStartScan = startScan;
        for (size_t i = 0; i < auxTx.size(); i++) {
          if (startScan.auxTxPwrIndex_ref().has_value()) {
            auxStartScan.txPwrIndex_ref() =
                startScan.auxTxPwrIndex_ref().value()[i];
          }
          if (startScan.auxTxNodes_ref().has_value() &&
              startScan.auxRxNodes_ref().has_value()) {
            const uint64_t auxBwgd = sendScanToTxAndRxNodes(
                startScan.auxTxNodes_ref().value()[i],
                {startScan.auxRxNodes_ref().value()[i]},
                scanId,
                bwgdIdx,
                auxStartScan,
                nodePolarities);
            VLOG(3) << "Scheduled CBF scan for aux link: scanId:" << scanId
                    << " auxTx:" << startScan.auxTxNodes_ref().value()[i]
                    << " auxRx:" << startScan.auxRxNodes_ref().value()[i]
                    << " bwgd:" << auxBwgd;
            if (mainBwgd != auxBwgd) {
              LOG(ERROR)
                  << "CBF main and aux scans at different bwgd: scanId:"
                  << scanId << " mainBwgd:" << mainBwgd << " auxBwgd:"
                  << auxBwgd;
            }
          }
        }
      }
    }
    bwgdIdx = bwgdEndMaxInGroup;
  }
  return bwgdIdx;
}

void
ScanApp::combinedScanStart(const thrift::ScanSchedule& scanSchedule) {
  uint64_t bwgdIdx =
      TimeUtils::currentBwgdIdx(FLAGS_scheduled_scans_start_time_offset_s);

  if (bwgdIdx < nextBwgdIdx_) {
    if (scanSchedule.combinedScanTimeoutSec_ref().has_value() &&
        scanSchedule.combinedScanTimeoutSec_ref().value() > 0) {
      // Periodic scan enabled, skip this cycle
      VLOG(3) << "Skipping periodic combined scan: bwgdIdx " << bwgdIdx
              << " < nextBwgdIdx_ " << nextBwgdIdx_;
      return;
    } else {
      // Do one-time scan adhering to next available BWGD
      bwgdIdx = nextBwgdIdx_;
    }
  }

  const int firstScanId = scanCounter_ + 1;
  groupCounter_++;

  // Start PBF/RTCAL scans
  std::vector<thrift::StartScan> startConfig;
  if (scanSchedule.pbfEnable) {
    thrift::StartScan startPbf;
    startPbf.scanType = thrift::ScanType::PBF;
    startPbf.scanMode = thrift::ScanMode::RELATIVE;
    startPbf.apply_ref() = true;
    if (FLAGS_pbf_relative_range > 0) {
      // Specify the one-side relative PBF beam range using the high beam index.
      // Use the same range for TX and RX nodes.
      thrift::BeamIndices beams;
      beams.low = 0;
      beams.high = FLAGS_pbf_relative_range;
      startPbf.beams_ref() = std::vector<thrift::BeamIndices>({beams, beams});
      startPbf.bwgdLen_ref() = FLAGS_pbf_relative_bwgd_len;
    } else {
      startPbf.bwgdLen_ref() = 2;
    }
    startConfig.push_back(std::move(startPbf));
  }
  if (scanSchedule.rtcalEnable) {
    thrift::StartScan startRtcal;
    startRtcal.scanType = thrift::ScanType::RTCAL;
    startRtcal.scanMode = thrift::ScanMode::RELATIVE;
    startRtcal.apply_ref() = true;
    startRtcal.bwgdLen_ref() = 8;
    startConfig.push_back(std::move(startRtcal));
  }
  if (!startConfig.empty()) {
    bwgdIdx = doPbfRtcalScan(startConfig, bwgdIdx);
  }

  // Update CBF config
  if (rfState_.dirty && FLAGS_cbf_config_periodic_update_enable) {
    setCbfConfig();
  }

  // Start CBF scans
  if (scanSchedule.cbfEnable) {
    bwgdIdx = doCbfScan(thrift::ScanType::CBF_TX, bwgdIdx);
    bwgdIdx = doCbfScan(thrift::ScanType::CBF_RX, bwgdIdx);
  }

  // Start IM scan
  if (scanSchedule.imEnable) {
    int scanCounterStart = scanCounter_;
    thrift::StartScan startScan;
    startScan.scanType = thrift::ScanType::IM;
    startScan.scanMode = thrift::ScanMode::RELATIVE;
    startScan.apply_ref() = true;
    startScan.bwgdLen_ref() = 4;
    bwgdIdx = doImScan(startScan, bwgdIdx);

    // Check if previous relative IM scan has finished
    if (relImGroupIdx_ || relImRemaining_) {
      VLOG(3) << "Warning: previous relative IM scan hasn't finished! "
                 "relImGroupIdx_:"
              << relImGroupIdx_ << " relImRemaining_:" << relImRemaining_;
    }

    thrift::CentralLaTpcConfig laTpcConfig =
        SharedObjects::getE2EConfigWrapper()
            ->rlock()
            ->getConfig()
            ->scanParams.centralLaTpcConfig;
    VLOG(5) << "maxMcsAutoEnable:" << laTpcConfig.maxMcsAutoEnable;
    if (laTpcConfig.maxMcsAutoEnable) {
      relImGroupIdx_ = groupCounter_;
      relImRemaining_ = scanCounter_ - scanCounterStart;
      // Adjust bwgdIdx to ensure next combined scan doesn't start until all
      // responses for this combined scan have been processed
      bwgdIdx += (uint64_t)std::ceil(
          (FLAGS_scan_all_responses_timeout_s.count() + 1.0) / kSecondsPerBwgd);
    }
  }

  nextBwgdIdx_ = bwgdIdx;

  if (scanCounter_ >= firstScanId) {
    const int lastScanId = scanCounter_;
    std::string scanMsg = folly::sformat(
        "Scheduled periodic combined scans {}-{} with groupId {}",
        firstScanId,
        lastScanId,
        groupCounter_);
    eventClient_->logEvent(
        thrift::EventCategory::SCAN,
        thrift::EventId::SCAN_REQ,
        thrift::EventLevel::INFO,
        scanMsg);
  }
}

uint64_t
ScanApp::doPbfRtcalScan(
    const std::vector<thrift::StartScan>& startConfig, uint64_t bwgdIdx) {
  VLOG(2) << "Initiating PBF/RTCAL scans for the network at bwgd:" << bwgdIdx;

  // Get polarities for all nodes from config
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities = getNodePolarities();

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  ScanScheduler scheduler(*lockedTopologyW);
  const std::vector<std::string> macs = scheduler.getAllMacs();

  for (const auto& schedGroup : scheduler.getSchedGroups()) {
    uint64_t bwgdEndMaxInGroup = bwgdIdx;
    for (const size_t txNodeId : schedGroup) {
      const std::string txMac = macs[txNodeId];
      for (const thrift::Link& link :
           lockedTopologyW->getLinksByRadioMac(txMac)) {
        if (link.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        if (link.a_node_mac.empty() || link.z_node_mac.empty()) {
          continue;
        }
        if (link.a_node_mac != txMac && link.z_node_mac != txMac) {
          continue;
        }
        const std::string rxMac =
            link.a_node_mac == txMac ? link.z_node_mac : link.a_node_mac;

        uint64_t bwgdLink = bwgdIdx;
        auto send = [&](thrift::StartScan startScan,
                        thrift::ScanSubType subType) {
          startScan.subType_ref() = subType;

          const int scanId = ++scanCounter_;
          const uint64_t actualBwgd = sendScanToTxAndRxNodes(
              txMac, {rxMac}, scanId, bwgdLink, startScan, nodePolarities);
          addScan(
              scanId,
              makeScanData(txMac, actualBwgd, startScan, 2, groupCounter_));

          bwgdLink = actualBwgd + scanDurationBwgd(startScan);
          if (bwgdEndMaxInGroup < bwgdLink) {
            bwgdEndMaxInGroup = bwgdLink;
          }

          VLOG(3) << "Scheduled " << scanTypeToStr(startScan.scanType)
                  << " scan from " << txMac << " to " << rxMac << " at "
                  << actualBwgd << " with token " << scanId;
        };

        for (const auto& scan : startConfig) {
          switch (scan.scanType) {
            case thrift::ScanType::PBF:
              send(scan, thrift::ScanSubType::NO_CAL);
              break;
            case thrift::ScanType::RTCAL:
              send(scan, thrift::ScanSubType::TOP_RX_CAL);
              if (FLAGS_vbs_rx_enable) {
                send(scan, thrift::ScanSubType::BOT_RX_CAL);
                send(scan, thrift::ScanSubType::VBS_RX_CAL);
              }
              send(scan, thrift::ScanSubType::TOP_TX_CAL);
              if (FLAGS_vbs_tx_enable) {
                send(scan, thrift::ScanSubType::BOT_TX_CAL);
                send(scan, thrift::ScanSubType::VBS_TX_CAL);
              }
              break;
            default:
              CHECK(false);
          }
        }
      }
    }
    bwgdIdx = bwgdEndMaxInGroup;
  }
  return bwgdIdx;
}

void
ScanApp::checkAndWarnTimeSkew(const std::string& node, uint64_t bwgd) {
  // This BWGD range corresponds to the years 2018-2042. Ought to be enough
  // for anyone :-) Checking this to avoid spurious error messages if the node
  // sends us a bad timestamp.
  if (bwgd >= 47000000000ULL && bwgd <= 77000000000ULL) {
    using namespace std::chrono;
    auto controllerTime = system_clock::now();
    auto nodeTime = TimeUtils::bwgdToUnixTime(bwgd);
    auto delta = controllerTime - nodeTime;
    if (delta.count() < 0) {
      delta = -delta;
    }
    if (delta > FLAGS_scans_start_time_offset_s &&
        controllerTime - lastTimeSyncWarnTime_ > kTimeSyncWarnInterval) {
      auto unixTimeToStr = [](system_clock::time_point t) {
        std::time_t time = system_clock::to_time_t(t);
        // ctime uses a static buffer and is not thread-safe. ctime_r is
        // thread-safe and requires a buffer of at least 26 bytes
        char buf[26];
        std::string s = ctime_r(&time, buf);
        if (!s.empty()) {
          // Remove trailing newline
          s.pop_back();
        }
        return s;
      };
      LOG(ERROR) << "Controller time (" << unixTimeToStr(controllerTime)
                 << ") is off from node [" << node << "] time (bwgd=" << bwgd
                 << ", " << unixTimeToStr(nodeTime) << ") by "
                 << duration_cast<seconds>(delta).count()
                 << "s, which is more than the threshold "
                 << duration_cast<seconds>(
                        FLAGS_scans_start_time_offset_s)
                        .count()
                 << "s. Scans will not work correctly.";
      lastTimeSyncWarnTime_ = controllerTime;
    }
  }
}

void
ScanApp::initiateTopoScan(
    const std::string& senderApp, const thrift::StartScan& startScan) {
  // Check if tx node is valid
  if (!startScan.txNode_ref().has_value()) {
    std::string error = "txNode must be specified for topology scan";
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }

  // Look up tx node
  std::optional<bool> nodeByName = std::nullopt;
  std::string error;
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
  std::optional<thrift::Node> tx = getNodeByNameOrMac(
      *lockedTopologyW, startScan.txNode_ref().value(), nodeByName, error);
  lockedTopologyW.unlock();  // lockedTopologyW -> NULL
  if (!tx) {
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }
  const std::string txMac =
      getNodeMac(nodeByName, *tx, startScan.txNode_ref().value());
  if (txMac.empty()) {
    error = folly::sformat(
        "Not starting topology scan for tx node '{}' with empty MAC", tx->name);
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }

  // Check for invalid polarity
  auto txPolarity = SharedObjects::getConfigHelper()->rlock()->getRadioPolarity(
      tx->name, txMac, false);
  if (isHybridPolarity(txPolarity)) {
    error = folly::sformat("Can't run topology scan on hybrid node {}", txMac);
    LOG(ERROR) << error;
    sendStartScanResp(senderApp, false, error);
    return;
  }

  // Send the topology scan to the tx node
  // TODO: currently, topology scan doesn't check the schedule of other scans or
  // reserve any slots in SchedulerApp, which may cause conflicts with existing
  // or following scans (the fw will reject the scans that cause conflict).
  const int scanId = ++scanCounter_;
  const uint64_t bwgdIdx = (startScan.startTime > 0)
      ? TimeUtils::unixTimeToBwgd(startScan.startTime)
      : 0;
  thrift::ScanReq req;
  req.token = scanId;
  req.scanType_ref() = thrift::ScanType::TOPO;
  req.startBwgdIdx_ref() = bwgdIdx;
  req.rxNodeMac_ref() = "ff:ff:ff:ff:ff:ff";
  if (startScan.txPwrIndex_ref().has_value()) {
    req.txPwrIndex_ref() = startScan.txPwrIndex_ref().value();
  }
  // other parameters in request are ignored

  VLOG(4) << "Sending topology scan request to tx node " << txMac << " at bwgd "
          << bwgdIdx << ", token=" << scanId;
  req.radioMac_ref() = txMac;
  scheduleSendToMinion(txMac, req);
  addScan(scanId, makeScanData(txMac, bwgdIdx, startScan, 1, groupCounter_));
  VLOG(2) << "Scheduled topology scan on " << txMac << " at bwgd " << bwgdIdx;

  std::string scanMsg = folly::sformat("Scheduled topology scan {}", scanId);
  sendStartScanResp(senderApp, true, scanMsg, scanId);
  eventClient_->logEventThrift(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_REQ,
      thrift::EventLevel::INFO,
      scanMsg,
      startScan);
}

std::unordered_map<std::string, std::optional<thrift::PolarityType>>
ScanApp::getNodePolarities(
    std::optional<std::vector<thrift::Node>> nodes) const {
  std::vector<thrift::Node> allNodes;
  if (!nodes) {
    allNodes = SharedObjects::getTopologyWrapper()->rlock()
        ->getAllNodes();
  } else {
    allNodes = *nodes;
  }

  // Get polarities for all nodes from config
  auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
      nodePolarities;
  for (const auto& node : allNodes) {
    if (!node.mac_addr.empty()) {
      nodePolarities[node.mac_addr] =
          lockedConfigHelper->getRadioPolarity(node.name, node.mac_addr, false);
    }
    for (const std::string& mac : node.wlan_mac_addrs) {
      if (mac != node.mac_addr) {
        nodePolarities[mac] =
            lockedConfigHelper->getRadioPolarity(node.name, mac, false);
      }
    }
  }
  lockedConfigHelper.unlock();
  return nodePolarities;
}

} // namespace terragraph
} // namespace facebook
