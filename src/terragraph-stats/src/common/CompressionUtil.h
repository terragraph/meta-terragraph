/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include "stats/if/gen-cpp2/Aggregator_types.h"

namespace facebook {
namespace terragraph {
namespace stats {

class CompressionUtil {
 public:
  // Compress the message using the given format.
  static void compress(
      thrift::AggrMessage& message,
      thrift::AggrCompressionFormat compressionFormat =
          thrift::AggrCompressionFormat::SNAPPY);

  // Decompress the message.
  // Upon failure, returns false and sets 'error' to the failure reason.
  static bool decompress(thrift::AggrMessage& message, std::string& error);
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
