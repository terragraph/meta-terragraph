/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>

#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/FwOptParams_types.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"

#include "MinionApp.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that handles link ignition (wireless link bring-up).
 *
 * This app primarily communicates with a separate IgnitionApp on the E2E
 * controller.
 */
class IgnitionApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * If operating in "secure mode" (wsec>=1), this will attempt to start a
   * wpa_supplicant process on every wireless interface (terra*).
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   * @param linkupRespWaitTimeout the amount of time to wait for a link status
   *                              event after sending a link-up command before
   *                              ignition times out
   * @param wsecEnable whether wireless security (wsec) is enabled
   */
  IgnitionApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr,
      const std::chrono::seconds linkupRespWaitTimeout,
      int64_t wsecEnable);

  /**
   * Destructor. This will kill all wsec processes (hostapd, wpa_supplicant).
   */
  ~IgnitionApp();

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** Information about ignition attempts to a responder radio. */
  struct DistributedIgnitionResponderState {
    /** The number of ignition attempts made. */
    int numAttempts{0};

    /** Timestamp of the most recent ignition attempt made. */
    std::optional<std::chrono::steady_clock::time_point> attemptTs;

    /**
     * The number of milliseconds that must pass before another ignition attempt
     * can be made, based on attemptTs.
     */
    std::optional<std::chrono::milliseconds> cooldownDuration;

    /** Whether ignition of this responder is disabled. */
    bool disabled{false};
  };

  /** State for a continuous topology scan loop. */
  struct ContinuousTopoScanState {
    /** The timer instance. */
    std::unique_ptr<fbzmq::ZmqTimeout> timer;

    /** The BWGD difference between consecutive topology scans. */
    uint64_t bwgdDelta;

    /** The steady_clock time point at which to stop looping. */
    std::chrono::steady_clock::time_point endTs;

    /* The BWGD index of the next topology scan to schedule. */
    uint64_t nextScanReqBwgd;
  };

  /**
   * Returns whether link encryption ("secure mode") should be used during
   * ignition.
   */
  bool isWsecEnabled() const;

  /** Returns whether WPA-PSK should be used during ignition. */
  bool isWpaPskEnabled() const;

  /** Returns whether 802.1X (EAPoL) should be used during ignition. */
  bool isEapolEnabled() const;

  /**
   * Process the SetLinkStatus message, sent from the controller's IgnitionApp
   * to either ignite or disassociate with a neighbor. This will delegate to
   * processLinkUpReq() or processLinkDownReq(), respectively.
   */
  void processSetLinkStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Bring up a link to the given neighbor. In this "ignition" process, this
   * node serves as the "initiator" and the other end as the "responder".
   *
   * Only one neighbor can be ignited at a time; if currResponderMac_ is already
   * set, then this function will be a no-op. The currResponderMac_ variable is
   * reset when we hear a new link status for the neighbor (DriverLinkStatus) or
   * after a timeout (clearCurrRespMacTimer_).
   *
   * If operating in "open mode" (wsec=0), this will immediately send out
   * DriverSetLinkStatus to ignite the neighbor. We should then receive a
   * LINK_UP event (DriverLinkStatus) when the link has been established.
   *
   * If operating in "secure mode" (wsec>=1), this will first send out
   * DriverDevAllocReq to request the interface name that the link will use.
   * We start the authenticator (hostapd) on that interface; the responder will
   * already be running the supplicant (wpa_supplicant). Afterwards, we proceed
   * to send out DriverSetLinkStatus to ignite the neighbor, as described above.
   */
  void handleSetLinkUpReq(const thrift::SetLinkStatus& setLinkStatus);

  /** Disassociate with the given neighbor. */
  void handleSetLinkDownReq(const thrift::SetLinkStatus& setLinkStatus);

  /**
   * Process the GetLinkStatus message, sent from the controller's TopologyApp
   * to learn this node's current link state to a neighbor.
   */
  void processGetLinkStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the GetLinkStatusDump message, sent from snmp agent on the node.
   *
   * Returns a list of all ignited links.
   */
  void processGetLinkStatusDump(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the BfRespScan message, sent from the controller's IgnitionApp to
   * temporarily enable BF responder mode.
   */
  void processBfRespScan(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the BfRespScan message, sent from the controller's IgnitionApp to
   * disable BF responder mode.
   */
  void processBfRespScanDisable(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /** Report the given link status to the controller. */
  void sendLinkStatus(
      const std::string& responderMac,
      const std::string& radioMac,
      const thrift::LinkStatusType& linkStatus,
      bool isEvent);

  /** Report the given link authorization status to the controller. */
  void sendLinkAuthorized(
      const std::string& responderMac,
      const std::string& radioMac,
      const std::string& ifname,
      int64_t wsecEnable);

  /** Send FW_BF_RESP_SCAN to the driver. */
  void sendFwBfRespScan(const std::string& radioMac, bool enable);

  /** Schedule BF_RESP_SCAN command after the given time interval. */
  void scheduleFwBfRespScan(
      const std::string& radioMac, bool enable, int delaySec);

  /** Log and pass a DriverLinkStatus message to handleDriverLinkStatus(). */
  void processDriverLinkStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the DriverLinkStatus message, which informs us of the current
   * driver link state (UP, DOWN, PAUSE, etc.) to a neighbor.
   *
   * If operating in "secure mode" (wsec>=1), a LINK_DOWN message will kill the
   * authenticator (hostapd) and restart supplicant (wpa_supplicant) on the
   * link's interface.
   */
  void handleDriverLinkStatus(
      const thrift::DriverLinkStatus& drLinkStatus,
      const std::string& radioMac) noexcept;

  /**
   * Process the DriverDevAllocRes message, which contains the interface name
   * used for the current responder node. If wsec is enabled, we will kill the
   * running supplicant process and start the authenticator on this interface.
   */
  void processDriverDevAllocRes(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the DriverWsecStatus message, notifying us that an interface
   * should be allowed to connect in "open mode" (wsec=0) even if we are
   * configured for "secure mode" (wsec>=1). Kill any running supplicant or
   * authenticator on this interface, as these processes will block insecure
   * connections from completing.
   *
   * This message should only arrive *after* we have received a corresponding
   * LINK_UP (DriverLinkStatus) for this link.
   */
  void processDriverWsecStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the DriverWsecLinkupStatus message, notifying us that the secure
   * port is authorized on the given interface. When operating in "secure mode"
   * (wsec>=1), we must wait for this message on the interface being ignited
   * before trying to ignite any other links.
   */
  void processDriverWsecLinkupStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the DriverDevUpDownStatus message (forwarded from StatusApp),
   * notifying us that a backhaul baseband device went offline. We need to mark
   * any associated links as down.
   */
  void processDevUpDownStatus(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the BfSlotExclusionReq message, notifying us to begin slot
   * exclusion for IBF.
   */
  void processBfSlotExclusionReq(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /**
   * Process the StartContinuousTopoScan message, notifying us to begin running
   * topology scans continuously.
   */
  void processStartContinuousTopoScan(
      const std::string& senderApp, const thrift::Message& message) noexcept;

  /** Execute one step in the continuous topology scan loop. */
  void continuousTopoScanStep(const std::string& radioMac) noexcept;

  /**
   * Process the GpsEnabled message, notifying us that GPS has been
   * successfully enabled on the node.
   */
  void processGpsEnabled(const std::string& senderApp) noexcept;

  /**
   * Process the ControllerConnected message, notifying us that a connection
   * has successfully been established with the controller.
   */
  void processControllerConnected(const std::string& senderApp) noexcept;

  /** Returns the number of milliseconds until the next ignition boundary. */
  std::chrono::milliseconds getTimeToNextIgnitionBoundary();

  /** Attempt to ignite a link to a neighbor defined in node config. */
  void selfIgnite() noexcept;

  /** Returns the wsec config parameters. */
  thrift::WsecParams getWsecParams();

  /** Reset the ignition state for the current link being ignited. */
  void resetCurrentIgnitionState();

  /**
   * Start/stop the ignition timer which triggers resetCurrentIgnitionState().
   */
  void setIgnitionTimer(bool start);

  /**
   * For each given interface, kill any existing hostapd/wpa_supplicant (if
   * killFirst=true) and starts wpa_supplicant.
   */
  void startSupplicants(
      const std::unordered_set<std::string>& ifaces, bool killFirst);

  /**
   * Retreive secrets based on hardware.
   *
   * hostapd and wpa_supplicant need some passwords to work. These are stored in
   * secure locations and require vendor-specific methods for retrieval.
   */
  std::string getValueforSecret(const std::string& key);

  /**
   * Fork a process using the given command, and optionally block until exit.
   *
   * Returns true on success:
   * - If non-blocking, success if fork didn't fail.
   * - If blocking, success if the child process returned an exit code of 0.
   */
  bool forkProcess(const std::string& command, bool waitForExit);

  /** Topology info for this node. */
  thrift::TopologyInfo topologyInfo_;

  /** Whether distributed ignition is enabled. */
  bool distributedIgnitionEnabled_{false};

  /** Candidate links for distributed ignition. */
  std::vector<thrift::SetLinkStatus> linkCandidates_{};

  /**
   * Index for iteration in the list of distributed ignition link candidates,
   * used to ignite links in a round-robin fashion.
   */
  std::size_t distributedIgnitionLinkIterationIndex_{0};

  /**
   * Mapping from the responder node to information about ignition attempts for
   * this link.
   */
  std::unordered_map<std::string, DistributedIgnitionResponderState>
      distributedIgnitionResponderState_;

  /** Timer to attempt distributed ignition on offline links. */
  std::unique_ptr<fbzmq::ZmqTimeout> distributedIgnitionTimer_{nullptr};

  /**
   * Only one ignition is possible at a time. This variable is set to the MAC
   * address of the neighbor currently being ignited. It is empty if no neighbor
   * is being ignited.
   */
  std::string currResponderMac_{};

  /** The MAC address of the radio performing the current ignition, if any. */
  std::string currInitiatorMac_{};

  /**
   * If hostapd has been started for the neighbor currently being ignited, this
   * variable holds the interface name that hostapd is bound to.
  */
  std::string currResponderHostapdIface_{};

  /**
   * When operating in "secure mode" (wsec>=1), we expect a LINK_UP
   * (DriverLinkStatus) status event before DR_WSEC_LINKUP_STATUS. This variable
   * holds the interface name from the LINK_UP event.
  */
  std::string currResponderLinkUpIface_{};

  /**
   * Queue all LINK_DOWN notifications that occur during "secure mode" (wsec>=1)
   * when waiting for the 4-way handshake to complete. We must not start/stop
   * any authenticator/supplicant processes on any interface during this time.
  */
  std::unordered_set<std::string> linkDownIfaceQueue_{};

  /**
   * The pending DriverSetLinkStatus message to send to the neighbor currently
   * being ignited, after we receive the DriverDevAllocRes response.
  */
  thrift::DriverSetLinkStatus currResponderPendingMsg_{};

  /**
   * Clear currentResponderMac_ if we don't get DriverLinkStatus within
   * linkupRespWaitTimeout_. This way we prevent the ignition process from
   * getting stuck. If currResponderHostapdIface_ is set, this will also
   * kill hostapd on the interface and restart wpa_supplicant.
  */
  std::chrono::seconds linkupRespWaitTimeout_{0};
  /** Timer associated with linkupRespWaitTimeout_. */
  std::unique_ptr<fbzmq::ZmqTimeout> clearCurrRespMacTimer_{nullptr};

  /**
   * Link encryption ("secure mode") configuration.
   *
   * This value is read from config on startup and persisted for the lifetime
   * of the process.
  */
  int64_t wsecEnable_{0};

  /**
   * Toggle BF responder mode (per-radio) before the next ignition cycle, or
   * upon receiving BF_RESP_SCAN_DISABLE.
   */
  std::unordered_map<
      std::string /* radioMac*/, std::unique_ptr<fbzmq::ZmqTimeout>>
          bfRespScanTimerMap_;

  /** This node's type. */
  thrift::NodeType myNodeType_;

  /** List of terra interfaces. */
  std::vector<std::string> terraIfList_;

  /** State associated with continuous topology scans (per-radio). */
  std::unordered_map<std::string /* radioMac*/, ContinuousTopoScanState>
      continuousTopoScanState_;

  /** Per-radio timer, for handling being in LINK_PAUSE for too long */
  std::unordered_map<
      std::string /* radioMac*/,
      std::unique_ptr<fbzmq::ZmqTimeout>>
      linkPauseTimers_;
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
