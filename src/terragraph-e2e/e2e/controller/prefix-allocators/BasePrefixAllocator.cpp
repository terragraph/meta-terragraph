/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "BasePrefixAllocator.h"

namespace facebook {
namespace terragraph {
BasePrefixAllocator::BasePrefixAllocator(
    const thrift::PrefixAllocParams& prefixAllocParams,
    TopologyWrapper *topologyW)
    : topologyW_(topologyW) {
  populatePrefixAllocParams(prefixAllocParams);
}

void
BasePrefixAllocator::populatePrefixAllocParams(
    const thrift::PrefixAllocParams& prefixAllocParams) {
  // Ensure prefix allocation parameters are set
  if (prefixAllocParams == thrift::PrefixAllocParams()) {
    throw std::invalid_argument(folly::sformat(
        "Prefix allocation parameters not set: "
        "Seed prefix: {}, Allocation Prefix Length: {}",
        prefixAllocParams.seedPrefix,
        prefixAllocParams.allocPrefixLen));
  }
  // Validate prefix allocation params
  validatePrefixAllocParams(prefixAllocParams);

  // Populate/format prefix allocation parameters
  if (prefixAllocParams != thrift::PrefixAllocParams()) {
    auto seedPrefix = folly::IPAddress::createNetwork(
        prefixAllocParams.seedPrefix);
    prefixAllocParams_ = std::make_pair(
        seedPrefix, prefixAllocParams.allocPrefixLen);
  } else {
    LOG(ERROR) << "PrefixAllocParams not set in controller config...";
  }
}

void
BasePrefixAllocator::validatePrefixAllocParams(
    const thrift::PrefixAllocParams& prefixAllocParams) const {
  auto seedPrefix = prefixAllocParams.seedPrefix;
  auto allocPrefixLen = prefixAllocParams.allocPrefixLen;

  folly::CIDRNetwork prefix;
  try {
    prefix = folly::IPAddress::createNetwork(seedPrefix);
  } catch (std::exception const& err) {
    throw std::invalid_argument(folly::sformat(
        "Invalid seed prefix {} for centralized prefix allocation: {}",
        seedPrefix,
        folly::exceptionStr(err)));
  }

  // Make sure allocPrefixLen is non-negative so we can cast to size_t
  if (allocPrefixLen < 0) {
    throw std::invalid_argument(folly::sformat(
        "Invalid allocation prefix length `{}` for centralized prefix "
        "allocation: allocPrefixLen is less than 0", allocPrefixLen));
  }

  std::size_t unsignedAllocPrefixLen = allocPrefixLen;

  // Usually don't want to allocate more than /64 per node, but its not a fatal
  // error
  if (unsignedAllocPrefixLen > 64) {
    LOG(WARNING) << "allocPrefixLen is greater than 64";
  }

  if (unsignedAllocPrefixLen <= prefix.second ||
      unsignedAllocPrefixLen > folly::IPAddressV6::bitCount()) {
    throw std::invalid_argument(folly::sformat(
        "invalid allocation prefix length `{}` for centralized prefix "
        "allocation: allocPrefixLen is not greater than the seed_prefix "
        "netmask `{}`, or it is greater than 128.",
        allocPrefixLen,
        prefix.second));
  }
}

} // namespace terragraph
} // namespace facebook
