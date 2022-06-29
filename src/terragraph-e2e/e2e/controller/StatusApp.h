/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/Controller_types.h"

#include "CtrlApp.h"

#include <deque>

namespace facebook {
namespace terragraph {

/**
 * App that oversees the status of the network.
 *
 * This app primarily communicates with a separate StatusApp on the E2E minion.
 */
class StatusApp final : public CtrlApp {
 public:
  /**
   * Constructor.
   *
   * @param zmqContext the ZMQ context
   * @param routerSockUrl the broker address for the E2E controller
   * @param monitorSockUrl the ZmqMonitor address for the E2E controller
   * @param statusReportThrottleInterval the interval at which to throttle
   *                                     status reports from the same minion
   * @param fullStatusReportInterval the interval at which to request full
   *                                 status reports from nodes (normally, some
   *                                 static data is omitted to save bandwidth)
   * @param versionFile the path to the current software version file
   */
  StatusApp(
      fbzmq::Context& zmqContext,
      const std::string& routerSockUrl,
      const std::string& monitorSockUrl,
      std::chrono::seconds statusReportThrottleInterval,
      std::chrono::seconds fullStatusReportInterval,
      const std::string& versionFile);

  /**
   * Supplemental data associated with minion status reports.
   *
   * This keeps track of report timestamps monotonically.
   */
  class StatusReport {
   public:
     /** Empty constructor. */
     explicit StatusReport() : steadyTs(), report(), lastFullReportTs() {}
     /** Constructor. */
     explicit StatusReport(
       std::chrono::steady_clock::time_point timeStamp,
       thrift::StatusReport statusReport)
       : steadyTs(timeStamp),
         report(statusReport),
         lastFullReportTs(timeStamp) {};

    /** The time when the last status report was received. */
    std::chrono::steady_clock::time_point steadyTs;

    /** The last status report received. */
    thrift::StatusReport report;

    /**
     * The time when the last FULL status report (e.g. without omitted data)
     * was received (in monotonic seconds).
     */
    std::chrono::steady_clock::time_point lastFullReportTs;
  };

 private:
  // from CtrlApp
  void processMessage(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

  /** Process a sysdump request. */
  void processGetSysdump(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a minion notification that a sysdump has completed. */
  void processMinionSysdumpComplete(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a minion notification that a sysdump has started. */
  void processMinionSysdumpStarted(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a status report. */
  void processStatusReport(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request for a status dump. */
  void processGetStatusDump(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a node reboot request. */
  void processRebootRequest(
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a minion restart request. */
  void processRestartMinionRequest(
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a request for the current GPS time. */
  void processGetGpsTime(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a request for neighbor information from all nodes. */
  void processGetCtrlNeighborsReq(
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process a neighbors response from an E2E minion. */
  void processGetMinionNeighborsResp(
      const std::string& minion,
      const std::string& senderApp,
      const thrift::Message& message);

  /** Process an "is alive" health check of the controller. */
  void processIsAlive(const std::string& senderApp);

  /** Update GPS clock epoch with GPS time data from a minion status report. */
  void updateGpsTime(
      const thrift::StatusReport& statusReport,
      const StatusReport& lastStatusReport,
      const std::string& minion);

  /**
   * Given a GPS timestamp reported from a node, returns true if the timestamp
   * is determined to be an outlier compared to GPS timestamps reported from
   * other nodes.
   */
  bool gpsTimestampIsOutlier(std::chrono::microseconds gpsTime);

  /**
   * Push a GPS timestamp onto the queue of timestamps while ensuring it keeps
   * its max size.
   */
  void addGpsTimestamp(int64_t gpsTimestamp);

  /** Drop status report packets which come within this throttle interval. */
  std::chrono::seconds statusReportThrottleInterval_;

  /** Request full status reports from minions at this interval. */
  std::chrono::seconds fullStatusReportInterval_;

  /** The controller's software version string. */
  std::string version_;

  /** Queue of the latest GPS timestamps received from nodes. */
  std::deque<int64_t> latestGpsTimestamps_{};
};

} // namespace terragraph
} // namespace facebook
