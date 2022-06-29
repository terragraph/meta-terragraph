/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <e2e/if/gen-cpp2/Topology_types.h>

#include "../topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Link group identification.
 *
 * A link group includes all links that must have the same Golay code/channel.
 *
 * The group name is the same as any one of its member links.
 */
class LinkGroupHelper {
  public:
    /** Map from group name to link names. */
    using GroupNameToLinkNames =
      std::unordered_map<std::string, std::unordered_set<std::string>>;

    /** Find all links groups in the topology. */
    static GroupNameToLinkNames getLinkGroups(const TopologyWrapper& topologyW);
};

} // namespace terragraph
} // namespace facebook
