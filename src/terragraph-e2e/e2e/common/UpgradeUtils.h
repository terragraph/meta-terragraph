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
 * Utilities for Terragraph software upgrades.
 */
class UpgradeUtils {
 public:
  /**
   * Compute the MD5 hash of the given upgrade image file (excluding the header
   * section).
   *
   * Throws std::runtime_error upon encountering an error.
   */
  static std::string getImageMd5(const std::string& path);

  /**
   * Check that the given upgrade image file has a given MD5 hash (excluding the
   * header section).
   *
   * Throws std::runtime_error upon encountering an error.
   */
  static void verifyImage(
      const std::string& path, const std::string& expectedMd5);
};

} // namespace terragraph
} // namespace facebook
