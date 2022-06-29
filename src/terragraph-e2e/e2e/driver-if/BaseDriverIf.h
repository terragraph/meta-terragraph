/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>

#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>
#include <fbzmq/service/monitor/ZmqMonitorClient.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "DriverIfUtil.h"
#include "GpsdClient.h"
#include "PTPClockHelper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/PassThru_types.h"

namespace facebook {
namespace terragraph {

/**
 * Base driver interface class.
 *
 * Contains virtual send() functions and implements some shared functionality.
 */
class BaseDriverIf : public fbzmq::ZmqEventLoop {
 public:
  BaseDriverIf() {}

  /**
   * Constructor.
   * @param zmqContext the ZMQ context
   * @param pairSockUrl the ZMQ `PAIR` socket URL on which pairSock_ binds
   * @param monitorSubmitUrl the ZmqMonitor submit URL for zmqMonitorClient_
   * @param radioMacToBusId mapping from known radio MAC addresses to their
   *                        respective bus IDs
   * @param daemonMode true if running standalone, i.e. without the E2E stack
   */
  BaseDriverIf(
      fbzmq::Context& zmqContext,
      const std::string& pairSockUrl,
      const std::string& monitorSubmitUrl,
      const std::unordered_map<std::string, std::string>& radioMacToBusId,
      bool daemonMode);

  /** Destructor. */
  virtual ~BaseDriverIf();

  /** \{ */
  BaseDriverIf(const BaseDriverIf&) = delete;
  BaseDriverIf& operator=(const BaseDriverIf&) = delete;
  /** \} */

 protected:
  /** Process a message destined to the driver/firmware. */
  void processMessage(const thrift::Message& message);

  /** Contextual message logging. */
  void logMessage(const thrift::Message& message) const;

  /** Set a counter using zmqMonitorClient_. */
  void setCounter(
      const std::string& key,
      int64_t value,
      const fbzmq::thrift::CounterValueType valueType,
      int64_t timestamp) const;

  /** Bump a counter using zmqMonitorClient_. */
  void bumpCounter(const std::string& key) const;

  /** Process `thrift::DrvrStatsList` from gpsdSock_ or ptpSock_. */
  void processDrvrStats(const thrift::DrvrStatsList& report) const;

  /** Handle PPS HTSF message. */
  void handlePpsHtsfInfo(
      const std::string& radioMac, const thrift::HtsfInfo& htsfInfo);

  // ---- send methods to the driver (bound for firmware) ----

  /** Send a node init request to the firmware (via driver). */
  virtual void sendNodeInit(
      const std::string& radioMac,
      const thrift::DriverNodeInitReq& msg) = 0;

  /** Send a link assoc/dissoc request to the firmware (via driver). */
  virtual void sendLinkStatus(
      const std::string& radioMac, const thrift::DriverSetLinkStatus& msg) = 0;

  /** Send a node params request to the firmware (via driver). */
  virtual void sendNodeParams(
      const std::string& radioMac,
      const thrift::FwSetNodeParams& msg) = 0;

  /** Send a firmware stats config request to the firmware (via driver). */
  virtual void sendFwStatsConfigureReq(
      const std::string& radioMac, const thrift::StatsConfigure& msg) = 0;

  /**
   * Send a PHY-layer link adaptation (LA) config request to the firmware (via
   * driver).
   */
  virtual void sendPhyLAConfigReq(
      const std::string& radioMac, const thrift::PhyLAConfig& msg) = 0;

  /**
   * Send a PHY-layer automatic gain control (AGC) config request to the
   * firmware (via driver).
   */
  virtual void sendPhyAgcConfigReq(
      const std::string& radioMac, const thrift::PhyAgcConfig& msg) = 0;

  /**
   * Send a PHY-layer transmit power control (TPC) config request to the
   * firmware (via driver).
   */
  virtual void sendPhyTpcConfigReq(
      const std::string& radioMac, const thrift::PhyTpcConfig& msg) = 0;

  /**
   * Send a PHY-layer transmit power control (TPC) adjustment table config
   * request to the firmware (via driver).
   */
  virtual void sendPhyTpcAdjTblCfgReq(
      const std::string& radioMac, const thrift::PhyTpcAdjTblCfg& msg) = 0;

  /**
   * Send a request to enable GPS functionality to the firmware (via driver).
   */
  virtual void sendGpsEnableReq(const std::string& radioMac) = 0;

  /** Send a GPS timestamp to the firmware (via driver). */
  virtual void sendGpsTime(
      const std::string& radioMac, const thrift::GpsTimeValue& msg) = 0;

  /** Send a GPS position to the firmware (via driver). */
  virtual void sendGpsSetPosReq(
      const std::string& radioMac, const thrift::Location& msg) = 0;

  /** Send a firmware config request to the firmware (via driver). */
  virtual void sendFwConfigParamsReq(
      const std::string& radioMac, const thrift::FwConfigParams& msg) = 0;

  /** Send a device allocation request to the driver. */
  virtual void sendDevAllocReq(
      const std::string& radioMac,
      const thrift::DriverDevAllocReq& msg) = 0;

  /**
   * Send a PHY-layer antenna codebook config request to the firmware (via
   * driver).
   */
  virtual void processFwSetCodebook(
      const std::string& radioMac,
      const thrift::PhyAntWgtCodeBookConfig& msg) = 0;

  /**
   * Send a PHY-layer Golay sequence config request to the firmware (via
   * driver).
   */
  virtual void sendPhyGolaySequenceConfigReq(
      const std::string& radioMac,
      const thrift::PhyGolaySequenceConfigReq& msg) = 0;

  /** Send a debug command to the firmware (via driver). */
  virtual void sendFwDebugReq(
      const std::string& radioMac, const thrift::Debug& msg) = 0;

  /**
   * Send a beamforming responder mode config request to the firmware (via
   * driver).
   */
  virtual void sendFwBfRespScan(
      const std::string& radioMac,
      const thrift::BfRespScanConfig& msg) = 0;

  /** Send a scan request to the firmware (via driver). */
  virtual void sendScanReq(
      const std::string& radioMac, const thrift::ScanReq& msg) = 0;

  /** Send a firmware logs config request to the firmware (via driver). */
  virtual void sendFwSetLogConfig(
      const std::string& radioMac, const thrift::SetLogConfig& msg) = 0;

  /** Send local routing adjacencies to the firmware (via driver). */
  virtual void sendFwAdjResp(
      const std::string& radioMac, const thrift::FwAdjResp& msg) = 0;

  /** Send a beamforming slot exclusion request to the firmware (via driver). */
  virtual void sendBfSlotExclusionReq(
      const std::string& radioMac,
      const thrift::BfSlotExclusionReq& msg) = 0;

  // ---- send methods to the driver (bound for u-blox driver) ----

  /** Send a GPS position request to the driver. */
  virtual void sendGpsGetPosReq() = 0;


  /**
   * If applicable, handle a request to send location to GPS directly
   * (e.g. not via u-blox driver).
   *
   * Returns true if handled here, or false if other handling (e.g. u-blox)
   * should be done.
   */
  bool sendLocationToGpsBase(const thrift::Location& location) const;

  /** Forward the given message up to DriverApp via pairSock_. */
  void sendToDriverApp(const thrift::Message& message);

  /**
   * Try to deserialize a binary Thrift value into the given Thrift type.
   *
   * Returns std::nullopt if this fails.
   */
  template <class T>
  std::optional<T>
  unwrap(const std::string& value) const {
    try {
      return fbzmq::util::readThriftObjStr<T>(value, serializer_);
    } catch (const std::exception& ex) {
      return std::nullopt;
    }
  }

  /** Publish any driver-if stats (called periodically). */
  virtual void publishDriverIfStats(int64_t tsUs);

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};

  /** Map of WLAN MACs to bus IDs. */
  std::unordered_map<std::string, std::string> radioMacToBusId_;

 private:
  /** Initialize structures related to PTPClockHelper. */
  void initPtp(fbzmq::Context& zmqContext);

  /** Initialize structures related to GpsdClient. */
  void initGpsd(fbzmq::Context& zmqContext);

  /** Write the given data to a file using low-level APIs. */
  void writeToFile(const std::string& f, const std::string& data) const;

  /** Update the up/down state of a baseband device (radioState_). */
  void updateDeviceState(const std::string& radioMac, bool state);

  /** Client to interact with ZmqMonitor. */
  std::unique_ptr<fbzmq::ZmqMonitorClient> zmqMonitorClient_;

  /** Pair socket on which the DriverIf listens. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_SERVER> pairSock_;

  /** gpsd socket on which the DriverIf listens. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_SERVER> gpsdSock_;

  /** gpsd client instance. */
  std::unique_ptr<GpsdClient> gpsdClient_{nullptr};

  /** gpsd client thread. */
  std::unique_ptr<std::thread> gpsdClientThread_;

  /** Simple loop-breaker in gpsdClientThread_. */
  std::atomic_bool gpsdClientThreadStop_{false};

  /** Is this process running as a daemon (without E2E minion's DriverApp)? */
  bool daemonMode_{false};

  /**
   * Internal radio state.
   *
   * This uses folly::Synchronized as it's shared with the gpsdClient_ thread,
   * and is a vector to preserve original ordering.
   */
  folly::Synchronized<std::vector<RadioState>> radioState_;

  /** PTP socket on which the DriverIf listens. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_SERVER> ptpSock_;

  /** The PTPClockHelper instance (or NULL if disabled). */
  std::shared_ptr<PTPClockHelper> ptpClockHelper_{nullptr};

  /** PTP clock event thread for ptpClockHelper_. */
  std::unique_ptr<std::thread> ptpClockHelperThread_;

  /** Simple loop-breaker in ptpClockHelperThread_. */
  std::atomic_bool ptpClockHelperThreadStop_{false};

  /** Current MAC address of the HTSF source baseband. */
  std::string htsfSrcMac_{};

  /** Monotonic time (in seconds) of the last HTSF PPS from htsfSrcMac_. */
  int64_t lastHtsfTs_{0};

  /** Timer to publish any driver-if stats periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> driverIfStatsTimer_{nullptr};
};

} // namespace terragraph
} // namespace facebook
