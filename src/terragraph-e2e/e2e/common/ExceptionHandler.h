/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace facebook {
namespace terragraph {

/**
 * Global uncaught exception handler.
 */
class ExceptionHandler {
 public:
  /**
   * Installs a global terminate handler that logs detailed error information
   * before aborting.
   */
  static void install();

 private:
  /** Logs the current stack trace. */
  static inline void printStackTrace();
};

} // namespace terragraph
} // namespace facebook
