/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>

namespace facebook {
namespace terragraph {

/**
 * UUID (universally unique identifier)-related utilities.
 */
class UuidUtils {
 public:
  /**
   * Generate a UUID string using boost::uuids.
   *
   * NOTE: This method is NOT thread-safe and will return duplicates!
   */
  static std::string genUuid();

 private:
  /** The random number generator instance. */
  static boost::uuids::random_generator gen;
};

} // namespace terragraph
} // namespace facebook
