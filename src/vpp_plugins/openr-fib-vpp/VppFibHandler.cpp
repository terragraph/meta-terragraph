/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "VppFibHandler.h"

#include "VppClient.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <iterator>
#include <thread>
#include <utility>

#include <folly/Format.h>
#include <folly/gen/Base.h>
#include <folly/gen/Core.h>

#include <openr/common/NetworkUtil.h>
#include <openr/common/Util.h>
#include <openr/if/gen-cpp2/Platform_constants.h>

namespace openr
{

namespace
{

// Lower preference is better
const std::unordered_map<int16_t, uint8_t> clientIdToPreference{
    {786, 10}, // OPENR  (special for Open/R)
};

folly::StringPiece kInvalidClientError{
    "Invalid clientId; Use value between [0, 255]"};

std::optional<uint8_t> toPreference (int16_t clientId)
{
  auto it = clientIdToPreference.find (clientId);
  if (it != clientIdToPreference.end ())
    {
      return it->second;
    }

  // Map clientId - 1:1
  if (clientId >= 0 && clientId <= 255)
    {
      return clientId;
    }

  return it->second;
}

} // namespace

VppFibHandler::VppFibHandler (folly::EventBase *evb, VppClient *vppClient)
    : startTime_ (std::chrono::duration_cast<std::chrono::seconds> (
                      std::chrono::system_clock::now ().time_since_epoch ())
                      .count ()),
      evb_ (evb), vppClient_ (vppClient)
{
  CHECK_NOTNULL (evb_);
  vppClient_->init (evb_);
}

VppFibHandler::~VppFibHandler ()
{
}

int64_t VppFibHandler::aliveSince ()
{
  return startTime_;
}

facebook::fb303::cpp2::fb303_status VppFibHandler::getStatus ()
{
  VLOG (3) << "Received getStatus";
  return facebook::fb303::cpp2::fb303_status::ALIVE;
}

void VppFibHandler::getCounters (std::map<std::string, int64_t> &counters)
{
  evb_->runInEventBaseThreadAndWait ([&]() noexcept {
    getCounters (counters);
  });
}

folly::Future<openr::thrift::SwitchRunState>
VppFibHandler::future_getSwitchRunState ()
{
  VLOG (3) << "Received getSwitchRunState";
  return openr::thrift::SwitchRunState::CONFIGURED;
}

folly::Future<folly::Unit> VppFibHandler::future_addUnicastRoute (
    int16_t clientId, std::unique_ptr<thrift::UnicastRoute> route)
{
  auto routes = std::make_unique<std::vector<thrift::UnicastRoute>> ();
  routes->emplace_back (std::move (*route));
  return future_addUnicastRoutes (clientId, std::move (routes));
}

folly::Future<folly::Unit> VppFibHandler::future_deleteUnicastRoute (
    int16_t clientId, std::unique_ptr<thrift::IpPrefix> prefix)
{
  auto prefixes = std::make_unique<std::vector<thrift::IpPrefix>> ();
  prefixes->emplace_back (std::move (*prefix));
  return future_deleteUnicastRoutes (clientId, std::move (prefixes));
}

folly::Future<folly::Unit> VppFibHandler::future_addUnicastRoutes (
    int16_t clientId,
    std::unique_ptr<std::vector<thrift::UnicastRoute>> routes)
{
  folly::Promise<folly::Unit> promise;
  auto future = promise.getFuture ();
  auto preference = toPreference (clientId);
  if (not preference.has_value ())
    {
      promise.setException (
          std::invalid_argument (kInvalidClientError.data ()));
      return future;
    }

  // Run all route updates in a single eventloop
  evb_->runInEventBaseThread ([this, preference = preference.value (),
                               promise = std::move (promise),
                               routes = std::move (routes)]() mutable {
    for (auto &route : *routes)
      {
        try
          {
            LOG (INFO) << "Updating route for prefix "
                       << toString (route.dest_ref ().value ()) << " with "
                       << route.nextHops_ref ().value ().size ()
                       << " nexthops";
            vppClient_->addRoute (preference, route);
          }
        catch (std::exception const &e)
          {
            promise.setException (e);
            return;
          }
      }
    promise.setValue ();
  });

  return future;
}

folly::Future<folly::Unit> VppFibHandler::future_deleteUnicastRoutes (
    int16_t clientId, std::unique_ptr<std::vector<thrift::IpPrefix>> prefixes)
{
  folly::Promise<folly::Unit> promise;
  auto future = promise.getFuture ();
  auto preference = toPreference (clientId);
  if (not preference.has_value ())
    {
      promise.setException (
          std::invalid_argument (kInvalidClientError.data ()));
      return future;
    }

  evb_->runInEventBaseThread ([this, preference = preference.value (),
                               promise = std::move (promise),
                               prefixes = std::move (prefixes)]() mutable {
    for (auto &prefix : *prefixes)
      {
        try
          {
            LOG (INFO) << "Deleting route for prefix " << toString (prefix);
            vppClient_->deleteRoute (preference, prefix);
          }
        catch (std::exception const &e)
          {
            promise.setException (e);
            return;
          }
      }
    promise.setValue ();
  });

  return future;
}

folly::Future<folly::Unit> VppFibHandler::future_syncFib (
    int16_t clientId,
    std::unique_ptr<std::vector<thrift::UnicastRoute>> routes)
{
  LOG (INFO) << "Syncing FIB with provided routes. Client: " << clientId;

  folly::Promise<folly::Unit> promise;
  auto future = promise.getFuture ();
  auto preference = toPreference (clientId);
  if (not preference.has_value ())
    {
      promise.setException (
          std::invalid_argument (kInvalidClientError.data ()));
      return future;
    }

  evb_->runInEventBaseThread ([this, preference = preference.value (),
                               promise = std::move (promise),
                               routes = std::move (routes)]() mutable {
    try
      {
        vppClient_->syncRoutes (preference, *routes);
      }
    catch (std::exception const &e)
      {
        promise.setException (e);
        return;
      }
    promise.setValue ();
  });
  return future;
}

folly::Future<std::unique_ptr<std::vector<openr::thrift::UnicastRoute>>>
VppFibHandler::future_getRouteTableByClient (int16_t clientId)
{
  LOG (INFO) << "Get unicast routes from FIB for clientId " << clientId;

  // promise here is used only for error case
  folly::Promise<std::unique_ptr<std::vector<openr::thrift::UnicastRoute>>>
      promise;
  auto future = promise.getFuture ();
  auto preference = toPreference (clientId);
  if (not preference.has_value ())
    {
      promise.setException (
          std::invalid_argument (kInvalidClientError.data ()));
      return future;
    }

  evb_->runInEventBaseThread ([this, preference = preference.value (),
                               promise = std::move (promise)]() mutable {
    try
      {
        auto routes = vppClient_->getRoutes (preference);
        promise.setValue (
            std::make_unique<decltype (routes)> (std::move (routes)));
      }
    catch (std::exception const &e)
      {
        promise.setException (e);
      }
  });
  return future;
}

} // namespace openr
