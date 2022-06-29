/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>

namespace facebook {
namespace terragraph {

/**
 * MD5 hash-related utilities.
 */
class Md5Utils {
 public:
  /** Compute the MD5 hash of the given string. */
  static std::string computeMd5(const std::string& str);

  /**
   * Compute the MD5 hash of the given file, optionally skipping a given number
   * of bytes at the start of the file.
   */
  static std::string computeFileMd5(
      const std::string& path, size_t skipHeaderSize = 0);

 private:
  /** Returns the input byte array as a hex string. */
  static std::string bytesToHex(const unsigned char bytes[], size_t size);
};

} // namespace terragraph
} // namespace facebook
