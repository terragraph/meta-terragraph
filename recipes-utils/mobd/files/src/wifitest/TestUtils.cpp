/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TestUtils.h"

#include "mobile_common/Const.h"
#include "mobile_common/Message.pb.h"
#include "mobile_common/Parser.h"
#include "mobile_common/Utils.h"
#include <boost/asio.hpp>
#include <glog/logging.h>

using namespace facebook::terragraph::mobd;
namespace ba = boost::asio;

// Dump message in human readable form.
static void
dumpMessage(const Message& m, bool useverb = true) {

  if (useverb && !VLOG_IS_ON(1)) {
    // least verbose message log
    auto desc = Message_MessageType_descriptor();
    auto mtype = desc->FindValueByNumber(m.message_type())->name();
    LOG(INFO) << "message_type: " << mtype << " cookie: " << m.cookie();
    if (m.has_error_message()) {
      LOG(INFO) << "error_message: " << m.error_message();
    }
    return;
  }

  LOG(INFO) << m.ShortDebugString();
  if (!m.has_message_body()) {
    LOG(INFO) << "No message_body";
    return;
  }

  switch (m.message_type()) {
    case Message::HELLO: {
      Hello mb;
      if (mb.ParseFromString(m.message_body())) {
        LOG(INFO) << mb.ShortDebugString();
      }
    } break;
    case Message::NODE_INFO: {
      NodeInfo mb;
      if (mb.ParseFromString(m.message_body())) {
        LOG(INFO) << mb.ShortDebugString();
      }
    } break;
    case Message::IGNITE_LINK: {
      IgniteLink mb;
      if (mb.ParseFromString(m.message_body())) {
        LOG(INFO) << mb.ShortDebugString();
      }
    } break;
    case Message::TEST: {
      Test mb;
      if (mb.ParseFromString(m.message_body())) {
        LOG(INFO) << mb.ShortDebugString();
      }
    } break;
    case Message::LINK_STATS: {
      LinkStats mb;
      if (mb.ParseFromString(m.message_body())) {
        LOG(INFO) << mb.ShortDebugString();
      }
    } break;
    default: {
      LOG(INFO) << "Unhandled message_body";
    } break;
  }
}

// Serialize and send a message
void
TestUtils::send(ba::ip::tcp::socket& s, const Message& m) {
  LOG(INFO) << " ---- REQUEST -----";
  dumpMessage(m, false /* force more verbose dump */);
  std::unique_ptr<char[]> wire(new char[UART_BUF_SIZE]);
  std::unique_ptr<char[]> plain(new char[UART_BUF_SIZE]);
  int len = ::serialize(m, wire.get(), plain.get(), UART_BUF_SIZE);
  VLOG(1) << "Serialized request is " << len << " bytes";
  hexdump(wire.get(), len, 8);
  int nw = 0;
  while (nw < len) { // TODO: use asio with timeout
    int n = s.write_some(ba::buffer(&wire[nw], len - nw));
    VLOG(1) << "Sent " << n << " request bytes";
    hexdump(&wire[nw], n, 8);
    nw += n;
  }
}

// Read socket until a valid message is received, then print it out
// Return: true if the received message is a response to "request".
bool
TestUtils::recv(ba::ip::tcp::socket& s, const Message& request) {
  LOG(INFO) << " ---- RESPONSE -----";
  Parser parser(MAX_PAYLOAD_SIZE);
  while (true) { // TODO: use asio with timeout
    char buf[256];
    auto nr = s.read_some(ba::buffer(buf, sizeof(buf)));
    VLOG(1) << "Received " << nr << " response bytes";
    hexdump(buf, nr, 8);
    for (int i = 0; i < int(nr); i++) {
      const Message* resp = parser.parse(buf[i]);
      if (resp != nullptr) {
        dumpMessage(*resp);
        return resp->cookie() == request.cookie() &&
               resp->message_type() == request.message_type();
      }
    }
  }
}
