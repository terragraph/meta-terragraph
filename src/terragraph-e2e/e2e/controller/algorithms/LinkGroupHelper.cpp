/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <deque>

#include "LinkGroupHelper.h"

namespace facebook {
namespace terragraph {

LinkGroupHelper::GroupNameToLinkNames
LinkGroupHelper::getLinkGroups(const TopologyWrapper& topologyW) {
  GroupNameToLinkNames group2Links;
  std::unordered_map<std::string, std::string> link2GroupName;
  std::unordered_set<std::string> visitedLinks;
  auto allLinks = topologyW.getAllLinks();
  std::deque<thrift::Link> linksQueue(allLinks.begin(), allLinks.end());
  while (!linksQueue.empty()) {
    auto link = linksQueue.front();
    linksQueue.pop_front();
    if (link.link_type != thrift::LinkType::WIRELESS ||
        link.a_node_mac.empty() || link.z_node_mac.empty()) {
      continue;
    }

    if (visitedLinks.count(link.name)) {
      continue;
    }
    visitedLinks.insert(link.name);

    std::string groupName;
    auto it = link2GroupName.find(link.name);
    if (it == link2GroupName.end()) {
      // Start a new group
      // We use the first discovered link name as the group name
      groupName = link.name;
      link2GroupName[link.name] = groupName;
    } else {
      groupName = it->second;
    }

    group2Links[groupName].insert(link.name);

    for (const auto& dependentLink : topologyW.getSameRadioLinks(link)) {
      linksQueue.push_front(dependentLink);
      link2GroupName[dependentLink.name] = groupName;
    }
  }

  return group2Links;
}

} // namespace terragraph
} // namespace facebook
