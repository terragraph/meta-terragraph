/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TcpServer.h"
#include "mobile_common/Const.h"
#include "mobile_common/Message.pb.h"
#include "mobile_common/MessageHandler.h"
#include "mobile_common/Parser.h"
#include "mobile_common/Utils.h"

#include <boost/asio.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <iostream>

DEFINE_int32(tcp_port, 12345, "tcp port");
DEFINE_int32(tcp_timeout_s, 30, "tcp timeout seconds");

namespace ba = boost::asio;

namespace facebook {
namespace terragraph {
namespace mobd {

namespace {
ba::io_service io;
ba::ip::tcp::socket sock(io);
ba::deadline_timer timer(io);
Parser parser(MAX_PAYLOAD_SIZE);
bool conn_drop_on_timeout = true;
char rx_buf[UART_BUF_SIZE];
char tx_buf[UART_BUF_SIZE];
char plain_buf[UART_BUF_SIZE]; // for encrypting serialized response
int tx_index = 0;
std::size_t tx_bytes_left = 0;
} // namespace

// Timer event handler.
// Drop the tcp connection when it is inactive for at least
// tcp_timeout_s seconds. Canceling all pending events causes
// the event loop - io.run() - to unblock in tcpServerStart()
// which then accepts a new connection.
//
// A timer event is always pending after a connection is
// accepted. The 'conn_drop_on_timeout' flag is cleared by
// the RX/TX handlers, and set by this callback.
static void
connectionTimeout(const boost::system::error_code& ec) {
  if (ec) {
    VLOG(1) << "Timeout " << ec.message();
    return; // error or cancel
  }
  if (conn_drop_on_timeout) {
    LOG(INFO) << "timeout expired, dropping connection";
    timer.cancel();
    sock.cancel();
    tx_bytes_left = 0;
    tx_index = 0;
    return;
  } else {
    LOG(INFO) << "timeout expired, no reset";
    conn_drop_on_timeout = true;
    timer.expires_from_now(boost::posix_time::seconds(FLAGS_tcp_timeout_s));
    timer.async_wait(connectionTimeout);
  }
}

// Socket write completion handler.
// Keep writing until the complete message is sent.
// Prevent connection timeout during send.
static void
socketTx(const boost::system::error_code& ec, std::size_t num_bytes) {
  if (ec) {
    VLOG(1) << "TX " << ec.message();
    sock.cancel();
    timer.cancel();
    return; // error or cancel, drop the connection
  }
  if (num_bytes >= tx_bytes_left) {
    VLOG(1) << "TX " << num_bytes << " bytes. TX req done.\n";
  } else {
    VLOG(1) << "TX " << num_bytes << " bytes";
    // Don't drop connection while we are sending.
    conn_drop_on_timeout = false;
    tx_index += num_bytes;
    tx_bytes_left -= num_bytes;
    sock.async_write_some(
        ba::buffer(&tx_buf[tx_index], tx_bytes_left), socketTx);
  }
}

// Socket read completion handler.
// Keep reading until socket close/error.
// Prevent connection timeout while valid requests are arriving.
// Send response (async) when a valid message is found.
static void
socketRx(const boost::system::error_code& ec, std::size_t num_bytes) {
  if (ec) {
    VLOG(1) << "RX " << ec.message();
    sock.cancel();
    timer.cancel();
    return; // error or cancel, drop the connection
  }
  VLOG(1) << "RX " << num_bytes << " bytes";
  hexdump(rx_buf, num_bytes);
  for (std::size_t i = 0; i < num_bytes; i++) {
    const Message* m = parser.parse(rx_buf[i]);
    if (m == nullptr) {
      continue; // no valid message yet
    }
    // Deserialized and decrypted a message with a good crc.
    // Don't drop connection while we are receiving valid messages.
    conn_drop_on_timeout = false;
    const Message* response = messageHandler(*m);
    if (response != nullptr) { // message is authentic, so send response
      tx_bytes_left = serialize(*response, tx_buf, plain_buf, sizeof(tx_buf));
      VLOG(1) << "response size " << tx_bytes_left;
      tx_index = 0;
      sock.async_write_some(ba::buffer(tx_buf, tx_bytes_left), socketTx);
    }
  }
  // Handled all the received bytes. Keep the socket open
  // and look for more requests.
  sock.async_read_some(ba::buffer(rx_buf, sizeof(rx_buf)), socketRx);
}

void
tcpServerStart() {
  while (true) {
    LOG(INFO) << "Listening on tcp port " << FLAGS_tcp_port;
    ba::ip::tcp::acceptor acceptor(
        io, ba::ip::tcp::endpoint(ba::ip::tcp::v4(), FLAGS_tcp_port));
    acceptor.accept(sock);
    LOG(INFO) << "Accepted connection";
    timer.expires_from_now(boost::posix_time::seconds(FLAGS_tcp_timeout_s));
    timer.async_wait(connectionTimeout);
    sock.async_read_some(ba::buffer(rx_buf, sizeof(rx_buf)), socketRx);
    io.run();
    VLOG(1) << "Cleaning up";
    sock.close();
    conn_drop_on_timeout = true;
    parser.reset();
    resetAuthentication();
    io.reset();
  }
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
