/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ControlSuperframeHelper.h"

#include <folly/Format.h>
#include <folly/String.h>

using std::invalid_argument;

namespace {
  // Valid control superframe config range
  const int kMinControlSuperframe{0};
  const int kMaxControlSuperframe{15};
  // Control superframe value equivalent to unset
  const int kControlSuperframeUnset{255};
} // namespace

namespace facebook {
namespace terragraph {

bool
ControlSuperframeHelper::isValidLinkControlSuperframe(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const thrift::Link& link,
    std::optional<int64_t> aSframe,
    std::optional<int64_t> zSframe) noexcept {
  auto aNode = topologyW.getNode(link.a_node_name);
  auto zNode = topologyW.getNode(link.z_node_name);
  if (!aNode || !zNode) {
    return false;
  }

  if (link.link_type != thrift::LinkType::WIRELESS || link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return true;
  }

  // Control Superframe should match from both ends of a link
  if (!aSframe || !zSframe || aSframe.value() != zSframe.value()) {
    return false;
  }

  // If any end of the link is a CN, value must be kControlSuperframeUnset
  if (aNode->node_type == thrift::NodeType::CN ||
      zNode->node_type == thrift::NodeType::CN) {
    if (aSframe.value() == kControlSuperframeUnset) {
      return true;
    }
    return false;
  }

  // DN-DN links should not have ControlSuperframe unset
  if (aSframe.value() == kControlSuperframeUnset) {
    return false;
  }

  // Check if other links from same radio have same ControlSuperframe
  auto aMacLinks = topologyW.getLinksByRadioMac(link.a_node_mac);
  for (const auto& radioLink : aMacLinks) {
    if (radioLink.name == link.name || radioLink.a_node_mac.empty() ||
        radioLink.z_node_mac.empty()) {
      continue;
    }

    auto responderMac = radioLink.a_node_mac == link.a_node_mac
                            ? radioLink.z_node_mac
                            : radioLink.a_node_mac;
    auto otherSframe = configHelper.getLinkControlSuperframe(
          link.a_node_name, responderMac, false);

    if (otherSframe && otherSframe.value() == aSframe.value()) {
      return false;
    }
  }

  auto zMacLinks = topologyW.getLinksByRadioMac(link.z_node_mac);
  for (const auto& radioLink : zMacLinks) {
    if (radioLink.name == link.name) {
      continue;
    }

    auto responderMac = radioLink.z_node_mac == link.z_node_mac
                            ? radioLink.a_node_mac
                            : radioLink.z_node_mac;
    auto otherSframe = configHelper.getLinkControlSuperframe(
          link.z_node_name, responderMac, false);
    if (otherSframe && otherSframe == aSframe) {
      return false;
    }
  }

  return true;
}

bool
ControlSuperframeHelper::assignLinkControlSuperframe(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    const thrift::Link& link) noexcept {
  if (link.link_type != thrift::LinkType::WIRELESS || link.a_node_mac.empty() ||
      link.z_node_mac.empty()) {
    return true;
  }

  // Check if existing assignment is valid
  auto aSframe = configHelper.getLinkControlSuperframe(
      link.a_node_name, link.z_node_mac, false);
  auto zSframe = configHelper.getLinkControlSuperframe(
      link.z_node_name, link.a_node_mac, false);
  if (isValidLinkControlSuperframe(
          topologyW, configHelper, link, aSframe, zSframe)) {
    return true;
  }

  // Check if any side of the link is user configured
  auto aUserSframe = configHelper.getLinkControlSuperframe(
      link.a_node_name, link.z_node_mac, true);
  auto zUserSframe = configHelper.getLinkControlSuperframe(
      link.z_node_name, link.a_node_mac, true);
  if (aUserSframe && zUserSframe) {
    return false;
  } else if (aUserSframe) {
    if (isValidLinkControlSuperframe(
            topologyW, configHelper, link, aUserSframe, aUserSframe)) {
      std::string errorMsg;
      return configHelper.setLinkControlSuperframe(
          link, aUserSframe, false, errorMsg);
    }
    return false;
  } else if (zUserSframe) {
    if (isValidLinkControlSuperframe(
            topologyW, configHelper, link, zUserSframe, zUserSframe)) {
      std::string errorMsg;
      return configHelper.setLinkControlSuperframe(
          link, zUserSframe, false, errorMsg);
    }
    return false;
  }

  auto aNode = topologyW.getNode(link.a_node_name);
  auto zNode = topologyW.getNode(link.z_node_name);
  if (!aNode || !zNode) {
    return false;
  }

  // If any end of the link is a CN, value must be kControlSuperframeUnset
  if (aNode->node_type == thrift::NodeType::CN ||
      zNode->node_type == thrift::NodeType::CN) {
    if (isValidLinkControlSuperframe(
            topologyW,
            configHelper,
            link,
            kControlSuperframeUnset,
            kControlSuperframeUnset)) {
      std::string errorMsg;
      return configHelper.setLinkControlSuperframe(
          link, kControlSuperframeUnset, false, errorMsg);
    }
    return false;
  }

  // Try all possible values
  std::vector<int64_t> possibleValues = {0, 1};
  for (auto sFrame : possibleValues) {
    if (isValidLinkControlSuperframe(
            topologyW, configHelper, link, sFrame, sFrame)) {
      std::string errorMsg;
      return configHelper.setLinkControlSuperframe(
          link, sFrame, false, errorMsg);
    }
  }

  return false;
}

int64_t
ControlSuperframeHelper::getPreferredControlSuperframe(
    const TopologyWrapper& topologyW,
    const thrift::Link& link,
    const std::unordered_map<std::string, int64_t>& userConfigured,
    const std::unordered_map<std::string, int64_t>& autoConfigured,
    const std::unordered_map<std::string, std::unordered_set<std::string>>&
        mac2LinkNames,
    const std::unordered_map<std::string, int64_t>& assignedMap) {
  auto aNode = topologyW.getNode(link.a_node_name);
  auto zNode = topologyW.getNode(link.z_node_name);
  if (!aNode || !zNode || aNode->node_type == thrift::NodeType::CN ||
      zNode->node_type == thrift::NodeType::CN) {
    return kControlSuperframeUnset;
  }

  // Check if other links from same radio have ControlSuperframe already
  // assigned
  std::unordered_set<int64_t> usedValues;
  auto aLinkNamesIter = mac2LinkNames.find(link.a_node_mac);
  auto zLinkNamesIter = mac2LinkNames.find(link.z_node_mac);
  if (aLinkNamesIter != mac2LinkNames.end()) {
    for (const auto& linkName : aLinkNamesIter->second) {
      if (linkName != link.name) {
        auto iter = assignedMap.find(linkName);
        if (iter != assignedMap.end() &&
            iter->second != kControlSuperframeUnset) {
          usedValues.insert(iter->second);
        }
      }
    }
  }
  if (zLinkNamesIter != mac2LinkNames.end()) {
    for (const auto& linkName : zLinkNamesIter->second) {
      if (linkName != link.name) {
        auto iter = assignedMap.find(linkName);
        if (iter != assignedMap.end() &&
            iter->second != kControlSuperframeUnset) {
          usedValues.insert(iter->second);
        }
      }
    }
  }

  if (!usedValues.empty()) {
    if (usedValues.size() > 1) {
      throw invalid_argument(
        folly::sformat(
            "Impossible to allocate control superframe for `{}`.", link.name));
    }
    return *usedValues.begin() == 0 ? 1 : 0;
  }

  // Pick user configured value if present
  auto iter = userConfigured.find(link.name);
  if (iter != userConfigured.end() && iter->second != kControlSuperframeUnset) {
    return iter->second;
  }

  // Pick auto configured value if present
  iter = autoConfigured.find(link.name);
  if (iter != autoConfigured.end() && iter->second != kControlSuperframeUnset) {
    return iter->second;
  }

  return 0;
}

void
ControlSuperframeHelper::assignAllControlSuperframes(
    const TopologyWrapper& topologyW,
    ConfigHelper& configHelper,
    bool clearUserConfig) {

  std::vector<thrift::Link> userConfiguredLinks;
  std::vector<thrift::Link> autoConfiguredLinks;
  std::vector<thrift::Link> notConfiguredLinks;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      mac2LinkNames;
  std::unordered_map<std::string, int64_t> userConfigured;
  std::unordered_map<std::string, int64_t> autoConfigured;
  for (auto& link : topologyW.getAllLinks()) {
    if (link.link_type != thrift::LinkType::WIRELESS ||
        link.a_node_mac.empty() || link.z_node_mac.empty()) {
      continue;
    }

    mac2LinkNames[link.a_node_mac].insert(link.name);
    mac2LinkNames[link.z_node_mac].insert(link.name);

    std::optional<int64_t> aSframe = std::nullopt;
    std::optional<int64_t> zSframe = std::nullopt;
    if (!clearUserConfig) {
      aSframe = configHelper.getLinkControlSuperframe(
          link.a_node_name, link.z_node_mac, true);
      zSframe = configHelper.getLinkControlSuperframe(
          link.z_node_name, link.a_node_mac, true);
      if (aSframe || zSframe) {
        userConfigured[link.name] = aSframe ? aSframe.value() : zSframe.value();
        userConfiguredLinks.push_back(link);
        continue;
      }
    }

    aSframe = configHelper.getLinkControlSuperframe(
        link.a_node_name, link.z_node_mac, false);
    zSframe = configHelper.getLinkControlSuperframe(
        link.z_node_name, link.a_node_mac, false);
    if (aSframe && zSframe && aSframe.value() == zSframe.value() &&
        aSframe.value() != kControlSuperframeUnset) {
      autoConfigured[link.name] = aSframe.value();
      autoConfiguredLinks.push_back(link);
      continue;
    }

    notConfiguredLinks.push_back(link);
  }

  // 1- Start with user configured links to maintain user config
  // 2- Followed by existing auto config to least disrupt the network
  // 3- Finally we allocate links with no ControlSuperframe allocation
  // Immediately process dependent links (p2mp links from the same radios) to
  // propagate allocation safely
  std::deque<thrift::Link> linksQueue(
      userConfiguredLinks.begin(), userConfiguredLinks.end());
  linksQueue.insert(
      linksQueue.end(), autoConfiguredLinks.begin(), autoConfiguredLinks.end());
  linksQueue.insert(
      linksQueue.end(), notConfiguredLinks.begin(), notConfiguredLinks.end());

  std::unordered_map<std::string, int64_t> assignedMap;
  while(!linksQueue.empty()) {
    auto link = linksQueue.front();
    linksQueue.pop_front();
    if (assignedMap.count(link.name)) {
      continue;
    }

    auto sFrame = getPreferredControlSuperframe(
        topologyW,
        link,
        userConfigured,
        autoConfigured,
        mac2LinkNames,
        assignedMap);

    // Check if we have to change a user configured value
    if (!clearUserConfig) {
      auto userConfigIter = userConfigured.find(link.name);
      if (userConfigIter != userConfigured.end() &&
          userConfigIter->second != sFrame) {
        // New ControlSuperframe does not matche existing user config.
        throw invalid_argument(
            "Unable to respect user configured control superframe for: " +
            link.name);
      }
    }

    assignedMap[link.name] = sFrame;
    auto dependentLinks = topologyW.getSameRadioLinks(link);
    linksQueue.insert(
        linksQueue.begin(), dependentLinks.begin(), dependentLinks.end());
  }


  if (clearUserConfig) {
    // Clear user-configured control super frames
    for (auto& link : topologyW.getAllLinks()) {
      std::string errMsg;
      configHelper.setLinkControlSuperframe(
          link, std::nullopt, true, errMsg);
    }
  }

  // Write to config
  std::string errorMsg;
  for (const auto& link : topologyW.getAllLinks()) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      continue;
    }

    auto assignedIter = assignedMap.find(link.name);
    if (assignedIter != assignedMap.end()) {
      configHelper.setLinkControlSuperframe(
          link, assignedIter->second, false, errorMsg);
      VLOG(2) << folly::sformat(
          "Assigned control superframe for `{}` to {}",
          link.name,
          assignedIter->second);
    } else {
      configHelper.setLinkControlSuperframe(link, std::nullopt, false,
          errorMsg);
      VLOG(2)
          << folly::sformat("Cleared control superframe for `{}`", link.name);
    }
  }
}

} // namespace terragraph
} // namespace facebook
