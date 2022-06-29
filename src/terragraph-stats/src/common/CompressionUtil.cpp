/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CompressionUtil.h"

#include <folly/compression/Compression.h>

namespace facebook {
namespace terragraph {
namespace stats {

void
CompressionUtil::compress(
    thrift::AggrMessage& message,
    thrift::AggrCompressionFormat compressionFormat) {
  folly::io::CodecType codecType;
  switch (compressionFormat) {
    case thrift::AggrCompressionFormat::SNAPPY:
      codecType = folly::io::CodecType::SNAPPY;
      break;
  }

  message.value = folly::io::getCodec(codecType)->compress(message.value);
  message.compressed_ref() = true;
  message.compressionFormat_ref() = compressionFormat;
}

bool
CompressionUtil::decompress(thrift::AggrMessage& message, std::string& error) {
  if (!message.compressed_ref() || !message.compressed_ref().value()) {
    return true;  // not compressed
  }

  if (!message.compressionFormat_ref()) {
    error = "Error decompressing message: No compression format specified.";
    return false;
  }

  switch (message.compressionFormat_ref().value()) {
    case thrift::AggrCompressionFormat::SNAPPY:
      message.value = folly::io::getCodec(
        folly::io::CodecType::SNAPPY)->uncompress(message.value);
      break;
    default:
      error =
          "Error decompressing message: Unknown compression format '" +
          std::to_string(static_cast<int>(message.compressionFormat_ref()
              .value())) +
          "'.";
      return false;
  }
  message.compressed_ref() = false;

  return true;
}

} // namespace stats
} // namespace terragraph
} // namespace facebook
