/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TerragraphNode.h"

#include <gflags/gflags.h>

#include <algorithm>
#include <chrono>
#include <thread>
#include <utility>
#include <vector>

// Limiting the number of links enables the emulation of assoc failures.
DEFINE_int32(max_links, 2, "number of different links that can be associated");
DEFINE_int32(ignite_delay_s, 0, "response delay for assoc/dissoc");

namespace {

class Link {
 public:
  Link(
      const std::string& initiatorMac,
      const std::string& responderMac,
      bool ignited)
      : initiatorMac_(initiatorMac),
        responderMac_(responderMac),
        ignited_(ignited) {}
  const bool
  hasInitMac(const std::string& initiatorMac) {
    return initiatorMac == initiatorMac_;
  }
  const bool
  hasRespMac(const std::string& responderMac) {
    return responderMac == responderMac_;
  }
  void
  setIgnited(bool ignited) {
    ignited_ = ignited;
  }
  bool
  isIgnited() {
    return ignited_;
  };

 private:
  const std::string initiatorMac_;
  const std::string responderMac_;
  bool ignited_;
};

// The links that have been associated in the past.
std::vector<Link> links;

Link*
findLink(const std::string& initiatorMac, const std::string& responderMac) {
  for (auto& link : links) {
    if (link.hasInitMac(initiatorMac) && link.hasRespMac(responderMac))
      return &link;
  }
  return nullptr;
}

bool
igniteLink(
    const std::string& initiatorMac,
    const std::string& responderMac,
    bool ignite) {
  Link* link = findLink(initiatorMac, responderMac);
  if (link != nullptr) {
    link->setIgnited(ignite);
    return true; // Successfully set ignited state of previously associated
                 // link.
  }
  if (!ignite) {
    return true; // ok to dissociate link that was never ignited before
  }
  if (links.size() >= FLAGS_max_links) {
    return false; // Too many links
  } else {
    links.push_back(Link(initiatorMac, responderMac, ignite));
    return true; // Associated a new link
  }
}

} // namespace

TerragraphNode::TerragraphNode()
    : osVersion("RELEASE M56"),
      secondaryOsVersion("RELEASE M100"),
      dtbVersion("DTB 876"),
      ubootVersion("u-boot 7.11"),
      nic0Mac("00:01:02:03:04:00"),
      radioMac{
          "00:01:02:03:04:01",
          "00:01:02:03:04:02",
          "00:01:02:03:04:03",
          "00:01:02:03:04:04"} {}

bool
TerragraphNode::reboot() const {
  return true;
}
bool
TerragraphNode::swapPrimaryAndSecondaryPartitions() const {
  return true;
}

bool
TerragraphNode::assoc(
    const std::string& initiatorMac, const std::string& responderMac) {
  std::this_thread::sleep_for(std::chrono::seconds(FLAGS_ignite_delay_s));
  return igniteLink(initiatorMac, responderMac, true);
}

bool
TerragraphNode::dissoc(
    const std::string& initiatorMac, const std::string& responderMac) {
  std::this_thread::sleep_for(std::chrono::seconds(FLAGS_ignite_delay_s));
  return igniteLink(initiatorMac, responderMac, false);
}

class Sample {
 public:
  Sample(int minv, int maxv, int val)
      : minValue(minv), maxValue(maxv), value(val) {}
  int
  getValue() {
    int v = value++;
    if (value > maxValue) {
      value = minValue;
    }
    return v;
  }

 private:
  const int minValue;
  const int maxValue;
  int value;
};

void
TerragraphNode::getLinkStats(
    int timeoutSec,
    const std::string& initiatorMac,
    const std::string& responderMac,
    std::vector<int>& snr,
    std::vector<int>& rssi) {
  static Sample snrSample(0, 100, 0);
  static Sample rssiSample(-50, 50, -50);
  snr.clear();
  rssi.clear();
  Link* link = findLink(initiatorMac, responderMac);
  if (link != nullptr && link->isIgnited()) {
    timeoutSec = std::max(timeoutSec, minLinkStatsTimeoutSec);
    timeoutSec = std::min(timeoutSec, maxLinkStatsTimeoutSec);
    std::this_thread::sleep_for(std::chrono::seconds(timeoutSec));
    for (int i = 0; i < timeoutSec; i++) {
      snr.push_back(snrSample.getValue());
      rssi.push_back(rssiSample.getValue());
    }
  }
}
