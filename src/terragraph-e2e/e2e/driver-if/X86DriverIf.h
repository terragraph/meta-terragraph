/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "BaseDriverIf.h"


namespace facebook {
namespace terragraph {

/**
 * Mock driver interface class for X86 architectures.
 *
 * This listens for commands from DriverApp and sends plausible replies.
 *
 * TODO: IMPORTANT! This is mostly broken - do not use!
 */
class X86DriverIf final : public BaseDriverIf {
 public:
  /**
   * Constructor.
   * @param zmqContext the ZMQ context
   * @param pairSockUrl the ZMQ `PAIR` socket URL on which pairSock_ binds
   * @param monitorSubmitUrl the ZmqMonitor submit URL for zmqMonitorClient_
   * @param macAddr our MAC address
   * @param radioMacToBusId mapping from known radio MAC addresses to their
   *                        respective bus IDs
   * @param daemonMode true if running standalone, i.e. without the E2E stack
   */
  X86DriverIf(
      fbzmq::Context& zmqContext,
      const std::string& pairSockUrl,
      const std::string& monitorSubmitUrl,
      const std::string& macAddr,
      const std::unordered_map<std::string, std::string>& radioMacToBusId,
      bool daemonMode);

 private:
  /** \{ */
  X86DriverIf(const X86DriverIf&) = delete;
  X86DriverIf& operator=(const X86DriverIf&) = delete;
  /** \} */

  // ---- send methods to the driver (bound for firmware) ----
  void sendNodeInit(
      const std::string& radioMac,
      const thrift::DriverNodeInitReq& msg) override;
  void sendLinkStatus(
      const std::string& radioMac,
      const thrift::DriverSetLinkStatus& msg) override;
  void sendNodeParams(
      const std::string& radioMac, const thrift::FwSetNodeParams& msg) override;
  void sendFwStatsConfigureReq(
      const std::string& radioMac, const thrift::StatsConfigure& msg) override;
  void sendPhyLAConfigReq(
      const std::string& radioMac, const thrift::PhyLAConfig& msg) override;
  void sendPhyAgcConfigReq(
      const std::string& radioMac, const thrift::PhyAgcConfig& msg) override;
  void sendPhyTpcConfigReq(
      const std::string& radioMac, const thrift::PhyTpcConfig& msg) override;
  void sendPhyTpcAdjTblCfgReq(
      const std::string& radioMac, const thrift::PhyTpcAdjTblCfg& msg) override;
  void sendGpsEnableReq(const std::string& radioMac) override;
  void sendGpsTime(
      const std::string& radioMac, const thrift::GpsTimeValue& msg) override;
  void sendGpsSetPosReq(
      const std::string& radioMac, const thrift::Location& msg) override;
  void sendFwConfigParamsReq(
      const std::string& radioMac, const thrift::FwConfigParams& msg) override;
  void sendDevAllocReq(
      const std::string& radioMac,
      const thrift::DriverDevAllocReq& msg) override;
  void processFwSetCodebook(
      const std::string& radioMac,
      const thrift::PhyAntWgtCodeBookConfig& msg) override;
  void sendPhyGolaySequenceConfigReq(
      const std::string& radioMac,
      const thrift::PhyGolaySequenceConfigReq& msg) override;
  void sendFwDebugReq(
      const std::string& radioMac, const thrift::Debug& msg) override;
  void sendFwBfRespScan(
      const std::string& radioMac,
      const thrift::BfRespScanConfig& msg) override;
  void sendScanReq(
      const std::string& radioMac, const thrift::ScanReq& msg) override;
  void sendFwSetLogConfig(
      const std::string& radioMac, const thrift::SetLogConfig& msg) override;
  void sendFwAdjResp(
      const std::string& radioMac, const thrift::FwAdjResp& msg) override;
  void sendBfSlotExclusionReq(
      const std::string& radioMac,
      const thrift::BfSlotExclusionReq& msg) override;

  // ---- send methods to the driver (bound for u-blox driver) ----
  void sendGpsGetPosReq() override;


  /** Process a message from other peer drivers. */
  void processPeerDriverMessage(
      const uint16_t initiatorId, const thrift::X86PeerDriverMsg& req);

  /**
   * Send a link up/down command to the responder and tell it to bring the
   * other end of link/tunnel up/down.
   */
  void sendLinkCmdToResponder(const std::string& responderIp, bool isAssoc);

  /** Send a FW_ACK for the given message type to DriverApp. */
  void sendFwAck(
      const std::string& radioMac,
      thrift::MessageType reqType,
      bool success = true);

  /** My node/sector ID in emulation. */
  const uint16_t myId_{0};

  /** My link-local IP address in emulation. */
  const std::string myLLIp_{};

  /** Socket on which DriverIf listens for incoming commands. */
  fbzmq::Socket<ZMQ_ROUTER, fbzmq::ZMQ_SERVER> cmdServerSock_;

  /** Socket on which DriverIf sends commands to other drivers. */
  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> cmdClientSock_;

  /** Last connected responder's URL. */
  std::optional<std::string> lastResponderUrl_{};
};

} // namespace terragraph
} // namespace facebook
