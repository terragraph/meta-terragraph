/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "mobile_common/Message.pb.h"
#include <boost/asio.hpp>

namespace TestUtils {

// Serialize and send a message
void send(
    boost::asio::ip::tcp::socket& s,
    const facebook::terragraph::mobd::Message& message);

// Read socket until valid message is received, then print it out.
// Return: true if the received message is a response to "request".
bool recv(
    boost::asio::ip::tcp::socket& s,
    const facebook::terragraph::mobd::Message& request);
} // namespace TestUtils
