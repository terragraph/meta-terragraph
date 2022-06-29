/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace facebook {
namespace terragraph {
namespace mobd {

// TCP request/response protobuf message server.
// Accept one tcp connection at a time.
// Parse/decrypt protobuf requests and send protobuf responses.
void tcpServerStart(); // blocking

} // namespace mobd
} // namespace terragraph
} // namespace facebook
