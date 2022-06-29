/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <memory>
#include <tuple>
#include <unordered_map>

#include "BaseDriverIf.h"
#include "BaseNetlinkSocket.h"
#include "e2e/common/Progress.h"

namespace facebook {
namespace terragraph {

// TODO:
// Future additions to data being passed to driver/firmware should add
// thrift::Message type abstractions and possibly their own parse/conversion
// methods from thrift::Message types.

/**
 * User space interface to the Terragraph driver.
 *
 * This uses netlink underneath to talk to the driver.
 *
 * The purpose of this class is to prevent leaking netlink into user space,
 * and user space types into netlink.
 */
class ArmDriverIf final : public BaseDriverIf {
 public:
  /**
   * Constructor.
   * @param zmqContext the ZMQ context
   * @param pairSockUrl the ZMQ `PAIR` socket URL on which pairSock_ binds
   * @param monitorSubmitUrl the ZmqMonitor submit URL for zmqMonitorClient_
   * @param netlinkSocket the underlying netlink socket instance
   * @param isIf2if true if we are operating in IF2IF mode
   * @param radioMacToBusId mapping from known radio MAC addresses to their
   *                        respective bus IDs
   * @param daemonMode true if running standalone, i.e. without the E2E stack
   */
  ArmDriverIf(
      fbzmq::Context& zmqContext,
      const std::string& pairSockUrl,
      const std::string& monitorSubmitUrl,
      std::unique_ptr<BaseNetlinkSocket> netlinkSocket,
      bool isIf2if,
      const std::unordered_map<std::string, std::string>& radioMacToBusId,
      bool daemonMode);

  // public for unit test

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
  void sendDevAllocReq(
      const std::string& radioMac,
      const thrift::DriverDevAllocReq& msg) override;
  void sendFwSetLogConfig(
      const std::string& radioMac, const thrift::SetLogConfig& msg) override;
  void sendFwAdjResp(
      const std::string& radioMac, const thrift::FwAdjResp& msg) override;
  void sendBfSlotExclusionReq(
      const std::string& radioMac,
      const thrift::BfSlotExclusionReq& msg) override;

  // ---- send methods to the driver (bound for u-blox driver) ----
  void sendGpsGetPosReq() override;


  /** Send the given location to the GPS receiver. */
  void sendLocationToGps(const thrift::Location& location) const;

  /** Publish any driver-if stats (called periodically). */
  void publishDriverIfStats(int64_t tsUs) override;

  // stats for unit test
  /** \{ */
  uint64_t
  getDrRespCount() {
    return drRespCount_;
  }
  uint64_t
  getUpNotifCount() {
    return upNotifCount_;
  }
  /** \} */

 private:
  /** \{ */
  ArmDriverIf(const ArmDriverIf&) = delete;
  ArmDriverIf& operator=(const ArmDriverIf&) = delete;
  /** \} */

  /** Radio properties obtained during node init. */
  struct RadioProperties {
    /** Vendor identifier string, received from firmware upon initialization. */
    std::string vendor;

    /** Beamforming procedure type (see fb_tg_fw_bf_state_if.h). */
    uint64_t ibfProcedureType = ~0 /* uninitialized */;

    /** Beamforming codebook variant (see fb_tg_fw_bf_state_if.h). */
    uint64_t codebookVariant = ~0 /* uninitialized */;

    /** PCIe slot in which the baseband card is plugged. */
    std::string pciSlot;

    // Custom comparators
    /** \{ */
    bool
    operator==(const RadioProperties& r) const {
      return vendor == r.vendor && ibfProcedureType == r.ibfProcedureType &&
             codebookVariant == r.codebookVariant && pciSlot == r.pciSlot;
    }
    bool
    operator<(const RadioProperties& r) const {
      return std::tie(vendor, ibfProcedureType, codebookVariant, pciSlot) <
             std::tie(
                 r.vendor, r.ibfProcedureType, r.codebookVariant, r.pciSlot);
    }
    /** \} */
  };

  /**
   * Split codebook into multiple passthru messages and send these to firmware.
   */
  void sendCodebook(
      const std::string& radioMac,
      const thrift::PhyAntWgtCodeBookConfig& codebook,
      const thrift::CodebookFetchReq& codebookFetchReq) const;

  /** Process a codebook. */
  void processCodebook(
      thrift::PhyAntWgtCodeBookConfig& codebook,
      const std::string& codebookFile = "") const;

  /**
   * Prepare and send a south-bound passthru message to the firmware via the
   * driver.
   */
  void prepareAndSendSBPassThruMessage(
      const thrift::PassThruMsg& passThroughMessage,
      const std::string& radioMac = "",
      bool noAck = false) const;

  /** Process a stats message from the firmware. */
  void processFwStatsMessage(
      const std::string& radioMac,
      const thrift::DriverFwStatsReport& report) const;

  /** Process a firmware health message (used by watchdog). */
  void processFwHealthyMessage(
      const thrift::FwHealthReport& fwHealthReport) const;

  /** Process a firmware codebook request. */
  void processFwGetCodebook(
      const std::string& radioMac,
      const thrift::CodebookFetchReq& codebookFetchReq) const;

  /** Process a firmware phy txpower adj table fetch request. */
  void processFwGetPhyTpcAdjTbl(
      const std::string& radioMac,
      const thrift::TpcAdjTblFetchReq& tpcAdjTblFetchReq) const;

  /** Process a node init response. */
  void processNodeInitNotify(
      const std::string& radioMac, const thrift::DriverNodeInitNotif& notify);

  /**
   * Load codebook JSON files corresponding to the given radio properties.
   * The content will be supplied to the firmware upon request.
   */
  void loadCodebook(
      const std::string& radioMac, const RadioProperties& radioProps);

  /**
   * Return the index into codebooks_ for the given props, or -1 if not found.
   */
  int getCodebookVecIndex(const RadioProperties& radioProps) const;

  /** Per-radio node init properties. */
  std::unordered_map<std::string /* radioMac */, RadioProperties>
      radioInitProps_;

  /** Firmware codebooks for each type of RadioProperties. */
  std::vector<
      std::pair<
          RadioProperties,
          std::unordered_map<uint8_t, thrift::PhyAntWgtCodeBookConfig>>>
              codebooks_;

  /**
   * Load tpc adj tbl JSON files corresponding to the given radio properties.
   * The content will be supplied to the firmware upon request.
   */
  void loadPhyTpcAdjTables(
    const std::string& radioMac, const RadioProperties& radioProps);

  /**
   * Return index into tpcAdjTables_ for the given props, or -1 if not found.
   */
  int getTpcAdjTblVecIndex(const RadioProperties& radioProps) const;

  /** Firmware tpcAdjTables for each type of RadioProperties. */
  std::vector<
      std::pair<RadioProperties, std::vector<thrift::PhyTpcAdjTblCfg>>>
          tpcAdjTables_;

  /** Netlink socket instance used to talk to the driver. */
  std::unique_ptr<BaseNetlinkSocket> netlinkSocket_;

  /** Whether we are operating in IF2IF mode. */
  bool isIf2if_;

  /** Progres reporting instance (used by watchdog). */
  Progress progress_;

  /** Netlink receive success counter. */
  int64_t nlRecvOkCount_ = 0;
  /** Netlink receive error counter. */
  int64_t nlRecvErrCount_ = 0;

  // stats for unit test
  /** \{ */
  uint64_t drRespCount_{0};
  uint64_t upNotifCount_{0};
  /** \} */
};

} // namespace terragraph
} // namespace facebook
