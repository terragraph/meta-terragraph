/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <deque>

#include <fbzmq/async/ZmqTimeout.h>
#include <folly/Expected.h>
#include <folly/Subprocess.h>
#include <folly/TokenBucket.h>


#include "MinionApp.h"
#include "e2e/common/Consts.h"
#include "e2e/common/CurlUtil.h"
#include "e2e/common/GpsClock.h"
#include "e2e/common/Progress.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/FwOptParams_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that mainly handles reporting of this node's status, but also performs
 * various miscellaneous actions.
 *
 * This app primarily communicates with a separate StatusApp on the E2E
 * controller.
 */
class StatusApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * This will fetch some status information immediately, as well as initialize
   * various timers.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   * @param statusReportInterval the time interval at which to report status to
   *                             the E2E controller
   * @param bgpStatusInterval the time interval at which to re-fetch BGP status
   * @param ipv6GlobalAddressableIfname the network interface name holding this
   *                                    node's globally-reachable IPv6 address
   * @param upgradeStatus this node's software upgrade state
   * @param versionFile the path to the current software version file
   */
  StatusApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr,
      const std::chrono::seconds statusReportInterval,
      const std::chrono::seconds bgpStatusInterval,
      const std::string& ipv6GlobalAddressableIfname,
      const thrift::UpgradeStatus& upgradeStatus,
      const std::string& versionFile);

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** Send a HELLO message to DriverApp, which will echo this message back. */
  void sendHello();

  /**
   * Send a NODE_INIT message to DriverApp to initialize the given radio with
   * its initial config.
   */
  void sendNodeInit(const std::string& radioMac);

  /**
   * Send a GPS_ENABLE_REQ message to DriverApp to enable GPS on the given
   * radio.
   */
  void sendGpsEnable(const std::string& radioMac);

  /**
   * Send NodeParams to the driver for the given baseband MAC address.
   *
   * Returns false if the given NodeParams structure is empty.
   */
  bool sendNodeParamsToDriver(
      const thrift::NodeParams& nodeParams, const std::string& macAddr);

  /**
   * Send StatsConfigure to the driver for the given baseband MAC address.
   *
   * The stats config parameters are read from node config.
   */
  void sendFwStatsConfig(const std::string& radioMac);

  /**
   * Receive a HELLO message from DriverApp, indicating it is initialized.
   *
   * Calls sendNodeInit() to initialize the radio(s).
   */
  void processHello(const std::string& senderApp);

  /** Handle a FW_CONFIG_REQ request timeout. */
  void fwConfigReqTimeout();

  /** Read node parameters from config and send them to firmware. */
  void initializeNodeParams(const std::string& radioMac);

  /** Process a node init acknowledgement from firmware. */
  void processNodeInitNotif(const thrift::Message& message);
  /** Process a firmware health report. */
  void processFwHealthReport(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a request to send node parameters to firmware. */
  void processSetNodeParams(
      const std::string& senderApp, const thrift::Message& message) noexcept;
  /** Process a request to send firmware stats config to all radios. */
  void processUpdateFwStatsConfig();
  /** Process a request to set this node's upgrade status. */
  void processSetUpgradeStatus(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a request for this node's GPS position. */
  void processGpsGetPos();
  /** Process a scan request. */
  void processScanReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a generic acknowledgement from firmware. */
  void processFwAck(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a status report acknowledgement from the E2E controller. */
  void processStatusReportAck(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a node reboot command. */
  void processRebootNode(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a request to set firmware configs. */
  void processFwConfigReq(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a response after setting firmware configs. */
  void processFwConfigResp(const thrift::Message& message);
  /** Process a minion restart command. */
  void processRestartMinion(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a request to fetch IPv6 neighbors. */
  void processGetMinionNeighborsReq(
      const std::string& senderApp, const thrift::Message& message);

  /** Process routing info from firmware. */
  void processFwRoutingInfo(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a request to upload a sysdump. */
  void processGetMinionSysdump(
      const std::string& senderApp, const thrift::Message& message);

  /**
   * Process a request to update/override link metrics with the current config.
   */
  void processUpdateLinkMetrics();

  /** Process a link up/down event. */
  void processLinkStatus(
      const std::string& senderApp, const thrift::Message& message);

  /**
   * Process backhaul baseband device up/down status.
   *
   * - If a device went DOWN, reset radioStatus_ members and notify IgnitionApp
   *   that all associated links are down.
   *
   * - If a device came UP, invoke sendNodeInit(), re-initialize network
   *   interfaces, and set myStatus_ to OFFLINE (so the controller re-sends
   *   SET_NODE_PARAMS).
   */
  void processDevUpDownStatus(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a status report request. */
  void processGetStatusReport(
      const std::string& senderApp, const thrift::Message& message);

  /**
   * Construct a status report.
   *
   * @param fullReport if false, constant fields (such as version strings) will
   *                   be omitted
   */
  thrift::StatusReport buildStatusReport(bool fullReport) const;

  /** Return a map of neighbor connections. */
  std::unordered_map<std::string, bool> getNeighborConnectionStatus() const;

  /**
   * Write stats and events, as necessary, for any BGP-related changes found.
   */
  void logBgpStatus(
      const std::unordered_map<std::string, thrift::BgpInfo>& bgpStatus) const;

  /**
   * If this node has BGP neighbors configured, then read info about its
   * neighbors, received routes, and advertised routes.
   */
  void updateBgpStatus();

  /**
   * Read the node's wireless firmware version (by running a command).
   *
   * If unsuccessful, return the command's error code.
   */
  folly::Expected<std::string, folly::ProcessReturnCode> getFirmwareVersion();

  /** Send a status report to the controller. */
  void reportStatus();

  /** Initiate a reboot (and uses "force" reboot if forcedReboot_ is set). */
  void reboot() const;

  /** Base interval to send status reports to the E2E controller. */
  std::chrono::seconds statusReportInterval_{0};

  /** Base interval to fetch this node's BGP status. */
  std::chrono::seconds bgpStatusInterval_{0};

  /** Interface name holding a globally-reachable IPv6 address. */
  std::string ipv6GlobalAddressableIfname_{""};

  /**
   * StatusApp will keep sending HELLO to DriverApp until it receives a reply.
   * It will then start other communications to DriverApp.
   */
  std::unique_ptr<fbzmq::ZmqTimeout> helloTimer_{nullptr};

  /** Timer to send status reports periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> statusReportTimer_{nullptr};

  /** Timer to fetch BGP status periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> bgpStatusTimer_{nullptr};

  /** This minion's status, in relation to the E2E controller. */
  thrift::NodeStatusType myStatus_{thrift::NodeStatusType::OFFLINE};

  /**
   * The status of all radios (e.g. MAC addresses).
   *
   * Radios that have never responded to NODE_INIT are not tracked here.
   * - initialized:
   *     - ONLINE: we have received NODE_INIT_NOTIFY from the radio
   *     - OFFLINE: we have received DR_DEV_UPDOWN_STATUS (DOWN) from the radio
   * - gpsSync => "true" upon successful FW_ACK for GPS_ENABLE_REQ
   * - vendor => vendor string from NODE_INIT_NOTIFY
   */
  std::unordered_map<std::string, thrift::RadioStatus> radioStatus_{};

  /**
   * The set of all radios (e.g. MAC addresses) that have had node parameters
   * (i.e. thrift::NodeParams) sent with at least polarity/channel filled in
   * (the required fields).
   */
  std::unordered_set<std::string> radiosWithAllNodeParamsSet_{};

  /**
   * Per-radio timeouts for `GPS_ENABLE_REQ` acknowledgements.
   *
   * Firmware does not send a negative `FW_ACK` when `forceGpsDisable=0` and it
   * fails to transition from `RF_SYNC` to `GPS_SYNC` state. We manually track
   * the requests here for logging purposes.
   */
  std::unordered_map<std::string, std::unique_ptr<fbzmq::ZmqTimeout>>
      gpsEnableTimers_{};

  /** Minion version. */
  std::string version_{};

  /** uboot version. */
  std::string ubootVersion_{};

  /** Hardware model. */
  std::string hardwareModel_{};

  /** Hardware board ID. */
  std::string hardwareBoardId_{};

  /** Minion software upgrade status. */
  thrift::UpgradeStatus upgradeStatus_;

  /**
   * Whether to send the full status report next interval.
   *
   * If false, the static strings (version, ubootVersion, hardwareModel) will
   * not be sent.
   */
  bool sendFullStatusReport_{true};

  /**
   * Used for reporting connectivity progress to the watchdog every time a
   * status acknowledgement is received from the controller.
   */
  Progress progress_;

  /** Timer to start the reboot process after receiving a reboot command. */
  std::unique_ptr<fbzmq::ZmqTimeout> rebootTimer_{nullptr};
  /** Whether the last reboot command requested a "forced" reboot. */
  bool forcedReboot_{false};

  /**
   * ZMQ ID of the app that requested FW_CONFIG_REQ.
   *
   * This is used to send FW_CONFIG_RESP back to it in response.
   */
  std::string fwConfigRequesterApp_;
  /**
   * Timer to stop waiting for `fwConfigReq` and send a failure notification
   * to the requester app.
   */
  std::unique_ptr<fbzmq::ZmqTimeout> fwConfigRequestTimeout_{nullptr};

  /** This node's type. */
  thrift::NodeType myNodeType_;

  /** Whether DPDK is enabled in node config. */
  bool dpdkEnabled_;

  /** This node's BGP status, updated periodically via bgpStatusTimer_. */
  std::optional<std::unordered_map<std::string, thrift::BgpInfo>> bgpStatus_;

  /** The timestamp at which the last status report ACK was received. */
  std::chrono::time_point<GpsClock> statusReportAckTimestamp_;

  /** Separate thread used in processGetMinionSysdump. */
  std::unique_ptr<std::thread> sysdumpThread_{nullptr};

  /** Timer to check for sysdump completion periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> sysdumpStatusTimer_{nullptr};

  /** Result of sysdump, nullptr if sysdump not running. */
  std::unique_ptr<thrift::MinionSysdumpComplete> sysdumpResp_{nullptr};

  /** Class holding link metric data. */
  class LinkMetricData {
   public:
    /** Constructor. */
    LinkMetricData(double genRate, double burstSize) {
      // Make sure we don't fatal with invalid values
      if (genRate <= 0) {
        genRate = 0.1;
      }
      if (burstSize <= 0) {
        burstSize = 3;
      }
      tokenBucket_ = std::make_unique<folly::TokenBucket>(genRate, burstSize);
    }

    /** The last routing metric set for this link. */
    int lastRoutingMetric_{0};

    /**
     * The metric computed from the last routing report (not necessarily set).
     * This is used to determine successive metric changes.
     */
    int newMetricCandidate_{0};

    /**
     * The number of successive metric changes computed from routing reports
     * (but not applied). The metric must have consistently moved in either
     * a positive or negative direction, or else this counter is reset.
     */
    int successiveChanges_{0};

    /** The token bucket that rate-limits link metric changes. */
    std::unique_ptr<folly::TokenBucket> tokenBucket_{nullptr};
  };

  /** Map of links (by MAC address) to link metric data. */
  std::unordered_map<std::string /* mac */, LinkMetricData> linkMetricData_{};

  /** Class for keeping track of the status report acknowledgement rate. */
  class AckMetricData {
   public:
    /** Constructor. */
    explicit AckMetricData(int nTrackedPings): nTrackedPings_(nTrackedPings) {}

    /**
     * Add an un-acked ping request to the queue.
     *
     * Get rid of stale ping data if pingQueue_ is full.
     */
    void recordPingRequest();

    /**
     * Record a response for the latest ping.
     *
     * Returns false if no pings tracked or most recent ping is already acked.
     */
    bool recordPingResponse();

    /**
     * Get the acknowledgement rate metric.
     *
     * Returns an optional double which represents the rate of pings acked.
     */
    std::optional<double> getAckRate();

   private:
    /** Maximum number of heartbeats we want to track for this metric. */
    int nTrackedPings_{0};

    /**
     * Number of acks from pings we are tracking.
     *
     * This is always <= `pingQueue_->size()`.
     */
    int nAcknowledgedPings_{0};

    /** Deque keeping track of pings and acks. */
    std::deque<uint64_t> pingQueue_;
  };

  /** Status report acknowledgement metric data. */
  std::unique_ptr<AckMetricData> srAckMetric_{nullptr};

  /** Network interface MAC address map. */
  std::unordered_map<std::string, std::string> networkInterfaceMacs_{};

  /** Wired network interface names. */
  std::vector<std::string> wiredInterfaceNames_{};
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
