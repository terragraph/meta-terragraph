/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Parser.h"
#include "Utils.h"
#include <boost/crc.hpp>

namespace facebook {
namespace terragraph {
namespace mobd {

Parser::Parser(std::size_t max_payload_size)
    : max_payload_size_(max_payload_size),
      payload_(new char[max_payload_size]),
      plain_(new char[max_payload_size]) {}

const Message*
Parser::parse(char c) {

  const Message* ret = nullptr; // no Message yet

  switch (state_) {
    case START1:
      if (c == 'T') {
        state_ = START2;
      }
      break;

    case START2:
      if (c == 'G') {
        state_ = LENGTH1;
      } else {
        state_ = START1;
      }
      break;

    case LENGTH1:
      length_ = (c << 8) & 0xff00;
      state_ = LENGTH2;
      break;

    case LENGTH2:
      length_ |= (c & 0xff);
      if (length_ > 0 && length_ <= max_payload_size_) {
        state_ = PAYLOAD;
        idx_ = 0;
      } else {
        state_ = START1;
      }
      break;

    case PAYLOAD:
      payload_[idx_++] = c;
      if (idx_ == length_) {
        state_ = CRC1;
      }
      break;

    case CRC1:
      crc16_ = (c << 8) & 0xff00;
      state_ = CRC2;
      break;

    case CRC2:
      crc16_ |= (c & 0xff);
      boost::crc_16_type computed_crc16;
      computed_crc16.process_bytes(payload_.get(), length_);
      if (computed_crc16.checksum() == crc16_ &&
          deserialize(message_, payload_.get(), plain_.get(), length_)) {
        ret = &message_;
      }
      state_ = START1;
      break;
  }
  return ret;
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
