/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <folly/Expected.h>
#include <folly/futures/Future.h>
#include <folly/io/async/EventBase.h>

#include <openr/common/NetworkUtil.h>
#include <openr/common/Util.h>
#include <openr/if/gen-cpp2/FibService.h>
#include <openr/if/gen-cpp2/NeighborListenerClientForFibagent.h>

namespace openr
{

class VppClient;

/**
 * This class implements OpenR's Platform.FibService thrift interface for
 * programming routes on Linux platform for packet routing in kernel
 *
 * > Note on Thread Safety
 * There can be multiple thrift worker threads. All thrift requests are
 * serialized into the EventBase passed in the constructor. This will ensure
 * thread safety of all data attributes of underlying VppClient code
 *
 * > Note on clientId
 * Thrift API exposes int16_t for ClientID. When programming routes, it is
 * encoded as `preference` attribute of next-hop. Lower preference is better.
 * The preference is `uint8_t`. As of now VppFibHandler will throw exception if
 * ClientID is not known beforehand. To support more ClientID, simply add
 * mapping in `clientIdToPreference` map defined in VppFibHandler.cpp
 *
 * > Note on MPLS
 * MPLS route APIs are not supported as of now, but VPP do support. The MPLS
 * API code to be added in VppFibHandler should follow above ClientID checks
 * and ensure thread safety. For implementation in VPP, refer to class
 * documentation in VppClient.h
 */
class VppFibHandler : public thrift::FibServiceSvIf
{
public:
  VppFibHandler (folly::EventBase *evb, VppClient *vppClient);
  ~VppFibHandler () override;

  /**
   * Disable copy & assignment operations
   */
  VppFibHandler (const VppFibHandler &) = delete;
  VppFibHandler &operator= (const VppFibHandler &) = delete;

  /**
   * BaseService APIs
   */
  int64_t aliveSince () override;

  facebook::fb303::cpp2::fb303_status getStatus () override;

  void getCounters (std::map<std::string, int64_t> &counters);

  /**
   * Run State
   */

  folly::Future<openr::thrift::SwitchRunState>
  future_getSwitchRunState () override;

  /**
   * UnicastRoutes APIs
   */

  folly::Future<folly::Unit> future_addUnicastRoute (
      int16_t clientId, std::unique_ptr<thrift::UnicastRoute> route) override;

  folly::Future<folly::Unit> future_deleteUnicastRoute (
      int16_t clientId, std::unique_ptr<thrift::IpPrefix> prefix) override;

  folly::Future<folly::Unit> future_addUnicastRoutes (
      int16_t clientId,
      std::unique_ptr<std::vector<thrift::UnicastRoute>> routes) override;

  folly::Future<folly::Unit> future_deleteUnicastRoutes (
      int16_t clientId,
      std::unique_ptr<std::vector<thrift::IpPrefix>> prefixes) override;

  folly::Future<folly::Unit> future_syncFib (
      int16_t clientId,
      std::unique_ptr<std::vector<thrift::UnicastRoute>> routes) override;

  folly::Future<std::unique_ptr<std::vector<openr::thrift::UnicastRoute>>>
  future_getRouteTableByClient (int16_t clientId) override;

private:
  // Time when service started, in number of seconds, since epoch
  const int64_t startTime_{0};

  // folly::EventBase pointer
  folly::EventBase *evb_{nullptr};

  // VPP client pointer
  VppClient *vppClient_;
};

} // namespace openr
