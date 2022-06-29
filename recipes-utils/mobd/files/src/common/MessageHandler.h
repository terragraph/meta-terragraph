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

class Message;

// Returns: the response to m, or nullptr if request is not authenticated.
const Message* messageHandler(const Message& m);
void resetAuthentication();

} // namespace mobd
} // namespace terragraph
} // namespace facebook
