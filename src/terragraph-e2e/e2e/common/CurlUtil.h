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
 * Wrapper class around libcurl.
 */
class CurlUtil {
 public:
  /**
   * Download a file from the given URL and save it to the local path specified.
   *
   * Upon failure, returns false and writes the reason to 'error'.
   */
  static bool download(
      const std::string& url, const std::string& savePath, std::string& error);

  /**
   * Upload a file from the given local path to the given URL.
   *
   * Upon failure, returns false and writes the reason to 'error'.
   */
   static bool upload(
       const std::string& url, const std::string& path, std::string& error);
};

} // namespace terragraph
} // namespace facebook
