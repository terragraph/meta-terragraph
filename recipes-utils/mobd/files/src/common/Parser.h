/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 TG Bluetooth message wire format.

  1. <start-of-message>  2 bytes  'T' 'G'
  2. <payload length>    2 bytes  uint16 network (big endian) byte order
  3. <payload>           n bytes  google proto buf (Message)
  4. <payload crc-16>    2-bytes  uint16 network (big endian) byte order
*/

#pragma once

#include "Message.pb.h"
#include <cstddef> // for std::size_t
#include <stdint.h>

namespace facebook {
namespace terragraph {
namespace mobd {

class Parser {
 public:
  explicit Parser(std::size_t max_payload_size);
  const Message* parse(char c);
  void reset();

 private:
  enum State { START1, START2, LENGTH1, LENGTH2, PAYLOAD, CRC1, CRC2 };

  const std::size_t max_payload_size_;
  State state_{START1};
  uint16_t length_;
  uint16_t crc16_;
  std::size_t idx_;
  std::unique_ptr<char[]> payload_; // encrypted payload
  std::unique_ptr<char[]> plain_; // temporary for decrypted payload
  Message message_;
};

inline void
Parser::reset() {
  state_ = START1;
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
