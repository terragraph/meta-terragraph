/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <memory>

#include <signal.h>

#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>

#include "MinionClient.h"
#include "StatCache.h"
#include "StatsSubscriber.h"

extern "C" {
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
}

#include "Access.h"
#include "Agent.h"

using namespace facebook::terragraph;

DEFINE_string(
    netsnmp_app_name,
    "tg_snmp_agent",
    "The name of the application to register with net-snmp");

namespace {
  // static key name mapping
  const std::vector<StatFormat> kStatsFormat{
      StatFormat("phystatus.ssnrEst", "tgf", SnmpColumn::SNR),
      StatFormat("phystatus.srssi", "tgf", SnmpColumn::RSSI),
      StatFormat("staPkt.mcs", "tgf", SnmpColumn::MCS)};
}

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  // we are a subagent
  netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE, 1);

  // initialize tcpip, if necessary
  SOCK_STARTUP;

  // initialize the agent library
  init_agent(FLAGS_netsnmp_app_name.c_str());

  fbzmq::Context context;

  // fetch list of links
  std::unique_ptr<facebook::terragraph::MinionClient> minionClient =
      std::make_unique<facebook::terragraph::MinionClient>(
          context, kStatsFormat);
  std::thread minionClientThread = std::thread([&context, &minionClient]() {
    LOG(INFO) << "Starting MinionClient thread...";
    folly::setThreadName("MinionClient");
    minionClient->run();
    LOG(INFO) << "MinionClient thread got stopped";
  });
  minionClient->waitUntilRunning();

  // start listening for stats data
  std::unique_ptr<facebook::terragraph::StatsSubscriber> statsSubscriber =
      std::make_unique<facebook::terragraph::StatsSubscriber>(context);
  std::thread statsSubscriberThread =
      std::thread([&context, &statsSubscriber]() {
        LOG(INFO) << "Starting StatsSubscriber thread...";
        folly::setThreadName("StatsPuStatsSubscriberblisher");
        statsSubscriber->run();
        LOG(INFO) << "StatsSubscriber thread got stopped";
      });
  statsSubscriber->waitUntilRunning();

  // initialize snmp agent providing access to the most recent stats
  initAgent();

  // register with net-snmp agent
  init_snmp(FLAGS_netsnmp_app_name.c_str());

  // main loop
  while (true) {
    agent_check_and_process(1);
  }

  // shutdown stats subscriber
  statsSubscriber->stop();
  statsSubscriber->waitUntilStopped();

  // shutdown minion client
  minionClient->stop();
  minionClient->waitUntilStopped();

  // wait for all threads to finish
  minionClientThread.join();
  statsSubscriberThread.join();

  // shutdown agent handler
  snmp_shutdown(FLAGS_netsnmp_app_name.c_str());

  // shutdown the agent library
  shutdown_agent();
  SOCK_CLEANUP;
}
