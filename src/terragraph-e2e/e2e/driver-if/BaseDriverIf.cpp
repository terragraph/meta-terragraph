/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BaseDriverIf.h"

#include <chrono>
#include <cmath>
#include <fcntl.h>
#include <folly/Format.h>
#include <folly/MapUtil.h>
#include <folly/String.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>
#include <iomanip>
#include <sstream>

#include "e2e/common/JsonUtils.h"
#include "e2e/common/TimeUtils.h"

using namespace fbzmq;
using apache::thrift::detail::TEnumMapFactory;

DEFINE_bool(
    log_all_pair_sock_messages,
    false,
    "Log all southbound Thrift messages received");

DEFINE_string(
    pps_timestamp_source,
    "",
    "PPS timestamp source for Wigig sectors in PPS sync mode");

DEFINE_string(
    ptp_timer_source, "", "PPS timestamp source for PTP timer synchronization");

DEFINE_string(ptp_device, "", "PTP hardware clock device (e.g. /dev/ptp0)");

DEFINE_string(
    ptp_vsc_ctrl_socket,
    "",
    "When using a VSC 10G 'Malibu' PHY, set the control socket path for LTC "
    "clock synchronization (if 'ptp_timer_source' is set)");

DEFINE_int32(
    ptp_vsc_port,
    0,
    "When using a VSC 10G 'Malibu' PHY, select the port used for timestamping");

DEFINE_string(
    htsf_src_mac,
    "",
    "When using HTSF as the PPS or PTP source, only use HTSF messages from "
    "this radio MAC address (empty = select automatically)");

DEFINE_int32(
    htsf_max_loop_size,
    0,
    "Largest possible loop size in the network, used in HTSF sync mode to "
    "determine the minimum time before switching to a new HTSF source in order "
    "to avoid timing loops");

DEFINE_bool(
    enable_gpsd_client,
    false,
    "Enable a gpsd client which forwards all received timestamps to firmware");

DEFINE_string(
    gps_module,
    "",
    "If set, enable module-specific commands to the GPS device");

DEFINE_int32(
    gpsd_ts_delay_ms,
    28 /* see usage for an explanation */,
    "Delay timestamps forwarded from gpsd to firmware (0 to disable)");

DEFINE_bool(
    enable_gps_position_hold,
    true,
    "Enable single-satellite mode operation on GPS devices which support it. "
    "The current location is taken from the FW_SET_NODE_PARAMS message.");

DEFINE_int32(
    driver_if_stats_interval_ms,
    1000,
    "Interval at which to publish any driver-if stats, in ms (0 to disable)");

namespace {
// ZMQ pair socket URL for PTPClockHelper thread <-> driver-if main thread
const std::string kPtpPairSockUrl{"inproc://driver-if-ptp-listen-url"};

// ZMQ pair socket URL for gpsd client thread <-> driver-if main thread
const std::string kGpsdPairSockUrl{"inproc://driver-if-gpsd-listen-url"};

// Number of missed seconds (PPS timestamps) before firmware will transition
// from PPS to RF sync (or bring down a link). This is actually 10 or 10+1, but
// we are adding a small buffer to be safe.
const int kFwSyncMissedSec{10+3};
}

namespace facebook {
namespace terragraph {

BaseDriverIf::BaseDriverIf(
    fbzmq::Context& zmqContext,
    const std::string& pairSockUrl,
    const std::string& monitorSubmitUrl,
    const std::unordered_map<std::string, std::string>& radioMacToBusId,
    bool daemonMode)
    : radioMacToBusId_{radioMacToBusId},
      pairSock_{zmqContext, folly::none, folly::none, NonblockingFlag{true}},
      daemonMode_{daemonMode} {
  // bind the pair socket
  LOG(INFO) << "Binding to '" << pairSockUrl << "'";
  if (pairSock_.bind(SocketUrl{pairSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << pairSockUrl << "'";
  }

  // create monitor client
  zmqMonitorClient_ = std::make_unique<fbzmq::ZmqMonitorClient>(
      zmqContext, monitorSubmitUrl, "DriverIf");

  // initialize radio state vector
  {
    auto lockedRadioState = radioState_.wlock();
    for (const auto& kv : radioMacToBusId) {
      RadioState radioState;
      radioState.radioMac = kv.first;
      radioState.isUp = false;
      radioState.positionSent = false;
      lockedRadioState->push_back(radioState);
    }
  }

  // initialize PTPClockHelper and GpsdClient
  // NOTE: Must be in this order!!
  initPtp(zmqContext);
  initGpsd(zmqContext);

  // message on pair socket
  addSocket(RawZmqSocketPtr{*pairSock_}, ZMQ_POLLIN, [this](int) noexcept {
    VLOG(5) << "DriverIf: Received a message from user";
    const auto thriftMsg =
        pairSock_.recvThriftObj<thrift::Message>(serializer_);
    if (thriftMsg.hasError()) {
      LOG(ERROR) << "Error receiving message: " << thriftMsg.error().errString;
      return;
    }
    processMessage(*thriftMsg);
  });

  driverIfStatsTimer_ = ZmqTimeout::make(this, [this]() noexcept {
    auto ts = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    publishDriverIfStats(ts.count());
  });
  if (FLAGS_driver_if_stats_interval_ms > 0) {
    driverIfStatsTimer_->scheduleTimeout(
        std::chrono::milliseconds(FLAGS_driver_if_stats_interval_ms),
        true /*periodic*/);
  } else {
    VLOG(2) << "driver-if stats are disabled.";
  }
}

BaseDriverIf::~BaseDriverIf() {
  if (gpsdClientThread_ && gpsdClientThread_->joinable()) {
    gpsdClientThreadStop_ = true;
    gpsdClientThread_->join();
  }
  if (ptpClockHelperThread_ && ptpClockHelperThread_->joinable()) {
    ptpClockHelperThreadStop_ = true;
    ptpClockHelperThread_->join();
  }
}

void
BaseDriverIf::publishDriverIfStats(int64_t /*tsUs*/) {
  // nothing yet
}

void
BaseDriverIf::initPtp(fbzmq::Context& zmqContext) {
  // enable PTP hardware clock sync or PPS timestamps?
  bool usePtpPps = (FLAGS_pps_timestamp_source == "PTP");
  if (!usePtpPps && FLAGS_ptp_timer_source.empty()) {
    return;
  }

  ptpClockHelper_ = std::make_shared<PTPClockHelper>();

  // initialize for NXP
  if (!FLAGS_ptp_device.empty()) {
    // bind the PTPClockHelper socket
    ptpSock_ = fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_SERVER>(
        zmqContext, folly::none, folly::none, NonblockingFlag{true});
    LOG(INFO) << "Binding to '" << kPtpPairSockUrl << "'";
    if (ptpSock_.bind(SocketUrl{kPtpPairSockUrl}).hasError()) {
      LOG(FATAL) << "Error binding to '" << kPtpPairSockUrl << "'";
    }

    // message on PTP socket
    addSocket(RawZmqSocketPtr{*ptpSock_}, ZMQ_POLLIN, [this](int) noexcept {
      VLOG(5) << "DriverIf: Received a message from PTP socket";
      const auto thriftMsg =
          ptpSock_.recvThriftObj<thrift::Message>(serializer_);
      if (thriftMsg.hasError()) {
        LOG(ERROR) << "Error receiving message: "
                   << thriftMsg.error().errString;
        return;
      }
      switch (thriftMsg->mType) {
        case thrift::MessageType::DR_STAT_PUSH: {
          if (auto msg = unwrap<thrift::DrvrStatsList>(thriftMsg->value)) {
            processDrvrStats(*msg);
          }
          break;
        }
        default:
          processMessage(*thriftMsg);
      }
    });

    ptpClockHelper_->initNxp(
        zmqContext, kPtpPairSockUrl, &radioState_, FLAGS_ptp_device);
    if (usePtpPps) {
      // Read PTP clock events on separate thread
      ptpClockHelperThread_ = std::make_unique<std::thread>([&]() {
        folly::setThreadName("DriverIfPTPClockHelper");

        // Run loop
        ptpClockHelper_->run(ptpClockHelperThreadStop_);
        LOG(INFO) << "PTP event thread exiting";
      });
    }
  }

  // initialize for VSC
  if (!FLAGS_ptp_vsc_ctrl_socket.empty()) {
    bool vscSyncInEventLoop = FLAGS_ptp_timer_source.empty();
    ptpClockHelper_->initVsc(
        FLAGS_ptp_vsc_ctrl_socket, FLAGS_ptp_vsc_port, vscSyncInEventLoop);
  }
}

void
BaseDriverIf::initGpsd(fbzmq::Context& zmqContext) {
#ifndef WITH_GPSD
  (void)zmqContext;
#else
  // enable gpsd client to forward timestamps to firmware?
  if (!FLAGS_enable_gpsd_client) {
    return;
  }

  // bind the gpsd socket
  gpsdSock_ = fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_SERVER>(
      zmqContext, folly::none, folly::none, NonblockingFlag{true});
  LOG(INFO) << "Binding to '" << kGpsdPairSockUrl << "'";
  if (gpsdSock_.bind(SocketUrl{kGpsdPairSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << kGpsdPairSockUrl << "'";
  }

  const bool forwardPpsTimestamps =
      (FLAGS_pps_timestamp_source == "GPS") ||
      FLAGS_pps_timestamp_source.empty() /* backwards compatibility */;

  // message on gpsd socket
  addSocket(
      RawZmqSocketPtr{*gpsdSock_},
      ZMQ_POLLIN,
      [this, forwardPpsTimestamps](int) noexcept {
    VLOG(5) << "DriverIf: Received a message from gpsd";
    const auto thriftMsg =
        gpsdSock_.recvThriftObj<thrift::Message>(serializer_);
    if (thriftMsg.hasError()) {
      LOG(ERROR) << "Error receiving message: " << thriftMsg.error().errString;
      return;
    }
    switch (thriftMsg->mType) {
      case thrift::MessageType::GPS_SEND_TIME: {
        if (!forwardPpsTimestamps) {
          VLOG(4) << "Dropping GPS_SEND_TIME from GPS thread (timestamp "
                      "source is set to '" << FLAGS_pps_timestamp_source
                  << "')";
          break;
        }

        // HACK! Delay timestamps by ~1 BWGD (25.6ms) + 1 superframe (1.6ms).
        // Firmware versions earlier than "Post-CS" correct for SW TSF error
        // in the first BWGD *after* PPS, and time MUST NOT be received
        // *within* the first BWGD.
        if (FLAGS_gpsd_ts_delay_ms) {
          scheduleTimeout(
              std::chrono::milliseconds(FLAGS_gpsd_ts_delay_ms),
              [&, thriftMsg]() noexcept {
            processMessage(*thriftMsg);
          });
        } else {
          processMessage(*thriftMsg);
        }
        break;
      }
      case thrift::MessageType::DR_STAT_PUSH: {
        if (auto msg = unwrap<thrift::DrvrStatsList>(thriftMsg->value)) {
          processDrvrStats(*msg);
        }
        break;
      }
      default:
        processMessage(*thriftMsg);
    }
  });

  // connect gpsd client on separate thread
  gpsdClientThread_ = std::make_unique<std::thread>([&]() {
    folly::setThreadName("DriverIfGpsdClient");

    // Run loop
    gpsdClient_ = std::make_unique<GpsdClient>(
        zmqContext,
        kGpsdPairSockUrl,
        &radioState_,
        FLAGS_gps_module,
        (FLAGS_ptp_timer_source == "GPS") ? ptpClockHelper_ : nullptr);
    gpsdClient_->run(gpsdClientThreadStop_);
    LOG(INFO) << "gpsd client thread exiting";
  });
#endif
}

template <class T>
void
logMessageBody(
    const thrift::MessageType& mType,
    const std::string& radioMac,
    const T& obj) {
  if (FLAGS_log_all_pair_sock_messages) {
    LOG(INFO) << "Received " << folly::get_default(
                  TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                  mType, "UNKNOWN")
              << " message for <" << radioMac << ">:\n"
              << JsonUtils::serializeToJson(obj);
  }
}

void
BaseDriverIf::processMessage(const thrift::Message& message) {
  // Unwrap thrift::Message -> thrift::DriverMessage
  auto driverMsg = unwrap<thrift::DriverMessage>(message.value);
  if (!driverMsg) {
    LOG(ERROR)
        << "Failed to unwrap "
        << folly::get_default(
               TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
               message.mType, "UNKNOWN")
        << " to thrift::DriverMessage";
    return;
  }

  // south bound messages to fw
  switch (message.mType) {
    case thrift::MessageType::NODE_INIT: {
      if (auto msg = unwrap<thrift::DriverNodeInitReq>(driverMsg->value)) {
        sendNodeInit(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::DR_SET_LINK_STATUS: {
      if (auto msg = unwrap<thrift::DriverSetLinkStatus>(driverMsg->value)) {
        sendLinkStatus(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_SET_NODE_PARAMS: {
      if (auto msg = unwrap<thrift::FwSetNodeParams>(driverMsg->value)) {
        sendNodeParams(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_STATS_CONFIGURE_REQ: {
      if (auto msg = unwrap<thrift::StatsConfigure>(driverMsg->value)) {
        sendFwStatsConfigureReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::PHY_LA_CONFIG_REQ: {
      if (auto msg = unwrap<thrift::PhyLAConfig>(driverMsg->value)) {
        sendPhyLAConfigReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::PHY_AGC_CONFIG_REQ: {
      if (auto msg = unwrap<thrift::PhyAgcConfig>(driverMsg->value)) {
        sendPhyAgcConfigReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::PHY_TPC_CONFIG_REQ: {
      if (auto msg = unwrap<thrift::PhyTpcConfig>(driverMsg->value)) {
        sendPhyTpcConfigReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::PHY_TPC_ADJ_TBL_CFG_REQ: {
      if (auto msg =
              unwrap<thrift::PhyTpcAdjTblCfg>(driverMsg->value)) {
        sendPhyTpcAdjTblCfgReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::GPS_GET_POS_REQ: {
      if (gpsdClient_) {
#ifdef WITH_GPSD
        // Read latest location from gpsdClient_ directly
        driverMsg->value = fbzmq::util::writeThriftObjStr(
            gpsdClient_->getLocation(), serializer_);
        thrift::Message respMsg;
        respMsg.mType = thrift::MessageType::GPS_GET_POS_RESP;
        respMsg.value = fbzmq::util::writeThriftObjStr(
            driverMsg.value(), serializer_);
        sendToDriverApp(respMsg);
#endif
      } else {
        sendGpsGetPosReq();
      }
      logMessageBody(message.mType, driverMsg->radioMac, thrift::Empty());
      break;
    }
    case thrift::MessageType::GPS_SET_POS_REQ: {
      if (auto msg = unwrap<thrift::Location>(driverMsg->value)) {
        sendGpsSetPosReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::GPS_ENABLE_REQ: {
      sendGpsEnableReq(driverMsg->radioMac);
      logMessageBody(message.mType, driverMsg->radioMac, thrift::Empty());
      break;
    }
    case thrift::MessageType::GPS_SEND_TIME: {
      if (auto msg = unwrap<thrift::GpsTimeValue>(driverMsg->value)) {
        sendGpsTime(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::DR_DEV_ALLOC_REQ: {
      if (auto msg = unwrap<thrift::DriverDevAllocReq>(driverMsg->value)) {
        sendDevAllocReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_CONFIG_REQ: {
      if (auto msg = unwrap<thrift::FwConfigParams>(driverMsg->value)) {
        sendFwConfigParamsReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_SET_CODEBOOK: {
      if (auto msg =
              unwrap<thrift::PhyAntWgtCodeBookConfig>(driverMsg->value)) {
        processFwSetCodebook(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::PHY_GOLAY_SEQUENCE_CONFIG_REQ: {
      if (auto msg =
              unwrap<thrift::PhyGolaySequenceConfigReq>(driverMsg->value)) {
        sendPhyGolaySequenceConfigReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_DEBUG_REQ: {
      if (auto msg = unwrap<thrift::Debug>(driverMsg->value)) {
        sendFwDebugReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_BF_RESP_SCAN: {
      if (auto msg = unwrap<thrift::BfRespScanConfig>(driverMsg->value)) {
        sendFwBfRespScan(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::SCAN_REQ: {
      if (auto msg = unwrap<thrift::ScanReq>(driverMsg->value)) {
        sendScanReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_SET_LOG_CONFIG: {
      if (auto msg = unwrap<thrift::SetLogConfig>(driverMsg->value)) {
        sendFwSetLogConfig(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::FW_ADJ_RESP: {
      if (auto msg = unwrap<thrift::FwAdjResp>(driverMsg->value)) {
        sendFwAdjResp(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    case thrift::MessageType::BF_SLOT_EXCLUSION_REQ: {
      if (auto msg = unwrap<thrift::BfSlotExclusionReq>(driverMsg->value)) {
        sendBfSlotExclusionReq(driverMsg->radioMac, *msg);
        logMessageBody(message.mType, driverMsg->radioMac, *msg);
      }
      break;
    }
    default: {
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") from user";
    }
  }
}

void
BaseDriverIf::sendToDriverApp(const thrift::Message& message) {
  // Inspect any relevant messages
  switch (message.mType) {
    case thrift::MessageType::NODE_INIT_NOTIFY: {
      // Device is UP
      if (auto driverMsg = unwrap<thrift::DriverMessage>(message.value)) {
        if (auto msg = unwrap<thrift::DriverNodeInitNotif>(driverMsg->value)) {
          if (!driverMsg->radioMac.empty() && msg->success) {
            updateDeviceState(driverMsg->radioMac, true);
          }
        }
      }
      break;
    }
    case thrift::MessageType::DR_DEV_UPDOWN_STATUS: {
      // Device is UP or DOWN
      if (auto driverMsg = unwrap<thrift::DriverMessage>(message.value)) {
        if (auto msg =
                unwrap<thrift::DriverDevUpDownStatus>(driverMsg->value)) {
          if (!driverMsg->radioMac.empty()) {
            updateDeviceState(driverMsg->radioMac, msg->status);
          }
        }
      }
      break;
    }
    case thrift::MessageType::FW_PPS_HTSF_INFO: {
      // Process HTSF info
      // Only use this here, don't forward up the stack
      if (auto driverMsg = unwrap<thrift::DriverMessage>(message.value)) {
        if (auto msg = unwrap<thrift::HtsfInfo>(driverMsg->value)) {
          handlePpsHtsfInfo(driverMsg->radioMac, *msg);
        }
      }
      return;
    }
    default:
      break;
  }

  // Send message across pairSock_
  const auto ret = pairSock_.sendThriftObj(message, serializer_);
  if (ret.hasError()) {
    // When running in daemon mode (e.g. without E2E minion's DriverApp), we
    // cannot gurantee a pair socket on receiver side exists. So, we suppress
    // ZMQ 'Resource Temporarily Unavailable' errors and log messages instead.
    if (daemonMode_ && ret.error().errNum == EAGAIN) {
      logMessage(message);
    } else {
      LOG(ERROR) << "Error routing message "
                 << folly::get_default(
                    TEnumMapFactory<thrift::MessageType>::
                        makeValuesToNamesMap(),
                    message.mType,
                    "UNKNOWN")
                 << " to pair socket: " << ret.error();
    }
  }
}

void
BaseDriverIf::logMessage(const thrift::Message& message) const {
  // parse DriverMessage wrapper struct
  auto driverMsg = unwrap<thrift::DriverMessage>(message.value);
  if (!driverMsg) {
    LOG(ERROR)
        << "Failed to unwrap message of type: "
        << folly::get_default(
               TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
               message.mType, "UNKNOWN");
    return;
  }

  switch (message.mType) {
    // response for node init
    case thrift::MessageType::NODE_INIT_NOTIFY: {
      auto driverNodeInitNotif =
          unwrap<thrift::DriverNodeInitNotif>(driverMsg->value);
      if (driverNodeInitNotif) {
        if (!driverNodeInitNotif->success) {
          LOG(INFO) << "Node initialization failed";
        } else {
          LOG(INFO) << "Node initialization succeeded with MAC "
                    << driverNodeInitNotif->macAddr << ", vendor <"
                    << driverNodeInitNotif->vendor << ">";
        }
      }
      break;
    }

    // firmware link status
    case thrift::MessageType::DR_LINK_STATUS: {
      auto drLinkStatus = unwrap<thrift::DriverLinkStatus>(driverMsg->value);
      if (drLinkStatus) {
        if (!drLinkStatus->valid) {
          LOG(ERROR) << "Driver Link status notification format unexpected";
          return;
        }
        LOG(INFO) << "Status of link to " << drLinkStatus->macAddr << " is: "
                  << folly::get_default(
                     TEnumMapFactory<thrift::DriverLinkStatusType>::
                         makeValuesToNamesMap(),
                     drLinkStatus->drLinkStatusType,
                     "UNKNOWN");
      }
      break;
    }

    // firmware ack for passthru messages
    case thrift::MessageType::FW_ACK: {
      auto fwAck = unwrap<thrift::FwAck>(driverMsg->value);
      if (fwAck) {
        auto reqType = folly::get_default(
            TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
            fwAck->reqType, "UNKNOWN");
        if (fwAck->success) {
          if (fwAck->reqType != thrift::MessageType::FW_SET_CODEBOOK) {
            LOG(INFO) << "Fw ack for " << reqType << " succeeded";
          } else {
            VLOG(3) << "Fw ack for " << reqType << " succeeded";
          }
        } else {
          LOG(ERROR) << "Fw ack for" << reqType << " failed";
        }
      }
      break;
    }

    // driver ack for south bound messages
    case thrift::MessageType::DR_ACK: {
      auto driverAck = unwrap<thrift::DriverAck>(driverMsg->value);
      if (driverAck) {
        auto reqType = folly::get_default(
           TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
            driverAck->reqType,
            "UNKNOWN");
        if (driverAck->success) {
          LOG(INFO) << "Driver ack for " << reqType << " succeeded";
        } else {
          LOG(INFO) << "Driver ack for" << reqType << " failed";
        }
      }
      break;
    }

    // connecting in open mode
    case thrift::MessageType::DR_WSEC_STATUS: {
      auto wsecStatus = unwrap<thrift::DriverWsecStatus>(driverMsg->value);
      if (wsecStatus) {
        LOG(INFO) << "Received DR_WSEC_STATUS for interface "
                  << wsecStatus->ifname;
      }
      break;
    }

    // ignore spammy messages
    case thrift::MessageType::FW_ROUTING_INFO:
    case thrift::MessageType::FW_HEALTHY:
      break;

    // log message type only for all other known (or unknown) messages
    default: {
      LOG(INFO)
          << "Message of type "
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << " received from driver";
    }
  }
}

void
BaseDriverIf::updateDeviceState(const std::string& radioMac, bool state) {
  // First check if we actually require a state update, then update if needed
  auto lockedRadioState = radioState_.ulock();
  for (int i = 0, n = lockedRadioState->size(); i < n; i++) {
    const RadioState& radioState = lockedRadioState->at(i);
    if (radioState.radioMac == radioMac) {
      if (radioState.isUp != state) {
        // Upgrade to write-lock, then write new value
        auto wlock = lockedRadioState.moveFromUpgradeToWrite();
        if (state) {
          wlock->at(i).isUp = true;
        } else {
          wlock->at(i).isUp = false;
          wlock->at(i).positionSent = false;
        }
      }
      break;
    }
  }
}

void
BaseDriverIf::setCounter(
    const std::string& key,
    int64_t value,
    const fbzmq::thrift::CounterValueType valueType,
    int64_t timestamp) const {
  // create counter object
  fbzmq::thrift::Counter counter;
  counter.value_ref() = value;
  counter.valueType_ref() = valueType;
  counter.timestamp_ref() = timestamp;

  zmqMonitorClient_->setCounter(key, counter);
}

void
BaseDriverIf::bumpCounter(const std::string& key) const {
  zmqMonitorClient_->bumpCounter(key);
}

void
BaseDriverIf::processDrvrStats(const thrift::DrvrStatsList& report) const {
  const auto& statsSamples = report.samples;
  const auto& gpsTimeUs = report.gpsTimeUs;
  for (const thrift::DrvrStatsSample& sample : statsSamples) {
    setCounter(
        sample.key,
        sample.value,
        fbzmq::thrift::CounterValueType::GAUGE,
        gpsTimeUs);
  }
}

bool
BaseDriverIf::sendLocationToGpsBase(const thrift::Location& location) const {
  // Forward to kernel if gpsd disabled or module name not provided
  if (!gpsdClient_ || FLAGS_gps_module.empty()) {
    (void) location; // prevent unused parameter warning if WITH_GPSD undefined
    return false;
  }

#ifdef WITH_GPSD
  // gpsd init handles serial port configuration, so don't write anything to
  // serial unless we know gpsd is running.
  // TODO: Forward success/failure ack upstream so e.g. StatusApp can retry.
  if (!gpsdClient_->connected()) {
    LOG(ERROR) << "Dropping location message (gpsd not connected)";
    return true;
  }
  std::string gpsDevice = gpsdClient_->getDevice();
  if (gpsDevice.empty()) {
    LOG(ERROR) << "Dropping location message (have not received GPS device)";
    return true;
  }

  // Module-specific commands
  if (!FLAGS_enable_gps_position_hold) {
    VLOG(2) << "Dropping location message (single-satellite mode is disabled)";
    return true;
  } else if (FLAGS_gps_module == "Telit_SL869_T") {
    // Latitude format: DDMM.MMMM (DegreesMinutes.FractionalMinute)
    // Longitude format: DDDMM.MMMM (DegreesMinutes.FractionalMinute)
    // Altitude format: dddddd.dd (signed), MSL in meters
    // Example command:
    //   $PSTMENABLEPOSITIONHOLD,1,3340.2555,N,11739.2333,W,255.55\r\n
    std::ostringstream ss;
    ss << "$PSTMENABLEPOSITIONHOLD,1,";
    ss << std::setprecision(6) << std::internal;
    ss << std::setw(2) << std::setfill('0');
    ss << (int)std::abs(location.latitude);
    ss << (60 * std::abs(location.latitude - (int)location.latitude)) << ",";
    ss << std::setfill(' ');
    ss << (location.latitude >= 0 ? "N" : "S") << ",";
    ss << std::setw(3) << std::setfill('0');
    ss << (int)std::abs(location.longitude);
    ss << std::setw(2);
    ss << (60 * std::abs(location.longitude - (int)location.longitude)) << ",";
    ss << std::setfill(' ');
    ss << (location.longitude >= 0 ? "E" : "W") << ",";
    ss << std::setprecision(8);
    ss << location.altitude;
    ss << "\r\n";
    std::string cmd = ss.str();

    // Write to serial
    writeToFile(gpsDevice, cmd);
    return true;
  }
  LOG(WARNING) << "Dropping location message (unsupported gps_module '"
               << FLAGS_gps_module << "')";
#endif
  return false;
}

void
BaseDriverIf::writeToFile(const std::string& f, const std::string& data) const {
  int fd = open(f.c_str(), O_WRONLY);
  if (fd < 0) {
    LOG(ERROR) << "Failed to open " << f << ": " << folly::errnoStr(errno);
    return;
  }
  int wlen = write(fd, data.c_str(), data.length());
  if (wlen < (int)data.length()) {
    LOG(ERROR) << "Wrote only " << wlen << " bytes (expected " << data.length()
               <<") to " << f;
  }
  if (close(fd) < 0) {
    LOG(ERROR) << "Failed to close " << f << ": " << folly::errnoStr(errno);
  }
}

void
BaseDriverIf::handlePpsHtsfInfo(
    const std::string& radioMac, const thrift::HtsfInfo& htsfInfo) {
  int64_t now = TimeUtils::getSteadyTimestamp();

  // Check if radioMac is the correct source
  bool drop = false;
  if (!FLAGS_htsf_src_mac.empty() && radioMac != FLAGS_htsf_src_mac) {
    // Explicit source configured?
    drop = true;
    VLOG(4) << "Dropping HTSF from " << radioMac << " (only accepting "
            << FLAGS_htsf_src_mac << ")";
  } else if (!htsfSrcMac_.empty() && radioMac != htsfSrcMac_) {
    // Change sources after HTSF timeout from htsfSrcMac_
    int64_t deltaSec = now - lastHtsfTs_;
    int minSwitchDelaySec = FLAGS_htsf_max_loop_size * kFwSyncMissedSec;
    if (minSwitchDelaySec == 0) {
      minSwitchDelaySec = 2;  // switch after 1-2 missed samples
    }
    if (deltaSec < minSwitchDelaySec) {
      drop = true;
      VLOG(4) << "Dropping HTSF from " << radioMac << " (last HTSF from "
              << htsfSrcMac_ << " was " << deltaSec << "s ago, waiting until "
              << minSwitchDelaySec << "s)";
    }
  }

  // Set counters
  bumpCounter("tgd.htsf." + radioMac + ".numTsRcvd");
  if (drop) {
    bumpCounter("tgd.htsf." + radioMac + ".numTsDropped");
    return;
  }

  htsfSrcMac_ = radioMac;
  lastHtsfTs_ = now;

  // Use SW-HTSF or HW-HTSF to sync PTP clock
  if (ptpClockHelper_) {
    if (FLAGS_ptp_timer_source == "SW_HTSF") {
      int64_t swHtsfNs = htsfInfo.ppsSwTsfNs + (htsfInfo.ppsSwTsfUs * 1000);
      VLOG(4) << "Syncing PTP clock to SW-HTSF: " << swHtsfNs;
      ptpClockHelper_->handleTimestamp(swHtsfNs);
    } else if (FLAGS_ptp_timer_source == "HW_HTSF") {
      int64_t hwHtsfNs = htsfInfo.ppsHwTsfNs + (htsfInfo.ppsHwTsfUs * 1000);
      VLOG(4) << "Syncing PTP clock to HW-HTSF: " << hwHtsfNs;
      ptpClockHelper_->handleTimestamp(hwHtsfNs);
    }
  }

  // Use SW-HTSF for OTA sync
  if (FLAGS_pps_timestamp_source == "SW_HTSF") {
    thrift::GpsTimeValue gpsTimeValue;
    gpsTimeValue.unixTimeSecs = htsfInfo.ppsSwTsfUs / 1000000;
    gpsTimeValue.unixTimeNsecs =
        htsfInfo.ppsSwTsfNs + ((htsfInfo.ppsSwTsfUs % 1000000) * 1000);
    VLOG(4) << folly::format(
        "Received SW-HTSF from {}: {}.{:09d}",
        radioMac, gpsTimeValue.unixTimeSecs, gpsTimeValue.unixTimeNsecs);

    // Send time to every other baseband
    auto lockedRadioState = radioState_.rlock();
    for (const auto& radioState : *lockedRadioState) {
      if (radioState.radioMac == radioMac) {
        continue;
      }
      if (radioState.isUp) {
        sendGpsTime(radioState.radioMac, gpsTimeValue);
        bumpCounter("tgd.htsf." + radioState.radioMac + ".numTsSent");
      } else {
        VLOG(4) << "Not forwarding SW-HTSF timestamp to offline radio: "
                << radioState.radioMac;
      }
    }
  } else {
    VLOG(4) << "Dropping PPS HTSF timestamp from " << radioMac
            << " (timestamp source is set to '" << FLAGS_pps_timestamp_source
            << "')";
  }
}

} // namespace terragraph
} // namespace facebook
