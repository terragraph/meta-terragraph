/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TestUtils.h"

#include "mobile_common/Const.h"
#include "mobile_common/Crypto.h"
#include "mobile_common/Message.pb.h"
#include "mobile_common/UnitTest.h"
#include "mobile_common/Utils.h"

#include <boost/array.hpp>
#include <boost/asio.hpp>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include <chrono>
#include <iostream>

DEFINE_bool(
    encryption,
    false,
    "Enable AES enryption. Must also be enabled in the server.");
DEFINE_bool(unittest, false, "Run the unit tests only");
DEFINE_string(ip, "192.168.4.1", "IPv4 address");
DEFINE_int32(port, 12345, "TCP port");
DEFINE_string(cmd, "nodeinfo", "Command: nodeinfo, test, assoc, dissoc, stats");
DEFINE_int32(bytes, 100, "Number of TEST bytes to request.");
DEFINE_int32(
    duration,
    0,
    "When positive, keep sending requests and receiving replies for at least"
    "this many seconds, with incremental cookie values.");
DEFINE_int32(cookie, 0, "request: message cookie");
DEFINE_int32(wdog, 10, "request: wdog disable minutes");
DEFINE_string(init, "01:02:03:04:05:06", "assoc/dissoc: initiator mac address");
DEFINE_string(resp, "01:02:03:04:05:07", "assoc/dissoc: responder mac address");
DEFINE_int32(samples, 3, "number of snr/rssi samples to request");

namespace ftm = facebook::terragraph::mobd;
namespace ba = boost::asio;

enum Command {
  REQ_UNKNOWN = 0,
  REQ_NODE_INFO = 1,
  REQ_TEST = 2,
  REQ_ASSOC = 3,
  REQ_DISSOC = 4,
  REQ_STATS = 5
};

static std::string commands[] = {
    "nodeinfo", "test", "assoc", "dissoc", "stats"};

int
main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FLAGS_logtostderr = true;

  // Perform unit tests
  if (FLAGS_unittest) {
    if (ftm::unitTest()) {
      LOG(INFO) << "Unit tests passed.";
      return 0;
    } else {
      LOG(INFO) << "Unit tests failed.";
      return 1;
    }
  }

  // Sanity check the command
  enum Command command = REQ_UNKNOWN;
  for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
    if (FLAGS_cmd == commands[i]) {
      command = static_cast<Command>(i + 1);
      break;
    }
  }
  if (command == REQ_UNKNOWN) {
    LOG(ERROR) << "Unknown command " << FLAGS_cmd;
    return 1;
  }

  // Set the default AES key
  ftm::crypto::init(ftm::DEFAULT_KEYGEN_PHRASE);

  int num_responses = 0;
  int bad_responses = 0;
  try {
    // Create a socket and connect to the ESP32
    LOG(INFO) << "Connecting to " << FLAGS_ip << ":" << FLAGS_port;
    ba::io_service io_service;
    ba::ip::tcp::socket socket(io_service);
    socket.connect(ba::ip::tcp::endpoint(
        ba::ip::address::from_string(FLAGS_ip), FLAGS_port));
    LOG(INFO) << "Connected!";

    // Note the test start time
    LOG(INFO) << "Starting test.";
    auto start_time = std::chrono::steady_clock::now();
    auto cookie = FLAGS_cookie;
    ftm::Message m;
    bool done = false;

    while (!done) {
      // Construct the request message
      std::string request_body;
      switch (command) {
        case REQ_TEST: {
          ftm::Test test;
          test.set_num_test_bytes(FLAGS_bytes);
          test.SerializeToString(&request_body);
          m.set_message_body(request_body);
          m.set_message_type(ftm::Message::TEST);
        } break;
        case REQ_NODE_INFO: {
          m.set_message_type(ftm::Message::NODE_INFO);
        } break;
        case REQ_ASSOC:
        case REQ_DISSOC: {
          ftm::IgniteLink igniteLink;
          igniteLink.set_initiator_mac(FLAGS_init);
          igniteLink.set_responder_mac(FLAGS_resp);
          igniteLink.set_ignite(command == REQ_ASSOC);
          igniteLink.SerializeToString(&request_body);
          m.set_message_body(request_body);
          m.set_message_type(ftm::Message::IGNITE_LINK);
        } break;
        case REQ_STATS: {
          ftm::LinkStats linkStats;
          linkStats.set_initiator_mac(FLAGS_init);
          linkStats.set_responder_mac(FLAGS_resp);
          linkStats.set_timeout_sec(FLAGS_samples);
          linkStats.SerializeToString(&request_body);
          m.set_message_body(request_body);
          m.set_message_type(ftm::Message::LINK_STATS);
        } break;
        default:
          LOG(ERROR) << "Invalid command " << command;
          return 1;
      }

      m.set_cookie(cookie);
      m.set_wdog_disable_minutes(FLAGS_wdog);

      // Serialize and send the request message
      TestUtils::send(socket, m); // produces detailed logs

      // Receive, deserialize, sanity check, and print the response
      LOG(INFO) << "Wating for response...";
      if (!TestUtils::recv(socket, m)) {
        bad_responses++;
      }
      num_responses++;

      // Check if we are done
      auto now = std::chrono::steady_clock::now();
      auto elapsed =
          std::chrono::duration_cast<std::chrono::seconds>(now - start_time)
              .count();
      if (FLAGS_duration <= 0 || elapsed >= FLAGS_duration) {
        done = true;
      } else {
        cookie++; // bump the message sequence number
        m.Clear();
      }
    } // while (!done)

    if (FLAGS_duration > 0) {
      LOG(INFO) << "Completed " << FLAGS_duration << " second test";
    }

    socket.shutdown(ba::ip::tcp::socket::shutdown_both);
    socket.close();
  } catch (std::exception& e) {
    LOG(ERROR) << "Caught " << e.what();
    return 1;
  }
  LOG(INFO) << "Responses " << num_responses << " Bad " << bad_responses;
  return (bad_responses == 0) ? 0 : 1;
}
