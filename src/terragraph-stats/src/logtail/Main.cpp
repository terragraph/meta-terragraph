/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "EventParser.h"
#include "LogPublisher.h"

#include <fbzmq/zmq/Context.h>
#include <folly/init/Init.h>
#include <folly/system/ThreadName.h>
#include <gflags/gflags.h>

DEFINE_string(
    my_network_info_file, "/tmp/mynetworkinfo", "Network information file");

// General options
DEFINE_string(node_id, "", "Node ID");
DEFINE_string(
    node_config_file, "/data/cfg/node_config.json", "Node configuration file");
DEFINE_int32(submission_interval_s, 2, "Periodicity for processing logs");
DEFINE_uint32(buffer_size, 200, "Size of buffer holding file tail data");

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  fbzmq::Context context;

  // fail if we're missing the node id
  if (FLAGS_node_id.empty()) {
    LOG(FATAL) << "Empty node id";
  }

  facebook::terragraph::stats::LogPublisher logPublisher(
      context,
      FLAGS_node_id,
      FLAGS_node_config_file,
      FLAGS_submission_interval_s,
      FLAGS_buffer_size,
      FLAGS_my_network_info_file);
  std::thread logPublisherThread([&logPublisher]() {
    LOG(INFO) << "Starting LogPublisher thread...";
    folly::setThreadName("LogPublisher");
    logPublisher.run();
    LOG(INFO) << "LogPublisher thread got stopped...";
  });

  facebook::terragraph::stats::EventParser eventParser(
      context,
      FLAGS_node_id,
      FLAGS_node_config_file,
      FLAGS_submission_interval_s,
      FLAGS_buffer_size);
  std::thread eventParserThread([&eventParser]() {
    LOG(INFO) << "Starting EventParser thread...";
    folly::setThreadName("EventParser");
    eventParser.run();
    LOG(INFO) << "EventParser thread got stopped...";
  });

  logPublisherThread.join();
  eventParserThread.join();

  return 0;
}
