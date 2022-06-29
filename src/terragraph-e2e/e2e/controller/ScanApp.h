/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/Controller_types.h"
#include <fbzmq/async/ZmqTimeout.h>
#include <topology/TopologyWrapper.h>

#include "CtrlApp.h"

namespace facebook {
namespace terragraph {

class SchedulerApp;

/**
 * App that performs scans on the network.
 *
 * This runs timer-based (automatic) or manual scans, either full-network and on
 * specified nodes. Nodes should be specified using their MAC address, or a
 * MAC address of a specific radio for multi-radio nodes.
 *
 * For backward compatibility, we accept node names instead of MAC addresses.
 * In this case, all nodes should be specified as names (not mix-and-match with
 * MACs), all nodes should be single-radio, and the scan results will also
 * contain node names rather than MACs. All internal processing happens with
 * MACs either way, and we convert from and to node names on the boundaries -
 * just after receiving a scan request, or just before storing it in the result
 * map. We use ScanData::convertMacToName to remember to convert back to name.
 */
class ScanApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param scheduler the SchedulerApp instance (TODO: this shouldn't be passed)
   */
  ScanApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      SchedulerApp& scheduler);

 private:
  // ---------- //
  //  Structs  //
  // --------- //

  /** CBF aux config. */
  struct CbfAuxConfig {
    /** The tx node. */
    std::string txNode;
    /** The rx node. */
    std::string rxNode;
    /** VRX null angle for RX CBF, ATX null angle for TX CBF. */
    int16_t nullAngle;
    /** Latest known aggressor TX power. */
    int16_t txPower;
    /** Scaled pathloss for aggressor -> victim link. */
    float trxOffsetDb;
    /** Whether the link is nulling eligible. */
    bool nullingEligible;
  };

  /** CbfAuxConfig comparator. */
  struct CbfAuxCompare {
    /**
     * Sort in order of decreasing INR for nulling eligible aux links, then in
     * order of decreasing INR for aux links that aren't nulling eligible:
     *
     * {maxInrNullingEligible .. minInrNullingEligible ..
     *  maxInrNotNullingEligible .. minInrNotNullingEligible}
     */
    bool
    operator()(const CbfAuxConfig& lhs, const CbfAuxConfig& rhs) const {
      if (lhs.nullingEligible && !rhs.nullingEligible) {
        return true;
      } else if (!lhs.nullingEligible && rhs.nullingEligible) {
        return false;
      } else {
        return lhs.trxOffsetDb + lhs.txPower > rhs.trxOffsetDb + rhs.txPower;
      }
    }
  };

  // ------------------ //
  //  Message handlers  //
  // ------------------ //

  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process a request to start a one-time scan. */
  void processStartScan(
      const std::string& senderApp, const thrift::StartScan& startScan);
  /**
   * Process a scan response from a node.
   *
   * If this is the last pending response, trigger handleAllResponsesReceived().
   */
  void processScanResp(const std::string& minion, thrift::ScanResp&& scanResp);
  /** Process a scan status request. */
  void processGetScanStatus(
      const std::string& senderApp, const thrift::GetScanStatus& getScanStatus);
  /** Process a request to clear all scan status. */
  void processResetScanStatus(const std::string& senderApp);
  /** Process a CBF config request. */
  void processGetCbfConfig(
      const std::string& senderApp, const thrift::GetCbfConfig& getCbfConfig);
  /** Process a request to set CBF config via setCbfConfig(). */
  void processSetCbfConfig(const std::string& senderApp);
  /** Process a request to clear all CBF config. */
  void processResetCbfConfig(const std::string& senderApp);
  /** Process an RF state request. */
  void processGetRfState(const std::string& senderApp);
  /** Process a request to set the RF state. */
  void processSetRfState(
      const std::string& senderApp, const thrift::SetRfState& setRfState);
  /** Process a request to clear all RF state. */
  void processResetRfState(const std::string& senderApp);
  /** Process a request to set LA/TPC params via setLaTpcParams(). */
  void processSetLaTpcParams(const std::string& senderApp);
  /** Process a scan schedule request. */
  void processGetScanSchedule(
      const std::string& senderApp,
      const thrift::GetScanSchedule& getScanSchedule);
  /** Process a request to set the scan schedule. */
  void processSetScanSchedule(
      const std::string& senderApp, const thrift::ScanSchedule& scanSchedule);
  /**
   * Process a notification that scan config was updated.
   *
   * Update scan timers via updateScanTimers().
   */
  void processScanConfigUpdated();

  // ---------------------- //
  //  Scan Start Functions  //
  // ---------------------- //

  /** Start a one-time PBF/RTCAL/IM scan. */
  void initiateManualScan(
      const std::string& senderApp, const thrift::StartScan& startScan);
  /** Start a one-time CBF scan. */
  void initiateCbfScan(
      const std::string& senderApp, const thrift::StartScan& startScan);
  /** Start a one-time TOPO scan. */
  void initiateTopoScan(
      const std::string& senderApp, const thrift::StartScan& startScan);

  /** Start network-wide PBF/RTCAL scans. */
  uint64_t doPbfRtcalScan(
      const std::vector<thrift::StartScan>& startConfig, uint64_t bwgdIdx);
  /** Start network-wide CBF scans. */
  uint64_t doCbfScan(const thrift::ScanType& scanType, uint64_t bwgdIdx);
  /** Start network-wide IM scans. */
  uint64_t doImScan(const thrift::StartScan& startScan, uint64_t bwgdIdx);

  /**
   * Trigger periodic combined scans via doPbfRtcalScan(), doCbfScan(), and
   * doImScan().
   */
  void combinedScanStart(const thrift::ScanSchedule& scanSchedule);
  /** Trigger periodic IM scans via doImScan(). */
  void imScanStart();

  /**
   * Schedule/unschedule periodic scan timers based on values in the current
   * controller configuration.
   */
  void updateScanTimers();

  /**
   * Add the given scan structure into scanStatus_.
   *
   * If we've already reached the max number of scan results, purge old ones.
   *
   * Schedule a timeout to clean up if some scan responses were never received.
   */
  void addScan(int scanId, thrift::ScanData&& data);

  /**
   * Construct thrift::ScanReq and send it to the given Tx and Rx nodes via
   * scheduleSendToMinion().
   */
  uint64_t sendScanToTxAndRxNodes(
      const std::string& txMac,
      const std::vector<std::string>& rxMacs,
      int scanId,
      uint64_t bwgdIdx,
      const thrift::StartScan& startScan,
      std::unordered_map<std::string, std::optional<thrift::PolarityType>>&
          nodePolarities);

  /**
   * Send the given scan request to a minion shortly before the actual scan time
   * by scheduling a timeout.
   */
  void scheduleSendToMinion(const std::string& mac, const thrift::ScanReq& req);

  // ------------------------- //
  //  Scan Response Functions  //
  // ------------------------- //

  /**
   * Entry point for actions to take when a scan has finished (either all
   * responses received, or scan timed out).
   */
  void handleAllResponsesReceived(int token, const thrift::ScanData& scanData);

  /** Process results of completed PBF scans. */
  void processPbfResp(int scanId, const thrift::ScanData& scanData);
  /** Process results of completed IM scans. */
  void processImResp(int scanId, const thrift::ScanData& scanData);
  /** Process results of completed relative IM scans. */
  void processRelImResp(int scanId, const thrift::ScanData& scanData);
  /** Process results of completed CBF scans. */
  void processCbfResp(int scanId, const thrift::ScanData& scanData);
  /** Process results of completed TOPO scans. */
  void processTopoResp(int scanId, const thrift::ScanData& scanData);

  /**
   * Check if all relative IM scans for the latest periodic combined scan have
   * finished.
   */
  void checkRelImGroupComplete();

  /**
   * Check and warn if current controller time is off by more than a threshold
   * from node's time expressed in BWGDs.
   */
  void checkAndWarnTimeSkew(const std::string& node, uint64_t bwgd);

  // ------------------------- //
  //  Const Utility Functions  //
  // ------------------------- //

  /** Send a START_SCAN_RESP message to the initiating app. */
  void sendStartScanResp(
      const std::string& senderApp,
      bool success,
      const std::string& message,
      const std::optional<int> token = std::nullopt,
      const std::optional<int> lastToken = std::nullopt);

  /** Return a string representation of the given scan info. */
  std::string getScanKey(
      const thrift::ScanType& scanType,
      const std::string& txNode,
      const std::string& rxNode) const;

  /** Return a string representation of a link between the given Tx/Rx nodes. */
  std::string getLinkKey(
      const std::string& txNode, const std::string& rxNode) const;

  /** Return an int32 representation of the given Tx/Rx beams. */
  int32_t getBeamKey(int16_t txBeam, int16_t rxBeam) const;

  /** Return a beam-to-nodes map from the given data. */
  std::unordered_map<int16_t, std::unordered_set<std::string>> getBeamMap(
      const TopologyWrapper& topologyW,
      std::vector<thrift::BeamInfo> beamInfoList) const;

  /** Construct a map of node name to polarity for all the given nodes. */
  std::unordered_map<std::string, std::optional<thrift::PolarityType>>
  getNodePolarities(
      std::optional<std::vector<thrift::Node>> nodes = std::nullopt) const;
  // TODO change map to be MAC to polarity

  /** Serialize thrift::CbfConfig to JSON. */
  std::string serializeCbfConfig(const thrift::CbfConfig& config) const;
  /** Deserialize thrift::CbfConfig from JSON. */
  thrift::CbfConfig deserializeCbfConfig(const std::string& configJson) const;

  /**
   * Returns a thrift::Node from a node name or MAC address.
   *
   * If nodeByName is absent, sets it to true/false depending of whether
   * the input string was a name or MAC address, respectively. If nodeByName
   * is present, only allow the string to be a name or MAC address, depending
   * on its value.
   *
   * On error, returns std::nullopt and sets an error string.
   */
  std::optional<thrift::Node> getNodeByNameOrMac(
      const TopologyWrapper& topologyW,
      const std::string& nameOrMac,
      std::optional<bool>& nodeByName,
      std::string& error);

  /**
   * Returns a node's MAC address.
   *
   * Depending on whether nodeByName is true or false, the MAC address is either
   * the (only) MAC address of the thrift::Node, or the passed-in string MAC
   * address.
   */
  std::string getNodeMac(
      const std::optional<bool>& nodeByName,
      const thrift::Node& node,
      const std::string& nameOrMac);

  // ----------------- //
  //  Misc. Functions  //
  // ----------------- //

  /**
   * Set LA/TPC params from RF state. For each link causing cross-link
   * interference, set laMaxMcs to limit estimated interference according to INR
   * threshold.
   */
  void setLaTpcParams();

  /** Get TX-RX coupling from RF state. Helper function for setLaTpcParams(). */
  void getTrxOffset(
      const std::string& tx_node,
      const std::string& rx_node,
      std::unordered_map<std::string, float>& trxOffset);

  /** Generate CBF config into the provided cbfConfig struct. */
  void generateCbfConfig(
      const thrift::ScanType& scanType, thrift::CbfConfig& cbfConfig);

  /**
   * Generate CBF config via generateCbfConfig() and store it in the controller
   * config.
   */
  void setCbfConfig();

  /**
   * Set CBF config for the given data and store it in the controller config.
   */
  void setCbfLinkConfig(
      const std::string& senderApp, const thrift::StartScan& startScan);

  /** Use IM topology data to populate beams. */
  void setLinkStateFromImData();

  // ------------------ //
  //  Member variables  //
  // ------------------ //

  /** Current RF network state based on scan results. */
  thrift::RfState rfState_{};

  /** The number of scans initiated so far (used as the scan ID). */
  int scanCounter_ = 0;

  /** The number of scan groups initiated so far. */
  int groupCounter_ = 0;

  /** Group index for LA/TPC auto config. */
  int relImGroupIdx_ = 0;
  /** Remaining IM scan count for LA/TPC auto config. */
  int relImRemaining_ = 0;

  /** Lowest response ID available in scanStatus_. */
  int scanRespCounterLo_ = 1;
  /**
   * Unique ID assigned for a scan after all responses have been received or
   * timeout occurred.
   */
  int scanRespCounter_ = 0;

  /** BWGD at which combined scans that are already scheduled will finish. */
  uint64_t nextBwgdIdx_ = 0;

  /** BWGD at which IM scans that are already scheduled will finish. */
  uint64_t nextBwgdIdxIm_ = 0;

  /** Timer to keep track of controller-initiated IM scans. */
  std::unique_ptr<fbzmq::ZmqTimeout> imScanTimeout_;

  /** Timer to keep track of controller-initiated combined scans. */
  std::unique_ptr<fbzmq::ZmqTimeout> combinedScanTimeout_;

  /**
   * Holds scan results for all scans conducted.
   *
   * This is the ultimate result of ScanApp.
   */
  thrift::ScanStatus scanStatus_;

  /**
   * Last time when a messages about lack of time sync between controller and a
   * node was displayed.
   */
  std::chrono::system_clock::time_point lastTimeSyncWarnTime_;

  /** The SchedulerApp instance. */
  SchedulerApp& schedulerApp_;
};

} // namespace terragraph
} // namespace facebook
