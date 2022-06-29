/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {

/**
 * Compression-related utilities.
 */
class CompressionUtil {
 public:
  /** Compress a message using the given compression format. */
  static void compress(
      thrift::Message& message,
      thrift::CompressionFormat compressionFormat =
          thrift::CompressionFormat::SNAPPY);

  /**
   * Decompress a message.
   *
   * Upon failure, returns false and sets 'error' to the failure reason.
   */
  static bool decompress(thrift::Message& message, std::string& error);
};

} // namespace terragraph
} // namespace facebook
