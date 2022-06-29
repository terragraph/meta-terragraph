/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <e2e/common/Consts.h>
#include <fbzmq/async/ZmqEventLoop.h>
#include <fbzmq/service/stats/ThreadData.h>
#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/io/async/EventBase.h>
#include <openr/common/Constants.h>
#include "NetlinkSocket.h"

#include "e2e/common/NodeConfigWrapper.h"

namespace facebook {
namespace terragraph {

const int kSendMonitorIntervalMs = 10000;
const int cpePrefixLength = 64;

constexpr folly::StringPiece RADVD_CONF_PATH = "/var/run/radvd.conf";
constexpr folly::StringPiece DHCP_CONF_PATH = "/var/run/dhcp.conf";

// This will be used with folly::format(). {{ and }} will be converted
// to literal { and }. Other {something} tokens will be substituted.
constexpr folly::StringPiece RADVD_CONF_NONDEF_ROUTE =
    "interface {0} {{\n\
    # Disable Default route\n\
    AdvDefaultLifetime 0;\n\
    AdvSendAdvert on;\n\
    AdvManagedFlag {1};\n\
    MinRtrAdvInterval 3;\n\
    MaxRtrAdvInterval 10;\n\
    prefix {2} {{\n\
            AdvOnLink on;\n\
            AdvAutonomous on;\n\
            AdvRouterAddr on;\n\
    }};\n\
    # RFC 4191\n\
    route {3} {{\n\
            AdvRouteLifetime infinity;\n\
    }};\n\
}};\n";

constexpr folly::StringPiece RADVD_CONF_DEF_ROUTE =
    "interface {0} {{\n\
    # Default Route\n\
    AdvDefaultLifetime 60;\n\
    AdvSendAdvert on;\n\
    AdvManagedFlag {1};\n\
    MinRtrAdvInterval 3;\n\
    MaxRtrAdvInterval 10;\n\
    prefix {2} {{\n\
            AdvOnLink on;\n\
            AdvAutonomous on;\n\
            AdvRouterAddr on;\n\
    }};\n\
}};\n";

constexpr folly::StringPiece DHCPV6_CONF =
    "default-lease-time 2592000;\n\
    preferred-lifetime 604800;\n\
    option dhcp-renewal-time 3600;\n\
    option dhcp-rebinding-time 7200;\n\
    allow leasequery;\n\
    subnet6 {0} {{\n\
             range6 {1}{2} {3}{4};\n\
             option dhcp6.name-servers {5};\n\
             {6}\n\
    }}\n";

// This class isn't thread-safe. Its methods are executed by ZmqEventLoop.
class SquireNlHandler : public openr::fbnl::NetlinkSocket::EventsHandler {
 public:
  SquireNlHandler(
      const std::string& cpeInterface,
      const std::string& wlInterface,
      fbzmq::ZmqEventLoop& zmqLoop,
      const std::string& prefixSourceInterface,
      const std::string& defaultRouteToAdv,
      const std::string& nodeConfigFile)
      : cpeInterface(cpeInterface),
        wlInterface_(wlInterface),
        zmqLoop_(zmqLoop),
        prefixSourceInterface_(prefixSourceInterface),
        defaultRouteToAdv_(defaultRouteToAdv) {
    scheduleRadvdTimeout_ =
        fbzmq::ZmqTimeout::make(&zmqLoop_, [this]() noexcept {
          if (ips_.empty()) {
            LOG(ERROR) << "No IP addresses on " << prefixSourceInterface_
                       << ", stopping radvd";
            launchRadvd(false);
          } else if (ips_.size() == 1) {
            configureAndRunRadvd(*ips_.begin());
          } else {
            LOG(ERROR) << "Too many IP addresses (" << ips_.size() << ") on "
                       << prefixSourceInterface_ << ": "
                       << folly::join(", ", ips_) << ", stopping radvd";
            launchRadvd(false);
          }
        });
    scheduleDhcpTimeout_ =
        fbzmq::ZmqTimeout::make(&zmqLoop_, [this]() noexcept {
          if (ips_.empty()) {
            LOG(ERROR) << "No IP addresses on " << prefixSourceInterface_
                       << ", stopping dhcpd";
            launchDhcp(false);
          } else if (ips_.size() == 1) {
            configureAndRunDhcp(*ips_.begin());
          } else {
            LOG(ERROR) << "Too many IP addresses (" << ips_.size() << ") on "
                       << prefixSourceInterface_ << ": "
                       << folly::join(", ", ips_) << ", stopping dhcp";
            launchDhcp(false);
          }
        });

    // initialize node config
    nodeConfigWrapper_.setNodeConfigFile(nodeConfigFile);
  }

  virtual ~SquireNlHandler() {}

  void linkEventFunc(
      const std::string&,
      const openr::fbnl::Link& linkEntry) noexcept override;

  void neighborEventFunc(
      const std::string&,
      const openr::fbnl::Neighbor& neighborEntry) noexcept override;

  void addrEventFunc(
      const std::string&,
      const openr::fbnl::IfAddress& addrEntry) noexcept override;

  // Schedules scheduleRadvdTimeout_ to fire in 100ms.
  void scheduleLaunchRadvd();

  // Schedules scheduleDhcpTimeout_ to fire in 100ms.
  void scheduleLaunchDhcp();

  // Writes the radvd config file to RADVD_CONF_PATH.
  // Returns true if successful, or false if any error occurred.
  bool writeRadvdConfigFile(const std::string& prefix);

  // If 'start' is true, (re)starts radvd and returns whether this succeeded.
  // If 'start' is false, stops radvd and returns true.
  bool launchRadvd(bool start);

  // Writes the radvd config file and then starts radvd.
  void configureAndRunRadvd(const std::string& prefix);
  void configureAndRunRadvd(const folly::IPAddress& ip);

  // Writes the dhcp config file to DHCP_CONF_PATH.
  // Returns true if successful, or false if any error occurred.
  bool writeDhcpConfigFile(
    const std::string& prefix,
    const std::string& maskPrefix);

  // If 'start' is true, (re)starts dhcp and returns whether this succeeded.
  // If 'start' is false, stops dhcp and returns true.
  bool launchDhcp(bool start);

  // Writes the dhcp config file and then starts dhcp.
  // If dhcp config members have not been initialized, this will be a no-op.
  void configureAndRunDhcp(
    const std::string& prefix,
    const std::string& maskPrefix);
  void configureAndRunDhcp(const folly::IPAddress& ip);

 protected:
  const std::string cpeInterface;

 private:
  const std::string wlInterface_;
  fbzmq::ZmqEventLoop& zmqLoop_;
  const std::string prefixSourceInterface_;
  const std::string defaultRouteToAdv_;
  std::unordered_set<folly::IPAddress> ips_;
  bool dhcpLaunched_{false};
  std::unique_ptr<fbzmq::ZmqTimeout> scheduleRadvdTimeout_{nullptr};
  std::unique_ptr<fbzmq::ZmqTimeout> scheduleDhcpTimeout_{nullptr};
  NodeConfigWrapper nodeConfigWrapper_;
};

class SquireNlThread {
 public:
  SquireNlThread(
      std::shared_ptr<SquireNlHandler> nlh,
      std::unique_ptr<openr::fbnl::NetlinkProtocolSocket> nlProtocolSocket,
      const std::string& cpeInterface,
      const std::string& wlInterface,
      int sigfd,
      fbzmq::ZmqEventLoop& zmqLoop,
      const std::string& prefixSourceInterface,
      const std::string& inputPrefix)
    : nlHandler(std::move(nlh)),
      cpeInterface(cpeInterface),
      wlInterface_(wlInterface),
      sigfd_(sigfd),
      zmqLoop_(zmqLoop),
      prefixSourceInterface_(prefixSourceInterface),
      inputPrefix_(inputPrefix) {

    // create netlink socket to receving netlink events and to
    // call netlink APIs
    netlinkSocket = std::make_shared<openr::fbnl::NetlinkSocket>(
        &zmqLoop_, nlHandler.get(), std::move(nlProtocolSocket));
    netlinkSocket->subscribeEvent(openr::fbnl::LINK_EVENT);
    netlinkSocket->subscribeEvent(openr::fbnl::NEIGH_EVENT);
    netlinkSocket->subscribeEvent(openr::fbnl::ADDR_EVENT);
  }

  virtual ~SquireNlThread() {}

  virtual void preRun();

  void run();

 protected:
  std::shared_ptr<SquireNlHandler> nlHandler;
  const std::string cpeInterface;
  std::shared_ptr<openr::fbnl::NetlinkSocket> netlinkSocket;

 private:
  const std::string wlInterface_;
  const int sigfd_;
  fbzmq::ZmqEventLoop& zmqLoop_;
  const std::string prefixSourceInterface_;
  const std::string inputPrefix_;
};

} // namespace terragraph
} // namespace facebook
