/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SquireHandler.h"

#include <folly/FileUtil.h>
#include <folly/json.h>
#include <folly/ScopeGuard.h>
#include <thrift/lib/cpp2/async/HeaderClientChannel.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "e2e/common/IpUtil.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

using apache::thrift::CompactSerializer;
using fbzmq::COUNT;

namespace facebook {
namespace terragraph {

// This class isn't thread-safe. Its methods are executed by ZmqEventLoop.
void
SquireNlHandler::linkEventFunc(
    const std::string&,
    const openr::fbnl::Link&) noexcept {
  return;
}

void
SquireNlHandler::neighborEventFunc(
    const std::string& ifName,
    const openr::fbnl::Neighbor& nbr) noexcept {
  const auto& ip = nbr.getDestination();

  if (ifName != wlInterface_ ||
      ! ip.isV6() ||
      ip.asV6().isMulticast()) {
    return;
  }

  try {
    std::string cmd;

    if (nbr.isReachable()) {
      cmd = "ip -6 r add ::/0 via " + ip.str() + " dev " + ifName;
    } else {
      cmd = "ip -6 r del ::/0 via " + ip.str() + " dev " + ifName;
    }
    int err = system(cmd.c_str());
    if (err == 0) {
      return;
    }
    auto errToStr = [](int err) {
      if (WIFEXITED(err)) {
        return folly::to<std::string>("Exit code ", WEXITSTATUS(err));
      } else if (WIFSIGNALED(err)) {
        return folly::to<std::string>("Killed by signal ", WTERMSIG(err));
      } else {
        return folly::to<std::string>("Unknown reason ", err);
      }
    };
    throw std::runtime_error(errToStr(err));
  } catch (std::exception const& e) {
      LOG(WARNING) << " Error in processing nbr event"
                 << " with ip: " << ip
                 << " and interface: " << ifName
                 << " with reachability: " << nbr.isReachable()
                 << " with exception :"
                 << folly::exceptionStr(e);
  }
}

void
SquireNlHandler::addrEventFunc(
    const std::string& ifName,
    const openr::fbnl::IfAddress& addrEntry) noexcept {
  auto network = addrEntry.getPrefix();
  if (!network.has_value()) {
    LOG(WARNING) << "Received netlink address event without address: "<< ifName;
    return;
  }
  const folly::IPAddress& ip = network.value().first;
  if (ifName != prefixSourceInterface_ || !ip.isV6() ||
      ip.asV6().isLoopback() ||
      (!ip.asV6().isPrivate() && !ip.asV6().isRoutable())) {
    return;
  }

  VLOG(3) << "IP " << ip.str() << " "
          << (addrEntry.isValid() ? "added" : "removed") << " on interface "
          << prefixSourceInterface_;
  if (addrEntry.isValid()) {
    ips_.insert(ip);
  } else {
    ips_.erase(ip);
  }

  scheduleLaunchDhcp();
  scheduleLaunchRadvd();
}

void
SquireNlHandler::scheduleLaunchRadvd() {
  // Let the list of IPs stabilize if we have multiple events
  if (scheduleRadvdTimeout_->isScheduled()) {
    scheduleRadvdTimeout_->cancelTimeout();
  }
  scheduleRadvdTimeout_->scheduleTimeout(std::chrono::milliseconds(100), false);
}

void
SquireNlHandler::scheduleLaunchDhcp() {
  // Let the list of IPs stabilize if we have multiple events
  if (scheduleDhcpTimeout_->isScheduled()) {
    scheduleDhcpTimeout_->cancelTimeout();
  }
  scheduleDhcpTimeout_->scheduleTimeout(std::chrono::milliseconds(100), false);
}

bool
SquireNlHandler::writeRadvdConfigFile(const std::string& prefix) {
  int fd = open(RADVD_CONF_PATH.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    PLOG(ERROR) << "Can't create " << RADVD_CONF_PATH;
    return false;
  }
  SCOPE_EXIT { close(fd); };

  std::vector<std::string> args = {
      cpeInterface, // nic
      dhcpLaunched_ ? "on" : "off", // Managed flag
      prefix
  };

  std::string radvd_template;
  if (defaultRouteToAdv_.empty()) {
    radvd_template = RADVD_CONF_DEF_ROUTE;
  } else {
    radvd_template = RADVD_CONF_NONDEF_ROUTE;
    args.push_back(defaultRouteToAdv_); // route
  }
  std::string data =
      fmt::format(folly::cEscape<std::string>(radvd_template),
          fmt::join(args, ", "));
  if (folly::writeFull(fd, data.c_str(), data.size()) < 0) {
    PLOG(ERROR) << "Can't write radvd.conf";
    return false;
  }
  return true;
}

bool
SquireNlHandler::launchRadvd(bool start) {
  int err;

  if (!start) {
    // shutdown
    err = system("sv d radvd");
  } else {
    // start (or restart)
    err = system("sv -w 1 restart radvd");
  }
  // Don't check error code in shutdown, there's nothing than can be done anyway
  if (err == 0 || !start) {
    return true;
  }
  auto errToStr = [](int err) {
    if (WIFEXITED(err)) {
      return folly::to<std::string>("Exit code ", WEXITSTATUS(err));
    } else if (WIFSIGNALED(err)) {
      return folly::to<std::string>("Killed by signal ", WTERMSIG(err));
    } else {
      return folly::to<std::string>("Unknown reason ", err);
    }
  };
  PLOG(ERROR) << "Can't start radvd, " << errToStr(err);
  return false;
}

void
SquireNlHandler::configureAndRunRadvd(const std::string& prefix) {
  LOG(INFO) << "Writing radvd.conf, prefix=" << prefix;
  if (!writeRadvdConfigFile(prefix)) {
    LOG(FATAL) << "writeRadvdConfigFile failed";
  }
  LOG(INFO) << "Running radvd";
  if (!launchRadvd(true)) {
    LOG(FATAL) << "Couldn't run radvd";
  }
}

void
SquireNlHandler::configureAndRunRadvd(const folly::IPAddress& ip) {
  std::string prefix =
    folly::to<std::string>(
        ip.mask(cpePrefixLength).str(),
        "/",
        cpePrefixLength);
  configureAndRunRadvd(prefix);
}

bool
SquireNlHandler::writeDhcpConfigFile(
    const std::string& prefix,
    const std::string& maskPrefix) {
  int fd = open(DHCP_CONF_PATH.data(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
  if (fd == -1) {
    PLOG(ERROR) << "Can't create " << DHCP_CONF_PATH;
    return false;
  }
  SCOPE_EXIT { close(fd); };

  auto dhcpParams = nodeConfigWrapper_.getDhcpParams();
  std::vector<std::string> args = {
      prefix, // prefix
      maskPrefix, // masked_prefix
      IpUtil::ipv6Host2String(dhcpParams->dhcpRangeMin), // dhcp_range_min
      maskPrefix, // masked_prefix
      IpUtil::ipv6Host2String(dhcpParams->dhcpRangeMax), // dhcp_range_max
      dhcpParams->dhcpNameServer, // dhcp_name_server
      dhcpParams->dhcpGlobalConfigAppend_ref().has_value() ?
          dhcpParams->dhcpGlobalConfigAppend_ref().value() :
          "" // dhcp_global_config_append
      };

  std::string data =
      fmt::format(folly::cEscape<std::string>(DHCPV6_CONF),
          fmt::join(args, ", "));
  if (folly::writeFull(fd, data.c_str(), data.size()) < 0) {
    PLOG(ERROR) << "Can't write dhcp.conf";
    return false;
  }
  return true;
}

bool
SquireNlHandler::launchDhcp(bool start) {
  int err;

  if (!start) {
    // shutdown
    err = system("sv d dhcp");
  } else {
    // start (or restart)
    err = system("sv -w 1 restart dhcp");
  }
  // Don't check error code in shutdown, there's nothing than can be done anyway
  if (err == 0 || !start) {
    return true;
  }
  auto errToStr = [](int err) {
    if (WIFEXITED(err)) {
      return folly::to<std::string>("Exit code ", WEXITSTATUS(err));
    } else if (WIFSIGNALED(err)) {
      return folly::to<std::string>("Killed by signal ", WTERMSIG(err));
    } else {
      return folly::to<std::string>("Unknown reason ", err);
    }
  };
  PLOG(ERROR) << "Can't start dhcp, " << errToStr(err);
  return false;
}

void
SquireNlHandler::configureAndRunDhcp(
    const std::string& prefix,
    const std::string& maskPrefix) {
  if (nodeConfigWrapper_.getDhcpParams()->keaEnabled) {
    LOG(ERROR) << "Dhcp is handled by kea, skipping....";
    return;
  }

  if (nodeConfigWrapper_.getDhcpParams()->dhcpNameServer.empty()) {
    LOG(ERROR) << "Dhcp name server is not configured, skipping....";
    return;
  }

  LOG(INFO) << "Writing dhcp.conf, prefix=" << prefix;
  if (!writeDhcpConfigFile(prefix, maskPrefix)) {
    LOG(FATAL) << "writeDhcpConfigFile failed";
  }
  LOG(INFO) << "Running dhcp";
  if (!launchDhcp(true)) {
    LOG(FATAL) << "Couldn't run dhcp";
  } else {
    // radvd config needs to be changed when dhcp starts, so re-launch it
    if (!dhcpLaunched_) {
      dhcpLaunched_ = true;
      scheduleLaunchRadvd();
    }
  }
}

void
SquireNlHandler::configureAndRunDhcp(const folly::IPAddress& ip) {
  std::string prefix =
    folly::to<std::string>(
        ip.mask(cpePrefixLength).str(),
        "/",
        cpePrefixLength);
  configureAndRunDhcp(prefix, ip.mask(cpePrefixLength).str());
}

void
SquireNlThread::preRun() {
  LOG(INFO) << "netlink monitor thread pre-Run";

  LOG(INFO) << "Stopping dhcpd (if running)";
  nlHandler->launchDhcp(false);

  LOG(INFO) << "Stopping radvd (if running)";
  nlHandler->launchRadvd(false);

  // Read current link state
  auto links = netlinkSocket->getAllLinks().get();
  if (!inputPrefix_.empty()) {
    nlHandler->configureAndRunRadvd(inputPrefix_);
    std::string::size_type pos = inputPrefix_.find('/');
    CHECK(pos != std::string::npos);
    nlHandler->configureAndRunDhcp(inputPrefix_, inputPrefix_.substr(0, pos));
  } else {
    auto it = links.find(prefixSourceInterface_);
    auto ifIndex= it->second.ifIndex;
    if (it != links.end()) {
      for (const auto& network : it->second.networks) {
        openr::fbnl::IfAddressBuilder addrBuilder;
        addrBuilder.setPrefix(network);
        addrBuilder.setIfIndex(ifIndex);
        addrBuilder.setValid(true);
        addrBuilder.setFamily(network.first.family());
        auto addr = openr::fbnl::IfAddress(addrBuilder);
        nlHandler->addrEventFunc(prefixSourceInterface_, addr);
      }
    }
  }
  // Read reachable neighbors
  const auto& nbrs = netlinkSocket->getAllReachableNeighbors().get();

  for (const auto& kv: nbrs) {
    openr::fbnl::NeighborBuilder neighborBuilder;
    auto ifName = netlinkSocket->getIfName(kv.second.getIfIndex()).get();
    neighborBuilder.setIfIndex(kv.second.getIfIndex());
    neighborBuilder.setDestination(kv.second.getDestination());
    neighborBuilder.setLinkAddress(kv.second.getLinkAddress().value());
    neighborBuilder.setState(NUD_REACHABLE);
    auto newNeighborEntry = openr::fbnl::Neighbor(neighborBuilder);
    nlHandler->neighborEventFunc(ifName, newNeighborEntry);
  }
}

void
SquireNlThread::run() {
  LOG(INFO) << "netlink monitor thread stared";

  // if we receive a sigint / sigquit, stop the zmq loop
  zmqLoop_.addSocketFd(sigfd_, POLLIN, [&](int) noexcept { zmqLoop_.stop(); });

  LOG(INFO) << "Starting zmq event loop";
  zmqLoop_.run();
  zmqLoop_.waitUntilStopped();
  LOG(INFO) << "Event loop stopped";

  LOG(INFO) << "Stopping dhcpd";
  nlHandler->launchDhcp(false);

  LOG(INFO) << "Stopping radvd";
  nlHandler->launchRadvd(false);
}
} // namespace terragraph
} // namespace facebook
