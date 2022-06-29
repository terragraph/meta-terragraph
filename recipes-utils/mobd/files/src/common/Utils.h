/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef> // for std::size_t
#include <string>

namespace facebook {
namespace terragraph {
namespace mobd {

class Message;

// Serialize a message
// Returns: serialized length or 0 on errors
//   message - message to serialize and encrypt
//   wire    - complete wire-message with encrypted payload
//   plain   - temporary for plain serialization of message
//             ignored when encryption is disabled
//   len     - the length of the 'wire' and 'plain' buffers
int serialize(const Message& message, char* wire, char* plain, std::size_t len);

// Deserialize a payload
// Returns: true on success
//   message - the deserialized message
//   payload - the encrypted message
//   plain   - temporary for decryption
//             ignored when encryption is disabled
//   len     - the length of the 'payload' and 'plain' buffers
bool deserialize(
    Message& message, const char* payload, char* plain, std::size_t len);

void hexdump(const void* ptr, int buflen, int bytes_per_line = 8);
bool disableWatchdog(const Message& m);
bool enableWatchdog(const Message& m);

// TODO: Use facebook::terragraph::SysUtils::system()
// Return: exit status, or -1 on failure. Log all errors.
int system(const std::string& command);

// TODO: Use facebook::terragraph::SysUtils::system()
// Return: true when command returns the expected status. Log all errors.
bool system(const std::string& command, int expectedExitStatus);

} // namespace mobd
} // namespace terragraph
} // namespace facebook
