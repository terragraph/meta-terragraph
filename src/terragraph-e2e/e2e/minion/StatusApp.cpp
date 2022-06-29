/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StatusApp.h"

#include <boost/filesystem.hpp>
#include <cmath>
#include <cstdio>
#include <folly/Conv.h>
#include <folly/FileUtil.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <fstream>

#include "BgpUtils.h"
#include "NeighborUtils.h"
#include "SharedObjects.h"
#include "e2e/common/Consts.h"
#include "e2e/common/IpUtil.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/SysUtils.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

using apache::thrift::detail::TEnumMapFactory;

DEFINE_string(uboot_version_file, "/tmp/ubootversion", "uboot version file");
DEFINE_string(
    vpp_wired_interface_substr,
    "Ethernet",
    "Wired network interface substring in VPP");

using namespace fbzmq;

namespace {
const int kOk{0}; // Script execution success

// Default version string (e.g. if version file is missing)
const std::string kUnknownVersionString{"UNKNOWN"};

const std::string kTestcodeStatusCommand{"/usr/sbin/testcode > /dev/null"};
const std::string kRestartMinionCmd{"sv restart e2e_minion"};
const std::string kRestartDeviceCmd{"/usr/sbin/fb_tg_restart.sh"};

const std::chrono::seconds kHelloTimeout{3};
const std::chrono::seconds kFwCfgReqTimeout{2};

// Declare a FW_ACK timeout X seconds after sending GPS_ENABLE_REQ
const std::chrono::seconds kGpsEnableReqFwAckTimeout{10};

// Duration of window (in minutes) for status report ack rate tracking
const std::chrono::minutes kStatusReportAckMetricWindow{5};

// command to get wireless firmware version
const std::string kGetFirmwareVersionCommand{"/usr/sbin/get_fw_version"};

// command to generate sysdump
const std::string kSysDumpCommand{"/usr/sbin/sys_dump"};

// The watchdog monitors this file for minion/controller connection status
const std::string kMinionConnectedFile{"minion.connected"};

// Stats keys
const std::string kStatusReportCounterKey{"numStatusReportSent"};
const std::string kLinkMetricChangeCounterKey{"numLinkMetricChanges"};
const std::string kBgpKeyPrefix{"e2e_minion.bgp"};
const std::string kTotalBgpPeerCounterKey{kBgpKeyPrefix + ".total_bgp_peers"};
const std::string kOnlineBgpPeerCounterKey{kBgpKeyPrefix + ".online_bgp_peers"};

// network interface info directory
const std::string kNetworkInterfaceInfoDirectory{"/sys/class/net"};

// network interface info mac address file
const std::string kNetworkInterfaceMacFilename{"/address"};

// Sysdump timeout, used to check if a sysdump has completed and send response.
const std::chrono::milliseconds kSysdumpTimeout{2000};
} // namespace

namespace facebook {
namespace terragraph {
namespace minion {

StatusApp::StatusApp(
    fbzmq::Context& zmqContext,
    const std::string& brokerRouterUrl,
    const std::string& monitorSockUrl,
    const std::string& macAddr,
    const std::chrono::seconds statusReportInterval,
    const std::chrono::seconds bgpStatusInterval,
    const std::string& ipv6GlobalAddressableIfname,
    const thrift::UpgradeStatus& upgradeStatus,
    const std::string& versionFile)
    : MinionApp(
          zmqContext,
          brokerRouterUrl,
          monitorSockUrl,
          macAddr,
          E2EConsts::kStatusAppMinionId),
      statusReportInterval_(statusReportInterval),
      bgpStatusInterval_(bgpStatusInterval),
      ipv6GlobalAddressableIfname_(ipv6GlobalAddressableIfname),
      upgradeStatus_(upgradeStatus) {

  // Get minion version string
  if (!folly::readFile(versionFile.c_str(), version_)) {
    version_ = kUnknownVersionString;
    LOG(ERROR) << "Could not read minion version file";
  } else {
    version_ = folly::trimWhitespace(version_).str();
    LOG(INFO) << "Current Minion Version: " << version_;
  }
  if (!folly::readFile(FLAGS_uboot_version_file.c_str(), ubootVersion_)) {
    ubootVersion_ = kUnknownVersionString;
    LOG(ERROR) << "Could not read uboot version file";
  } else {
    ubootVersion_ = folly::trimWhitespace(ubootVersion_).str();
    LOG(INFO) << "Current Uboot Version: " << ubootVersion_;
  }

  // Read values from node config
  dpdkEnabled_ = SharedObjects::getNodeConfigWrapper()->rlock()
      ->getEnvConfig()->DPDK_ENABLED_ref().value_or("0") == "1";

  // Get hardware model and board id
  auto lockedNodeInfoW = SharedObjects::getNodeInfoWrapper()->rlock();
  auto hwModel = lockedNodeInfoW->getHwModel();
  auto hwBoardId = lockedNodeInfoW->getHwBoardId();
  lockedNodeInfoW.unlock(); // lockedNodeInfoW -> NULL

  if (hwModel) {
    hardwareModel_ = hwModel.value();
  }
  if (hwBoardId) {
    hardwareBoardId_ = hwBoardId.value();
  }

  // Get wireless firmware version
  auto firmwareVersion = getFirmwareVersion();
  if (!firmwareVersion.hasError()) {
    (*SharedObjects::getFirmwareVersion()->wlock()) =
        folly::trimWhitespace(firmwareVersion.value()).str();
  }

  // Get node's network interface maps
  for (const auto& interface : SysUtils::findSubdirectoriesInDirectory(
           kNetworkInterfaceInfoDirectory)) {
    const std::string interfaceName = interface.second;
    const std::string interfaceMacFilePath =
        interface.first + kNetworkInterfaceMacFilename;
    std::string interfaceMac;
    if (folly::readFile(interfaceMacFilePath.c_str(), interfaceMac)) {
      if (interfaceMac.back() == '\n') {
        interfaceMac.pop_back();
      }
      networkInterfaceMacs_[interfaceName] = interfaceMac;
    }
  }

  // TODO: add node config for interfaces on which to check wired neighbor
  // status? (wiredInterfaceNames_)

  auto srAckMetricWindowSeconds =
      std::chrono::duration_cast<std::chrono::seconds>(
          kStatusReportAckMetricWindow);
  int srAckMetricWindowSize =
      std::floor(static_cast<double>(srAckMetricWindowSeconds.count()) /
          statusReportInterval_.count());
  srAckMetric_ = std::make_unique<AckMetricData>(srAckMetricWindowSize);

  // schedule initial hello message sent to driver app
  helloTimer_ = ZmqTimeout::make(this, [this]() noexcept { sendHello(); });
  helloTimer_->scheduleTimeout(kHelloTimeout);

  // schedule initial hello message sent to driver app
  fwConfigRequestTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { fwConfigReqTimeout(); });

  // status report timer
  statusReportTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { reportStatus(); });

  // bgp status update timer
  bgpStatusTimer_ =
      ZmqTimeout::make(this, [this]() noexcept { updateBgpStatus(); });
}

void
StatusApp::processMessage(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::HELLO:
      processHello(senderApp);
      break;
    case thrift::MessageType::NODE_INIT_NOTIFY:
      processNodeInitNotif(message);
      break;
    case thrift::MessageType::SET_NODE_PARAMS:
      processSetNodeParams(senderApp, message);
      break;
    case thrift::MessageType::GPS_GET_POS_REQ:
      processGpsGetPos();
      break;
    case thrift::MessageType::SCAN_REQ:
      processScanReq(senderApp, message);
      break;
    case thrift::MessageType::FW_HEALTHY:
      processFwHealthReport(senderApp, message);
      break;
    case thrift::MessageType::FW_ACK:
      processFwAck(senderApp, message);
      break;
    case thrift::MessageType::STATUS_REPORT_ACK:
      processStatusReportAck(senderApp, message);
      break;
    case thrift::MessageType::SET_UPGRADE_STATUS:
      processSetUpgradeStatus(senderApp, message);
      break;
    case thrift::MessageType::REBOOT_NODE:
      processRebootNode(senderApp, message);
      break;
    case thrift::MessageType::FW_CONFIG_REQ:
      processFwConfigReq(senderApp, message);
      break;
    case thrift::MessageType::FW_CONFIG_RESP:
      processFwConfigResp(message);
      break;
    case thrift::MessageType::GET_MINION_NEIGHBORS_REQ:
      processGetMinionNeighborsReq(senderApp, message);
      break;
    case thrift::MessageType::FW_ROUTING_INFO:
      processFwRoutingInfo(senderApp, message);
      break;
    case thrift::MessageType::UPDATE_LINK_METRICS:
      processUpdateLinkMetrics();
      break;
    case thrift::MessageType::UPDATE_FW_STATS_CONFIG:
      processUpdateFwStatsConfig();
      break;
    case thrift::MessageType::DR_LINK_STATUS:
      processLinkStatus(senderApp, message);
      break;
    case thrift::MessageType::DR_DEV_UPDOWN_STATUS:
      processDevUpDownStatus(senderApp, message);
      break;
    case thrift::MessageType::RESTART_MINION:
      processRestartMinion(senderApp, message);
      break;
    case thrift::MessageType::GET_STATUS_REPORT:
      processGetStatusReport(senderApp, message);
      break;
    case thrift::MessageType::GET_MINION_SYSDUMP:
      processGetMinionSysdump(senderApp, message);
      break;
    default:
      LOG(ERROR) << "Received message of unexpected type: "
                 << folly::get_default(
                    TEnumMapFactory<thrift::MessageType>::
                        makeValuesToNamesMap(),
                    message.mType,
                    "UNKNOWN");
  }
}

void
StatusApp::sendHello() {
  VLOG(3) << "Sending HELLO to DriverApp...";
  sendToMinionApp(
      E2EConsts::kDriverAppMinionId,
      thrift::MessageType::HELLO,
      thrift::Hello());
  helloTimer_->scheduleTimeout(kHelloTimeout);
}

void
StatusApp::fwConfigReqTimeout() {
  LOG(ERROR) << "Timeout FW_CONFIG_REQ requested by " << fwConfigRequesterApp_;

  // Prepare reponse message
  thrift::FwConfigResp resp;
  resp.getFwConfigResp.fwParamsType = thrift::FwParamsType::FW_PARAMS_INVALID;

  // Send response to requester app
  sendToCtrlApp(
      fwConfigRequesterApp_, thrift::MessageType::FW_CONFIG_RESP, resp);

  // Clear the saved requester app
  fwConfigRequesterApp_.erase();
}

void
StatusApp::sendNodeInit(const std::string& radioMac) {
  auto radioParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getRadioParams(radioMac);
  thrift::DriverNodeInitReq driverNodeInitReq;
  driverNodeInitReq.optParams = radioParams.fwParams;
  sendToDriverApp(radioMac, thrift::MessageType::NODE_INIT, driverNodeInitReq);
}

void
StatusApp::sendGpsEnable(const std::string& radioMac) {
  LOG(INFO) << "Enabling GPS...";
  sendToDriverApp(
      radioMac, thrift::MessageType::GPS_ENABLE_REQ, thrift::Empty());

  // Schedule timeout for FW_ACK
  auto& fwAckTimer = gpsEnableTimers_[radioMac];
  if (!fwAckTimer) {
    fwAckTimer = ZmqTimeout::make(this, [&, radioMac]() noexcept {
      LOG(ERROR) << "<" << radioMac << "> GPS_ENABLE_REQ timed out.";
      eventClient_->logEventDynamic(
          thrift::EventCategory::STATUS,
          thrift::EventId::GPS_SYNC,
          thrift::EventLevel::ERROR,
          folly::sformat("GPS_ENABLE_REQ timed out ({})", radioMac),
          folly::dynamic::object("sync", false)("mac", radioMac),
          std::make_optional(radioMac));
    });
  }
  fwAckTimer->scheduleTimeout(kGpsEnableReqFwAckTimeout);
}

void
StatusApp::processHello(const std::string& senderApp) {
  if (senderApp != E2EConsts::kDriverAppMinionId) {
    LOG(ERROR) << "Ignoring hello from unexpected app " << senderApp;
    return;
  }

  // received response, so cancel timer for re-sending hello to DriverApp
  helloTimer_->cancelTimeout();

  // initialize radio(s)
  LOG(INFO) << "Initializing radio(s)...";
  auto radioMacToBusId =
    SharedObjects::getNodeInfoWrapper()->rlock()->getRadioMacToBusId();
  if (radioMacToBusId.empty()) {
    sendNodeInit("");
  } else {
    for (const auto& kv : radioMacToBusId) {
      sendNodeInit(kv.first);
    }
  }
}

void
StatusApp::processNodeInitNotif(const thrift::Message& message) {
  auto drMessage = maybeReadDriverMessage<thrift::DriverNodeInitNotif>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverNodeInitNotif", "");
    return;
  }
  thrift::DriverNodeInitNotif& notif = drMessage->value;

  if (!notif.success) {
    std::string radioMac = !notif.macAddr.empty()
        ? notif.macAddr : drMessage->radioMac;
    std::string errMsg = drMessage->macPrefix() +
        "Node initialization failed for MAC '" + radioMac + "'";
    if (!radioMac.empty() && radioStatus_[radioMac].initialized) {
      // In some cases (ex. DR_DEV_UPDOWN_STATUS) we may get duplicate node init
      // responses, so ignore any subsequent failures (harmless)
      errMsg += " which is already initialized (ignoring event)";
      LOG(ERROR) << errMsg;
      return;
    } else {
      // Something is fundamentally broken, so forcefully restart the stack
      eventClient_->logEvent(
          thrift::EventCategory::DRIVER,
          thrift::EventId::DRIVER_NODE_INIT,
          thrift::EventLevel::FATAL,
          errMsg,
          "",
          std::make_optional(notif.macAddr));
      LOG(FATAL) << errMsg;
    }
  }

  // Radio is now initialized
  radioStatus_[notif.macAddr].initialized = true;
  std::string msg = folly::sformat(
      "Node initialization succeeded with MAC {}, vendor=<{}>",
      notif.macAddr,
      notif.vendor);
  LOG(INFO) << drMessage->macPrefix() << msg;
  eventClient_->logEvent(
      thrift::EventCategory::DRIVER,
      thrift::EventId::DRIVER_NODE_INIT,
      thrift::EventLevel::INFO,
      msg,
      "",
      std::make_optional(notif.macAddr));

  // Schedule timeouts upon first notification received
  if (!statusReportTimer_->isScheduled()) {
    statusReportTimer_->scheduleTimeout(
        statusReportInterval_, true /*periodic*/);
  }
  if (!bgpStatusTimer_->isScheduled()) {
    bgpStatusTimer_->scheduleTimeout(bgpStatusInterval_, true /*periodic*/);
    scheduleTimeout(std::chrono::milliseconds::zero(), [&]() noexcept {
      updateBgpStatus();  // get bgp status immediately
    });
  }

  // Get node config parameters and send to firmware
  initializeNodeParams(drMessage->radioMac);
  sendFwStatsConfig(drMessage->radioMac);
}

void
StatusApp::initializeNodeParams(const std::string& radioMac) {
  auto lockedNodeConfig = SharedObjects::getNodeConfigWrapper()->rlock();

  // Populate NodeParams from config...
  thrift::NodeParams nodeParams;
  nodeParams.type = thrift::NodeParamsType::INIT;
  auto radioParams = lockedNodeConfig->getRadioParams(radioMac);
  if (radioParams.fwParams.polarity_ref().has_value()) {
    nodeParams.polarity_ref() =
        static_cast<thrift::PolarityType>(radioParams.fwParams.polarity_ref()
        .value());
  }
  if (radioParams.fwParams.channel_ref().has_value()) {
    nodeParams.channel_ref() = radioParams.fwParams.channel_ref().value();
  }
  auto nodeAirtime = lockedNodeConfig->getNodeAirtime();
  if (!nodeAirtime.linkAirtimes.empty()) {
    nodeParams.airtimeAllocMap_ref() = nodeAirtime;
  }

  // Enable GPS if this is a DN and site accuracy is sufficient
  auto topologyInfo = lockedNodeConfig->getTopologyInfo();
  if (topologyInfo->nodeType_ref().has_value() &&
      topologyInfo->nodeType_ref().value() == thrift::NodeType::DN) {
    thrift::Location location = topologyInfo->site.location;
    if (radioParams.fwParams.forceGpsDisable_ref().value_or(0) > 0) {
      // We want to ignore accuracy-related GPS checks.
      // Send "enableGps" and force accuracy to 0 meters.
      location.accuracy = 0;
    }
    if (location.accuracy <= E2EConsts::kGpsAccuracyThresh) {
      nodeParams.enableGps_ref() = true;
      nodeParams.location_ref() = location;
    }
  }

  lockedNodeConfig.unlock();  // lockedNodeConfig -> NULL
  sendNodeParamsToDriver(nodeParams, radioMac);

  // Mark this radio as actually initialized if required fields are filled
  // Otherwise, report an error here
  if (nodeParams.polarity_ref().has_value() &&
      nodeParams.channel_ref().has_value()) {
    radiosWithAllNodeParamsSet_.insert(radioMac);
    std::string msg =
        folly::sformat("Radio parameters initialized for MAC {}", radioMac);
    LOG(INFO) << msg;
    eventClient_->logEvent(
        thrift::EventCategory::STATUS,
        thrift::EventId::NODE_PARAMS,
        thrift::EventLevel::INFO,
        msg,
        "",
        std::make_optional(radioMac));
  } else {
    std::string msg =
        folly::sformat("Missing radio parameters for MAC {}", radioMac);
    LOG(WARNING) << msg;
    eventClient_->logEvent(
        thrift::EventCategory::STATUS,
        thrift::EventId::NODE_PARAMS,
        thrift::EventLevel::WARNING,
        msg,
        "",
        std::make_optional(radioMac));
  }
}

void
StatusApp::processSetNodeParams(
    const std::string& senderApp, const thrift::Message& message) noexcept {
  // Parse request message
  auto nodeParams = maybeReadThrift<thrift::NodeParams>(message);
  if (!nodeParams) {
    handleInvalidMessage("NodeParams", senderApp);
    return;
  }

  LOG(INFO) << "Received SetNodeParams (type "
            << folly::get_default(
               TEnumMapFactory<thrift::NodeParamsType>::makeValuesToNamesMap(),
               nodeParams->type,
               "UNKNOWN")
            << ")" << (nodeParams->radioMac_ref().has_value()
                    ? " for MAC " + nodeParams->radioMac_ref().value()
                    : "");

  std::vector<std::string> radioMacs;
  bool anyRadioGpsSync = false;
  if (nodeParams->radioMac_ref().has_value()) {
    // Request is for a specific radio
    auto iter = radioStatus_.find(nodeParams->radioMac_ref().value());
    if (iter != radioStatus_.end()) {
      if (iter->second.initialized) {
        radioMacs.push_back(nodeParams->radioMac_ref().value());
      }
      anyRadioGpsSync |= iter->second.gpsSync;
    }
  } else {
    // Send NodeParams to driver for each radio on the node
    for (const auto& kv : radioStatus_) {
      if (kv.second.initialized) {
        radioMacs.push_back(kv.first);
      }
      anyRadioGpsSync |= kv.second.gpsSync;
    }
  }
  nodeParams->radioMac_ref().reset();  // not used anymore
  for (const auto& mac : radioMacs) {
    // NOTE: We ignore polarity/channel from controller if already initialized
    //       via node config in initializeNodeParams()
    sendNodeParamsToDriver(nodeParams.value(), mac);
  }

  // On the initial NodeParams message received, mark node as online
  if (nodeParams->type == thrift::NodeParamsType::INIT &&
      myStatus_ == thrift::NodeStatusType::OFFLINE) {
    // TODO - Transitioning directly to ONLINE_INITIATOR means that we may end
    // up with stale GPS location (e.g., location was changed in config but not
    // re-sent to firmware). This is resolved if minion restarts (manually,
    // by watchdog, etc.).
    myStatus_ = anyRadioGpsSync
        ? thrift::NodeStatusType::ONLINE_INITIATOR
        : thrift::NodeStatusType::ONLINE;
    reportStatus();

    std::string msg = "Minion is online";
    LOG(INFO) << msg;
    eventClient_->logEvent(
        thrift::EventCategory::STATUS,
        thrift::EventId::NODE_INFO,
        thrift::EventLevel::INFO,
        msg);

    // Let IgnitionApp know the minion is now connected to the controller so
    // self ignition can be disabled.
    sendToMinionApp(
        E2EConsts::kIgnitionAppMinionId,
        thrift::MessageType::CONTROLLER_CONNECTED,
        thrift::ControllerConnected());
  }
}

bool
StatusApp::sendNodeParamsToDriver(
    const thrift::NodeParams& nodeParams, const std::string& macAddr) {
  thrift::NodeParams params = nodeParams;

  // Don't re-send polarity/channel in INIT type messages if already initialized
  // in initializeNodeParams()
  if (nodeParams.type == thrift::NodeParamsType::INIT &&
      radiosWithAllNodeParamsSet_.count(macAddr)) {
    params.polarity_ref().reset();
    params.channel_ref().reset();
  }

  // Construct pass-through messages via FwSetNodeParams
  thrift::FwSetNodeParams fwSetNodeParams;
  if (params.airtimeAllocMap_ref().has_value()) {
    // Dynamic airtime allocation map
    thrift::PassThruMsg airtimeMapMsg;
    airtimeMapMsg.msgType = thrift::PtMsgTypes::SB_AIRTIMEALLOC;
    airtimeMapMsg.dest = thrift::PtMsgDest::SB;
    airtimeMapMsg.airtimeAllocMap = params.airtimeAllocMap_ref().value();
    fwSetNodeParams.passThruMsgs.push_back(airtimeMapMsg);
  }
  if (params.polarity_ref().has_value()) {
    // Polarity
    thrift::PassThruMsg polarityMsg;
    polarityMsg.msgType = thrift::PtMsgTypes::SB_POLARITY;
    polarityMsg.dest = thrift::PtMsgDest::SB;
    polarityMsg.polarityCfg.polarity = (int64_t)params.polarity_ref().value();
    fwSetNodeParams.passThruMsgs.push_back(polarityMsg);
  }
  if (params.golayIdx_ref().has_value()) {
    // Golay index (unused!!)
    thrift::PassThruMsg golayIdxMsg;
    golayIdxMsg.msgType = thrift::PtMsgTypes::SB_GOLAY_INDX;
    golayIdxMsg.dest = thrift::PtMsgDest::SB;
    golayIdxMsg.golayCfg.txGolayIndx = params.golayIdx_ref().value().txGolayIdx;
    golayIdxMsg.golayCfg.rxGolayIndx = params.golayIdx_ref().value().rxGolayIdx;
    fwSetNodeParams.passThruMsgs.push_back(golayIdxMsg);
  }
  if (params.channel_ref().has_value()) {
    // Channel
    thrift::PassThruMsg channelCfgMsg;
    channelCfgMsg.msgType = thrift::PtMsgTypes::SB_CHANNEL_CONFIG;
    channelCfgMsg.dest = thrift::PtMsgDest::SB;
    channelCfgMsg.channelCfg.channel = params.channel_ref().value();
    fwSetNodeParams.passThruMsgs.push_back(channelCfgMsg);
  }

  // Send GPS enable when instructed by controller (for DNs only)
  bool enableGps = false;
  if (params.enableGps_ref().has_value() && params.enableGps_ref().value()) {
    enableGps = true;
  }
  if (params.location_ref().has_value()) {
    fwSetNodeParams.location_ref() = params.location_ref().value();
  }

  // Check that some optional fields are set
  if (fwSetNodeParams.passThruMsgs.empty() &&
      !fwSetNodeParams.location_ref().has_value() &&
      !enableGps) {
    VLOG(2) << "Ignoring empty NodeParams for MAC: " << macAddr;
    return false;
  }

  // Send request to DriverApp for given baseband
  VLOG(2) << "Sending NodeParams to driver for MAC: " << macAddr;
  if (enableGps) {
    sendGpsEnable(macAddr);
  }
  if (!fwSetNodeParams.passThruMsgs.empty() ||
      fwSetNodeParams.location_ref().has_value()) {
    sendToDriverApp(
        macAddr, thrift::MessageType::FW_SET_NODE_PARAMS, fwSetNodeParams);
  }

  return true;
}

void
StatusApp::processUpdateFwStatsConfig() {
  for (const auto& kv : radioStatus_) {
    if (kv.second.initialized) {
      sendFwStatsConfig(kv.first);
    }
  }
}

void
StatusApp::sendFwStatsConfig(const std::string& radioMac) {
  auto radioParams =
      SharedObjects::getNodeConfigWrapper()->rlock()->getRadioParams(radioMac);
  if (radioParams.fwStatsConfig.empty()) {
    VLOG(2) << "Ignoring empty StatsConfigure for MAC: " << radioMac;
    return;
  }

  // Construct pass-through message
  thrift::StatsConfigure statsConfigure;
  for (const auto& pair : radioParams.fwStatsConfig) {
    statsConfigure.configs[pair.first] = pair.second;
  }
  statsConfigure.onDuration = 1;
  statsConfigure.period = 1;

  // Send request to DriverApp for given baseband
  VLOG(2) << "Sending StatsConfigure to driver for MAC: " << radioMac;
  sendToDriverApp(
      radioMac, thrift::MessageType::FW_STATS_CONFIGURE_REQ, statsConfigure);
}

void
StatusApp::processGpsGetPos() {
  sendToDriverApp(
      "" /* terminate at driver */,
      thrift::MessageType::GPS_GET_POS_REQ,
      thrift::Empty());
}

void
StatusApp::processScanReq(
    const std::string& senderApp, const thrift::Message& message) {
  auto scanReq = maybeReadThrift<thrift::ScanReq>(message);
  if (!scanReq) {
    handleInvalidMessage("ScanReq", senderApp);
    return;
  }
  sendToDriverApp(
      scanReq->radioMac_ref().value_or(""),
      thrift::MessageType::SCAN_REQ,
      scanReq.value());
  eventClient_->logEventThrift(
      thrift::EventCategory::SCAN,
      thrift::EventId::SCAN_REQ,
      thrift::EventLevel::INFO,
      "Starting scan",
      scanReq.value());
}

void
StatusApp::processFwHealthReport(
    const std::string& senderApp, const thrift::Message& message) {
  auto drMessage = maybeReadDriverMessage<thrift::FwHealthReport>(message);
  if (!drMessage) {
    handleInvalidMessage("FwHealthReport", senderApp);
    return;
  }
  thrift::FwHealthReport& healthReport = drMessage->value;

  // Get GPS time as reported by firmware.
  auto gpsTime = std::chrono::microseconds((uint64_t) healthReport.tsf);
  if (gpsTime.count() > 0) {
    GpsClock::setEpoch(gpsTime);
  }
}

void
StatusApp::processFwAck(
    const std::string& senderApp, const thrift::Message& message) {
  auto drMessage = maybeReadDriverMessage<thrift::FwAck>(message);
  if (!drMessage) {
    handleInvalidMessage("FwAck", senderApp);
    return;
  }
  thrift::FwAck& fwAck = drMessage->value;

  auto reqType = folly::get_default(
      TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
          fwAck.reqType, "UNKNOWN");
  LOG(INFO) << drMessage->macPrefix() << "Received FwAck for " << reqType;

  switch (fwAck.reqType) {
    case thrift::MessageType::FW_SET_NODE_PARAMS: {
      if (!fwAck.success) {
        LOG(ERROR) << drMessage->macPrefix() << "SetNodeParams failed.";
      } else {
        radioStatus_[drMessage->radioMac].nodeParamsSet = true;
      }
      break;
    }
    case thrift::MessageType::GPS_ENABLE_REQ: {
      // Store per-radio GPS state.
      // Currently, we report ONLINE_INITIATOR if *any* GPS is in sync.
      thrift::NodeStatusType newStatus;
      if (!fwAck.success) {
        radioStatus_[drMessage->radioMac].gpsSync = false;

        // If no more radios are GPS synced, revert node status
        bool anyRadioGpsSync = false;
        for (const auto& kv : radioStatus_) {
          anyRadioGpsSync |= kv.second.gpsSync;
        }
        if (!anyRadioGpsSync) {
          newStatus = thrift::NodeStatusType::ONLINE;
        }

        LOG(ERROR) << drMessage->macPrefix() << "GPS is not in sync.";
        eventClient_->logEventDynamic(
            thrift::EventCategory::STATUS,
            thrift::EventId::GPS_SYNC,
            thrift::EventLevel::ERROR,
            folly::sformat("GPS is not in sync ({})", drMessage->radioMac),
            folly::dynamic::object("sync", false)("mac", drMessage->radioMac),
            std::make_optional(drMessage->radioMac));
      } else {
        newStatus = thrift::NodeStatusType::ONLINE_INITIATOR;
        radioStatus_[drMessage->radioMac].gpsSync = true;

        LOG(INFO) << drMessage->macPrefix() << "GPS is in sync.";
        eventClient_->logEventDynamic(
            thrift::EventCategory::STATUS,
            thrift::EventId::GPS_SYNC,
            thrift::EventLevel::INFO,
            folly::sformat("GPS is in sync ({})", drMessage->radioMac),
            folly::dynamic::object("sync", true)("mac", drMessage->radioMac),
            std::make_optional(drMessage->radioMac));

        // Notify IgnitionApp that GPS has been enabled so distributed link
        // ignition can begin.
        sendToMinionApp(
            E2EConsts::kIgnitionAppMinionId,
            thrift::MessageType::GPS_ENABLED,
            thrift::GpsEnabled());
      }

      if (myStatus_ != thrift::NodeStatusType::OFFLINE /* must be online! */ &&
          myStatus_ != newStatus) {
        // Report status change immediately
        myStatus_ = newStatus;
        reportStatus();
      }

      // Cancel FW_ACK timeout
      auto iter = gpsEnableTimers_.find(drMessage->radioMac);
      if (iter != gpsEnableTimers_.end() &&
          iter->second &&
          iter->second->isScheduled()) {
        iter->second->cancelTimeout();
        gpsEnableTimers_.erase(iter);
      }

      break;
    }
    case thrift::MessageType::FW_STATS_CONFIGURE_REQ: {
      if (fwAck.success) {
        LOG(INFO) << drMessage->macPrefix() << "StatsConfigure succeeded.";
      } else {
        LOG(ERROR) << drMessage->macPrefix() << "StatsConfigure failed.";
      }
      break;
    }
    default:
      LOG(ERROR) << drMessage->macPrefix() << "Ignoring FwAck for " << reqType;
  }
}

void
StatusApp::processStatusReportAck(
    const std::string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Received ACK from " << senderApp;
  progress_.report(kMinionConnectedFile);

  auto ack = maybeReadThrift<thrift::StatusReportAck>(message);
  if (!ack) {
    handleInvalidMessage("StatusReportAck", senderApp);
    return;
  }

  statusReportAckTimestamp_ = GpsClock::now();

  // Update status report ack metric to show we received ack
  srAckMetric_->recordPingResponse();

  // If the controller is missing the full status report, send it immediately
  if (ack->requestFullStatusReport) {
    sendFullStatusReport_ = true;
    reportStatus();
  }
  sendFullStatusReport_ = false;
}

void
StatusApp::processSetUpgradeStatus(
    const std::string& senderApp, const thrift::Message& message) {
  auto upgradeStatus = maybeReadThrift<thrift::UpgradeStatus>(message);
  if (!upgradeStatus) {
    handleInvalidMessage("UpgradeStatus", senderApp);
    return;
  }

  upgradeStatus_ = upgradeStatus.value();
  reportStatus();
}

void
StatusApp::processRebootNode(
    const std::string& senderApp, const thrift::Message& message) {
  auto rebootNode = maybeReadThrift<thrift::RebootNode>(message);
  if (!rebootNode) {
    handleInvalidMessage("RebootNode", senderApp);
    return;
  }

  LOG(INFO) << "Received a reboot request from " << senderApp
            << " with force flag set to " << rebootNode->force;

  forcedReboot_ = rebootNode->force;

  if (!forcedReboot_) {
    // Reject unforced reboot when testcoding or preparing to testcode.
    // Self-reboot is imminent (by UpgradeApp::commitUpgrade) when we are
    // preparing to testcode, and testcode-commit is imminent (by watchdog)
    // if we are testcoding.
    const int testcodeStatus = SysUtils::system(kTestcodeStatusCommand);
    if (testcodeStatus != kOk) {
      LOG(INFO)
          << "Reboot request rejected: Testcoding or preparing to testcode "
          << std::to_string(testcodeStatus);
      return;
    }

    // Check upgrade status
    if (upgradeStatus_.usType != thrift::UpgradeStatusType::NONE) {
      LOG(INFO) << "Reboot request rejected: Upgrade in progress";
      return;
    }
  }

  // schedule reboot
  rebootTimer_ = ZmqTimeout::make(this, [this]() noexcept { reboot(); });
  rebootTimer_->scheduleTimeout(
      std::chrono::seconds(rebootNode->secondsToReboot_ref().value()));
}

void
StatusApp::processFwConfigReq(
    const std::string& senderApp, const thrift::Message& message) {
  // Check if another app already requested FW_CONFIG_REQ and did not get
  // reply yet
  if (fwConfigRequesterApp_.empty()) {
    // Save the requester app to send the response back to it
    fwConfigRequesterApp_ = senderApp;

    // Start timer to clear reply to requeter if FW did not reply back
    fwConfigRequestTimeout_->scheduleTimeout(
        kFwCfgReqTimeout, false /* not periodic*/);

    // Parse request message
    auto fwConfigParams = maybeReadThrift<thrift::FwConfigParams>(message);
    if (!fwConfigParams) {
      handleInvalidMessage("FwConfigParams", senderApp);
      fwConfigReqTimeout();
      return;
    }

    LOG(INFO) << "Forwarding FW_CONFIG_REQ message from "
              << fwConfigRequesterApp_ << " to driver";

    // TODO per-radio requests?
    sendToDriverApp(
        "", thrift::MessageType::FW_CONFIG_REQ, fwConfigParams.value());
  } else {
    LOG(ERROR) << "FW_CONFIG_REQ already in progress by  "
               << fwConfigRequesterApp_ << ", try again later";

    thrift::FwConfigResp resp;
    resp.getFwConfigResp.fwParamsType = thrift::FwParamsType::FW_PARAMS_INVALID;
    sendToCtrlApp(senderApp, thrift::MessageType::FW_CONFIG_RESP, resp);
  }
}

void
StatusApp::processFwConfigResp(const thrift::Message& message) {
  // Stop the timeout timer
  if (fwConfigRequestTimeout_->isScheduled()) {
    fwConfigRequestTimeout_->cancelTimeout();
  }

  // Parse request message
  thrift::FwConfigResp fwConfigRespMsg;
  auto getFwParamsRespDrMessage =
      maybeReadDriverMessage<thrift::GetFwParamsResp>(message);
  if (getFwParamsRespDrMessage) {
    VLOG(2) << getFwParamsRespDrMessage->macPrefix()
            << "Receieved GetFwParamsResp";
    fwConfigRespMsg.getFwConfigResp =
        (thrift::GetFwParamsResp)getFwParamsRespDrMessage->value;
  }
  auto setFwParamsRespDrMessage =
      maybeReadDriverMessage<thrift::SetFwParamsResp>(message);
  if (setFwParamsRespDrMessage) {
    VLOG(2) << getFwParamsRespDrMessage->macPrefix()
            << "Receieved SetFwParamsResp";
    fwConfigRespMsg.setFwConfigResp =
        (thrift::SetFwParamsResp)setFwParamsRespDrMessage->value;
  }

  // Failed to parse?
  if (!getFwParamsRespDrMessage && !setFwParamsRespDrMessage) {
    handleInvalidMessage("FwConfigResp", "");
    fwConfigReqTimeout();
    return;
  }

  // Forward the response
  LOG(INFO) << "Forwarding FW_CONFIG_RESP to " << fwConfigRequesterApp_;
  sendToCtrlApp(
      fwConfigRequesterApp_,
      thrift::MessageType::FW_CONFIG_RESP,
      fwConfigRespMsg);

  // Clear the cached requesting app
  fwConfigRequesterApp_.erase();
}

void
StatusApp::processGetMinionNeighborsReq(
    const std::string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Received GET_MINION_NEIGHBORS_REQ from " << senderApp;

  auto request = maybeReadThrift<thrift::GetMinionNeighborsReq>(message);
  if (!request) {
    handleInvalidMessage("GetMinionNeighborsReq", senderApp);
    return;
  }

  // Response to send back to controller
  thrift::GetMinionNeighborsResp response;
  response.reqId = request->reqId;
  response.senderApp = request->senderApp;
  for (const auto& device : request->devices) {
    auto neighborInfo = NeighborUtils::getIpNeighborInfo(device);
    if (neighborInfo.hasValue()) {
      for (const auto& info : neighborInfo.value()) {
        // Use folly::get_default to ensure minion doesn't crash
        thrift::MinionNeighborState neighborState = folly::get_default(
          TEnumMapFactory<thrift::MinionNeighborState>::makeNamesToValuesMap(),
          info.state.c_str(),
          thrift::MinionNeighborState::UNKNOWN);

        // Add neighbor to response
        thrift::MinionNeighbor minionNeighbor;
        minionNeighbor.ipv6Address = info.remoteAddress;
        minionNeighbor.macAddr = info.remoteMac;
        minionNeighbor.state = neighborState;
        response.deviceNeighborsMap[device].push_back(minionNeighbor);
      }
    } else if (neighborInfo.error().exitStatus() > 0) {  // ran unsuccessfully
      LOG(WARNING) << "Invalid device: " << device;
    } else {  // didn't run (system command failed)
      LOG(ERROR) << folly::format(
          "Neighbor query failed with exit status {}",
          neighborInfo.error().exitStatus());
    }
  }

  // Send response to controller
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::GET_MINION_NEIGHBORS_RESP,
      response);
}

void
StatusApp::processFwRoutingInfo(
    const std::string& senderApp, const thrift::Message& message) {
  auto drMessage = maybeReadDriverMessage<thrift::FwRoutingInfo>(message);
  if (!drMessage) {
    handleInvalidMessage("FwRoutingInfo", senderApp);
    return;
  }
  thrift::FwRoutingInfo& routingInfo = drMessage->value;

  // Don't set link metrics if this is a CN
  if (myNodeType_ == thrift::NodeType::CN) {
    return;  // feature disabled on CN
  }

  // Is adaptive link metric feature enabled?
  auto linkMetricConfig = SharedObjects::getNodeConfigWrapper()
      ->rlock()->getOpenrParams()->linkMetricConfig;
  if (!linkMetricConfig.enabled) {
    return;  // feature disabled
  }

  // Determine the metric that should be set (from config)
  auto metricIter = linkMetricConfig.metricMap.find(
      "MCS" + std::to_string(routingInfo.mcs));
  if (metricIter == linkMetricConfig.metricMap.end()) {
    return;  // no metric mapping (shouldn't happen)
  }
  int metric = metricIter->second;

  // Don't set link metrics if neighbor is a CN
  // (An entry is created in the peer node type map when the link comes up)
  {
    auto lockedPeerNodeTypeMap = SharedObjects::getPeerNodeTypeMap()->rlock();
    auto peerNodeTypeIter = lockedPeerNodeTypeMap->find(routingInfo.addr);
    if (peerNodeTypeIter == lockedPeerNodeTypeMap->end() ||
        peerNodeTypeIter->second == thrift::NodeType::CN) {
      return;  // feature disabled on CN
    }
  }

  // Find our previous data
  auto dataIter = linkMetricData_.find(routingInfo.addr);
  if (dataIter == linkMetricData_.end()) {
    // Initialize data for this link
    auto pair = linkMetricData_.emplace(
        routingInfo.addr,
        LinkMetricData(
            linkMetricConfig.tokenGenRate,
            linkMetricConfig.tokenBurstSize));
    dataIter = pair.first;
  }
  LinkMetricData& data = dataIter->second;

  // Compute number of successive link metric changes in either direction
  if (data.lastRoutingMetric_ != 0) {  // skip this check for initial metric
    // Check current vs. previous change direction
    if ((metric > data.lastRoutingMetric_ &&
         data.newMetricCandidate_ > data.lastRoutingMetric_) ||
        (metric < data.lastRoutingMetric_ &&
         data.newMetricCandidate_ < data.lastRoutingMetric_)) {
      data.successiveChanges_++;
    } else {
      data.successiveChanges_ = 1;
    }
    data.newMetricCandidate_ = metric;

    // Check with config value
    if (data.successiveChanges_ < linkMetricConfig.successiveChangeCount) {
      return;  // new metric is not stable enough
    }
  }

  // Don't resend same link metric
  if (metric == data.lastRoutingMetric_) {
    return;  // metric didn't change
  }

  // Rate-limit changes to link metric
  if (!data.tokenBucket_->consume(1.0)) {
    return;  // not enough tokens in bucket
  }

  // Send request
  thrift::SetLinkMetric setLinkMetric;
  setLinkMetric.linkMetricMap[routingInfo.addr] = metric;
  sendToMinionApp(
      E2EConsts::kOpenrClientAppMinionId,
      thrift::MessageType::SET_LINK_METRIC,
      setLinkMetric);

  data.lastRoutingMetric_ = metric;
  data.newMetricCandidate_ = metric;
  data.successiveChanges_ = 1;

  // Record stats
  bumpCounter(kLinkMetricChangeCounterKey);
}

void
StatusApp::processUpdateLinkMetrics() {
  auto linkMetricConfig = SharedObjects::getNodeConfigWrapper()
      ->rlock()->getOpenrParams()->linkMetricConfig;

  // If link metric config is disabled, make sure metrics get cleared
  thrift::SetLinkMetric setLinkMetric;
  for (const auto& kv : linkMetricData_) {
    if (!linkMetricConfig.enabled) {
      setLinkMetric.linkMetricMap[kv.first] = 0;  // 0 = remove
    }
  }
  sendToMinionApp(
      E2EConsts::kOpenrClientAppMinionId,
      thrift::MessageType::SET_LINK_METRIC,
      setLinkMetric);

  // Clear data, causing metrics to get re-initialized on next routing message
  linkMetricData_.clear();
}

void
StatusApp::processLinkStatus(
    const std::string& senderApp, const thrift::Message& message) {
  auto drLinkStatus = maybeReadThrift<thrift::DriverLinkStatus>(message);
  if (!drLinkStatus) {
    handleInvalidMessage("DriverLinkStatus", senderApp);
    return;
  }

  // Update node type for self/peer
  // Note that this message was forwarded from IgnitionApp only on link up/down
  // status, and only if self/peer node types were valid.
  myNodeType_ = drLinkStatus->selfNodeType;
  auto lockedPeerNodeTypeMap = SharedObjects::getPeerNodeTypeMap()->wlock();
  if (drLinkStatus->drLinkStatusType == thrift::DriverLinkStatusType::LINK_UP) {
    (*lockedPeerNodeTypeMap)[drLinkStatus->macAddr] =
        drLinkStatus->peerNodeType;
  } else {
    lockedPeerNodeTypeMap->erase(drLinkStatus->macAddr);
  }
}

void
StatusApp::processDevUpDownStatus(
    const std::string& senderApp, const thrift::Message& message) {
  auto drMessage =
      maybeReadDriverMessage<thrift::DriverDevUpDownStatus>(message);
  if (!drMessage) {
    handleInvalidMessage("DriverDevUpDownStatus", senderApp);
    return;
  }
  thrift::DriverDevUpDownStatus& devUpDownStatus = drMessage->value;

  // Check if this is a valid WLAN MAC address
  // (The driver may send UP before the correct MAC is initialized)
  auto busId = SharedObjects::getNodeInfoWrapper()->rlock()
      ->getBusId(drMessage->radioMac);
  if (!busId) {
    LOG(ERROR) << "Ignoring DriverDevUpDownStatus ("
               << (devUpDownStatus.status ? "UP" : "DOWN")
               << ") from unknown radio MAC " << drMessage->radioMac;
    return;
  }

  LOG(INFO) << drMessage->macPrefix() << "Device status: "
            << (devUpDownStatus.status ? "UP" : "DOWN");

  // Device went DOWN
  if (!devUpDownStatus.status) {
    // Forward notification to IgnitionApp (all associated links went down)
    sendToMinionApp(E2EConsts::kIgnitionAppMinionId, message);
    radioStatus_[drMessage->radioMac].initialized = false;
    radioStatus_[drMessage->radioMac].gpsSync = false;
    radioStatus_[drMessage->radioMac].nodeParamsSet = false;
    radiosWithAllNodeParamsSet_.erase(drMessage->radioMac);
    eventClient_->logEventThrift(
        thrift::EventCategory::DRIVER,
        thrift::EventId::DRIVER_DEVICE_STATUS,
        thrift::EventLevel::ERROR,
        folly::sformat("Device is DOWN: {}", drMessage->radioMac),
        devUpDownStatus,
        std::make_optional(drMessage->radioMac));
    return;  // nothing else to do here
  }

  // Device went UP
  auto iter = radioStatus_.find(drMessage->radioMac);
  if (iter == radioStatus_.end() || iter->second.initialized) {
    return;  // received UP, but device was not previously down
  }

  // Mark ourselves OFFLINE so the controller re-sends SET_NODE_PARAMS
  myStatus_ = thrift::NodeStatusType::OFFLINE;

  // Send DriverNodeInitReq to driver
  LOG(INFO) << drMessage->macPrefix() << "Re-initializing radio...";
  sendNodeInit(drMessage->radioMac);
  eventClient_->logEventThrift(
      thrift::EventCategory::DRIVER,
      thrift::EventId::DRIVER_DEVICE_STATUS,
      thrift::EventLevel::INFO,
      folly::sformat("Device is UP: {}", drMessage->radioMac),
      devUpDownStatus,
      std::make_optional(drMessage->radioMac));

  // NodeParams will get sent after receiving DriverNodeInitNotif

  // Re-initialize network interfaces
  SysUtils::system(folly::sformat(
      "{} \"{}\"",
      kRestartDeviceCmd,
      busId->empty() ? " " /* special case (DPDK_ENABLED)  */ : busId.value()));
}

void
StatusApp::logBgpStatus(
    const std::unordered_map<std::string, thrift::BgpInfo>& bgpStatus) const {
  // Send an event if any BGP peer's status changed
  int onlineNeighborCount = 0;
  for (const auto& peer : bgpStatus) {
    const auto& neighbor = peer.second;

    bool sendEvent = false;
    thrift::EventLevel level;
    std::string msg;
    folly::dynamic obj;
    if (!bgpStatus_) {
      sendEvent = true;
      level = neighbor.online ? thrift::EventLevel::INFO
                              : thrift::EventLevel::ERROR;
      msg = "New BGP peer detected";
      obj = folly::dynamic::object("peerAddress", neighbor.ipv6Address)(
          "peerStatus", neighbor.online);
    } else {
      auto bgpStatusIt = bgpStatus_->find(neighbor.ipv6Address);
      if (bgpStatusIt == bgpStatus_->end()) {
        sendEvent = true;
        level = neighbor.online ? thrift::EventLevel::INFO
                                : thrift::EventLevel::ERROR;
        msg = "New BGP peer detected";
        obj = folly::dynamic::object("peerAddress", neighbor.ipv6Address)(
            "peerStatus", neighbor.online);
      } else if (bgpStatusIt->second.online != neighbor.online) {
        sendEvent = true;
        level = neighbor.online ? thrift::EventLevel::INFO
                                : thrift::EventLevel::ERROR;
        msg = "BGP peer status changed";
        obj = folly::dynamic::object("peerAddress", neighbor.ipv6Address)(
            "peerStatus",
            neighbor.online)("peerPrevStatus", bgpStatusIt->second.online);
      }
    }
    if (sendEvent) {
      eventClient_->logEventDynamic(
          thrift::EventCategory::STATUS,
          thrift::EventId::BGP_STATUS,
          level,
          msg,
          obj);
    }
    if (neighbor.online) {
      onlineNeighborCount++;
    }
  }

  // Send event for any lost bgp peers
  if (bgpStatus_) {
    for (const auto& oldPeer : *bgpStatus_) {
      if (bgpStatus.count(oldPeer.first) == 0) {
        eventClient_->logEventDynamic(
            thrift::EventCategory::STATUS,
            thrift::EventId::BGP_STATUS,
            thrift::EventLevel::WARNING,
            "Lost BGP peer",
            folly::dynamic::object("peerLostAddr", oldPeer.first));
      }
    }
  }

  // Set stat counters
  setCounter(
      kTotalBgpPeerCounterKey,
      (uint64_t)bgpStatus.size(),
      fbzmq::thrift::CounterValueType::GAUGE);
  setCounter(
      kOnlineBgpPeerCounterKey,
      (uint64_t)onlineNeighborCount,
      fbzmq::thrift::CounterValueType::GAUGE);
}

void
StatusApp::updateBgpStatus() {
  // Check if node is a POP by checking if it uses BGP (e.g. has BGP neighbors)
  if (SharedObjects::getNodeConfigWrapper()->rlock()
          ->getNodeConfig()->bgpParams.neighbors.empty()) {
    bgpStatus_ = std::nullopt;
    return;
  }

  std::unordered_map<std::string, thrift::BgpInfo> bgpStatus;
  std::unordered_map<std::string, int> bgpStats;
  if (boost::filesystem::exists(
          boost::filesystem::path(FLAGS_exabgp_fifo_in)) &&
      boost::filesystem::exists(
          boost::filesystem::path(FLAGS_exabgp_fifo_out))) {
    bgpStatus = BgpUtils::fetchExabgpBgpStatus();
    bgpStats = BgpUtils::fetchExabgpBgpStats();
  } else if (
      boost::filesystem::exists(boost::filesystem::path(FLAGS_vtysh_path))) {
    bgpStatus = BgpUtils::fetchVtyshBgpStatus();
  } else {
    VLOG(4) << "BGP status retrieval is not implemented for this platform";
    return;
  }

  logBgpStatus(bgpStatus);
  bgpStatus_ = bgpStatus;

  // Set bgp stat counters
  for (const auto& kvpair : bgpStats) {
    // Format the key
    std::string key = folly::sformat("{}.{}", kBgpKeyPrefix, kvpair.first);
    setCounter(key, kvpair.second, fbzmq::thrift::CounterValueType::COUNTER);
  }
}

folly::Expected<std::string, folly::ProcessReturnCode>
StatusApp::getFirmwareVersion() {
  // Create command vector
  std::vector<std::string> command;
  folly::split(" ", kGetFirmwareVersionCommand, command);

  return SysUtils::runCommand(command);
}

void
StatusApp::processRestartMinion(
    const std::string& senderApp, const thrift::Message& message) {
  auto restartMinion = maybeReadThrift<thrift::RestartMinion>(message);
  if (!restartMinion) {
    handleInvalidMessage("RestartMinion", senderApp);
    return;
  }

  LOG(INFO) << "Received a minion restart request from " << senderApp
            << " with delay of " << restartMinion->secondsToRestart << "s";
  scheduleTimeout(
      std::chrono::seconds(restartMinion->secondsToRestart), [&]() noexcept {
    SysUtils::system(kRestartMinionCmd);
  });
}

void
StatusApp::processGetStatusReport(
    const std::string& senderApp, const thrift::Message& /*message*/) {
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::STATUS_REPORT,
      buildStatusReport(true /* fullReport */));
}

thrift::StatusReport
StatusApp::buildStatusReport(bool fullReport) const {
  thrift::StatusReport statusReport;

  statusReport.timeStamp = std::time(nullptr);
  statusReport.ipv6Address =
      IpUtil::getIpv6GlobalAddress(ipv6GlobalAddressableIfname_);
  statusReport.version = fullReport ? version_ : "";
  statusReport.ubootVersion = fullReport ? ubootVersion_ : "";
  statusReport.status = myStatus_;
  statusReport.upgradeStatus = upgradeStatus_;
  statusReport.configMd5 =
      SharedObjects::getNodeConfigWrapper()->rlock()->getConfigMd5();
  statusReport.hardwareModel = fullReport ? hardwareModel_ : "";
  statusReport.hardwareBoardId = fullReport ? hardwareBoardId_ : "";

  if (bgpStatus_.has_value()) {
    statusReport.bgpStatus_ref() = bgpStatus_.value();
  }
  if (TEnumMapFactory<thrift::NodeType>::makeValuesToNamesMap()
          .count(myNodeType_)) {  // initialized?
    statusReport.nodeType_ref() = myNodeType_;
  }
  if (srAckMetric_->getAckRate().has_value()) {
    statusReport.nodeReachability_ref() = srAckMetric_->getAckRate().value();
  }
  statusReport.neighborConnectionStatus_ref() = getNeighborConnectionStatus();
  if (fullReport) {
    statusReport.networkInterfaceMacs_ref() = networkInterfaceMacs_;
  }
  statusReport.lastAckGpsTimestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(
          statusReportAckTimestamp_.time_since_epoch()).count();
  statusReport.sentGpsTimestamp =
      std::chrono::duration_cast<std::chrono::microseconds>(
          GpsClock::now().time_since_epoch()).count();
  statusReport.radioStatus = radioStatus_;
  statusReport.firmwareVersion = fullReport
      ? *SharedObjects::getFirmwareVersion()->rlock()
      : "";

  return statusReport;
}

void
StatusApp::processGetMinionSysdump(
    const std::string& senderApp, const thrift::Message& message) {
  auto request = maybeReadThrift<thrift::GetMinionSysdump>(message);
  if (!request) {
    handleInvalidMessage("GetMinionSysdump", senderApp);
    return;
  }
  VLOG(2) << "Received sysdump request from " << senderApp;
  thrift::MinionSysdumpStarted minionSysdumpStarted;
  minionSysdumpStarted.sender = request->sender;
  if (sysdumpThread_) {
    std::string error = "sysdump is already running";
    LOG(ERROR) << error;
    minionSysdumpStarted.error = error;
    minionSysdumpStarted.filename = "";
    sendToCtrlApp(
        senderApp,
        thrift::MessageType::MINION_SYSDUMP_STARTED,
        minionSysdumpStarted);
    return;
  }

  std::string filename = folly::sformat(
      "sysdump-{}-{}.tgz", macAddr_, std::time(nullptr));

  // create a new thread to create and upload the sysdump
  sysdumpThread_ = std::make_unique<std::thread>([this, request, filename]() {
    std::string path = folly::sformat("/tmp/{}", filename);
    std::string command = folly::sformat("{} -o {}", kSysDumpCommand, path);
    std::string error;
    VLOG(3) << "Running sysdump: " << command;
    thrift::MinionSysdumpComplete minionSysdumpComplete;
    minionSysdumpComplete.sender = request->sender;
    if (SysUtils::system(command)) {
      error = "Could not run sysdump script";
      LOG(ERROR) << error;
      minionSysdumpComplete.error = error;
      sysdumpResp_ = std::make_unique<thrift::MinionSysdumpComplete>(
          minionSysdumpComplete);
      return;
    }
    VLOG(2) << "Uploading sysdump to: " << request->url;
    if (!CurlUtil::upload(request->url, path, error)) {
      LOG(ERROR) << error;
      minionSysdumpComplete.error = error;
      sysdumpResp_ = std::make_unique<thrift::MinionSysdumpComplete>(
          minionSysdumpComplete);
      return;
    }
    VLOG(3) << "Finished sysdump upload.";
    minionSysdumpComplete.error = "";
    sysdumpResp_ = std::make_unique<thrift::MinionSysdumpComplete>(
        minionSysdumpComplete);

    VLOG(3) << "Deleting sysdump tarball.";
    if (std::remove(path.c_str())) {
      LOG(ERROR) << "Could not delete sysdump tarball: " << path;
    }
  });
  sysdumpThread_->detach();

  minionSysdumpStarted.error = "";
  minionSysdumpStarted.filename = filename;
  sendToCtrlApp(
      senderApp,
      thrift::MessageType::MINION_SYSDUMP_STARTED,
      minionSysdumpStarted);

  // periodically check if the sysdump is complete
  sysdumpStatusTimer_ = fbzmq::ZmqTimeout::make(this, [this, senderApp]() {
    if (sysdumpResp_) {
      VLOG(2) << "Forwarding sysdump response to " << senderApp;
      sendToCtrlApp(
          senderApp,
          thrift::MessageType::MINION_SYSDUMP_COMPLETE,
          *sysdumpResp_);
      sysdumpResp_ = nullptr;
      sysdumpThread_ = nullptr;
      return;
    }
    sysdumpStatusTimer_->scheduleTimeout(kSysdumpTimeout);
  });
  sysdumpStatusTimer_->scheduleTimeout(kSysdumpTimeout);
}

void
StatusApp::reportStatus() {
  LOG(INFO) << "Reporting status to controller";

  thrift::StatusReport statusReport = buildStatusReport(sendFullStatusReport_);
  statusReport.timeStamp = 0;  // historical reasons (controller ignores anyway)
  if (statusReport.ipv6Address.empty()) {
    LOG(ERROR) << "Could not get minion's global IPv6 address on "
               << ipv6GlobalAddressableIfname_;
  }
  sendFullStatusReport_ = true;

  srAckMetric_->recordPingRequest();

  sendToCtrlApp(
      E2EConsts::kStatusAppCtrlId,
      thrift::MessageType::STATUS_REPORT,
      statusReport);
  sendToBroadcastSock(thrift::MessageType::STATUS_REPORT, statusReport);
  bumpCounter(kStatusReportCounterKey);
}

void
StatusApp::reboot() const {
  std::string cmd = std::string("reboot");
  if (forcedReboot_) {
    cmd += " -f";
  }
  if (!SysUtils::system(cmd, kOk)) {
    LOG(FATAL) << cmd << " failed";
  }
}

void
StatusApp::AckMetricData::recordPingRequest() {
  if (static_cast<int>(pingQueue_.size()) == nTrackedPings_) {
    nAcknowledgedPings_ -= pingQueue_.back();
    pingQueue_.pop_back();
  }
  pingQueue_.push_front(0);
}

bool
StatusApp::AckMetricData::recordPingResponse() {
  if (pingQueue_.empty() || pingQueue_.front() != 0) {
     return false;
   }
   pingQueue_.front() = 1;
   nAcknowledgedPings_++;
   return true;
}

std::optional<double>
StatusApp::AckMetricData::getAckRate() {
  if (pingQueue_.empty()) {
    return std::nullopt;
  }
  return static_cast<double>(nAcknowledgedPings_) / pingQueue_.size();
}

std::unordered_map<std::string, bool>
StatusApp::getNeighborConnectionStatus() const {
  auto results = NeighborUtils::fetchIpNeighborStatus(wiredInterfaceNames_);
  if (dpdkEnabled_) {
    auto vppResults = NeighborUtils::fetchVppIpNeighborStatus(
        FLAGS_vpp_wired_interface_substr);
    for (const auto& pair : vppResults) {
      results[pair.first] = pair.second;
    }
  }
  return results;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
