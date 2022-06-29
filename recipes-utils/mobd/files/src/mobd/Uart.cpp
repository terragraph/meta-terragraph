/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mobile_common/Const.h"
#include "mobile_common/Message.pb.h"
#include "mobile_common/MessageHandler.h"
#include "mobile_common/Parser.h"
#include "mobile_common/Utils.h"

#include <boost/asio.hpp>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstring>
#include <errno.h>
#include <iostream>
#include <termios.h> // for tcflush

DEFINE_int32(baud, 115200, "Baud rate");
DEFINE_string(port, "/dev/ttyS0", "Serial port");
DEFINE_int32(timeout, 30, "UART timeout seconds");

static boost::asio::io_service io;
static boost::asio::serial_port port(io);
static boost::asio::deadline_timer timer(io);

namespace facebook {
namespace terragraph {
namespace mobd {

static Parser parser(MAX_PAYLOAD_SIZE);
static bool uart_reset_on_timeout = true;
static char rx_buf[UART_BUF_SIZE];
static char tx_buf[UART_BUF_SIZE];
static char plain_buf[UART_BUF_SIZE]; // for encrypting serialized response
static int tx_index = 0;
static std::size_t tx_bytes_left = 0;

// Forward declaration
static void uartRx(const boost::system::error_code& ec, std::size_t num_bytes);

static void
uartFlush() {
  if (0 != ::tcflush(port.lowest_layer().native_handle(), TCIOFLUSH)) {
    LOG(ERROR) << "tcflush failed " << std::strerror(errno);
  }
}

static void
uartTimeout(const boost::system::error_code& ec) {
  if (ec) {
    VLOG(1) << "Timeout " << ec.message();
    return; // error or cancel
  }
  if (uart_reset_on_timeout) {
    LOG(INFO) << "timeout expired, reset";
    port.cancel();
    uartFlush();
    parser.reset();
    resetAuthentication();
    tx_bytes_left = 0;
    tx_index = 0;
    port.async_read_some(boost::asio::buffer(rx_buf, sizeof(rx_buf)), uartRx);
  } else {
    LOG(INFO) << "timeout expired, no reset";
  }
  uart_reset_on_timeout = true;
  timer.expires_from_now(boost::posix_time::seconds(FLAGS_timeout));
  timer.async_wait(uartTimeout);
}

static void
uartTx(const boost::system::error_code& ec, std::size_t num_bytes) {
  if (ec) {
    VLOG(1) << "TX " << ec.message();
    return; // error or cancel
  }
  if (num_bytes >= tx_bytes_left) {
    VLOG(1) << "TX " << num_bytes << " bytes. TX req done.\n";
  } else {
    VLOG(1) << "TX " << num_bytes << " bytes";
    // Don't reset uart while we are sending.
    uart_reset_on_timeout = false;
    tx_index += num_bytes;
    tx_bytes_left -= num_bytes;
    port.async_write_some(
        boost::asio::buffer(&tx_buf[tx_index], tx_bytes_left), uartTx);
  }
}

static void
uartRx(const boost::system::error_code& ec, std::size_t num_bytes) {
  if (ec) {
    VLOG(1) << "RX " << ec.message();
    return; // error or cancel
  }
  VLOG(1) << "RX " << num_bytes << " bytes";
  hexdump(rx_buf, num_bytes);
  for (std::size_t i = 0; i < num_bytes; i++) {
    const Message* m = parser.parse(rx_buf[i]);
    if (m == nullptr) {
      continue; // no valid message yet
    }
    // Deserialized and decrypted a message with a good crc.
    // Don't reset uart while we are receiving valid messages.
    uart_reset_on_timeout = false;
    const Message* response = messageHandler(*m);
    if (response != nullptr) { // message is authentic, so send response
      tx_bytes_left = serialize(*response, tx_buf, plain_buf, sizeof(tx_buf));
      VLOG(1) << "response size " << tx_bytes_left;
      tx_index = 0;
      port.async_write_some(boost::asio::buffer(tx_buf, tx_bytes_left), uartTx);
    }
  }
  port.async_read_some(boost::asio::buffer(rx_buf, sizeof(rx_buf)), uartRx);
}

void
uartListen() {
  port.open(FLAGS_port);
  port.set_option(boost::asio::serial_port_base::baud_rate(FLAGS_baud));
  uartFlush();
  timer.expires_from_now(boost::posix_time::seconds(FLAGS_timeout));
  timer.async_wait(uartTimeout);
  port.async_read_some(boost::asio::buffer(rx_buf, sizeof(rx_buf)), uartRx);
  LOG(INFO) << "Listening on " << FLAGS_port;
  io.run();
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
