/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * Helper for persisting the software upgrade state (thrift::UpgradeStatus).
 *
 * NOTE: This class is not thread-safe.
 */
class UpgradeStateCache {
 public:
  /**
   * Constructor. This loads the upgrade state from a file on disk.
   * @param versionFile the path to the current software version file
   */
  UpgradeStateCache(const std::string& versionFile);

  /** Return the cached upgrade state. */
  const thrift::UpgradeStatus& getUpgradeStatus() const;

  /** Persist the given upgrade status. */
  void save(const thrift::UpgradeStatus& upgradeStatus);

  /**
   * Invalidate any existing upgrade state cache, and optionally reset the
   * current upgrade status.
   */
  void invalidate(bool resetStatus = true);

 private:
  /** The current upgrade status. */
  thrift::UpgradeStatus upgradeStatus_;

  /** The path to the current software version file. */
  const std::string& versionFile_;
};

inline const thrift::UpgradeStatus&
UpgradeStateCache::getUpgradeStatus() const {
  return upgradeStatus_;
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
