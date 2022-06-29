/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <chrono>

#include <fbzmq/service/if/gen-cpp2/Monitor_types.h>

namespace facebook {
namespace terragraph {
namespace stats {

/**
 * Helper functions for creating fbzmq::thrift::Counter structs
 */
class ZmqCounterUtils {
 public:
  // return a gauge value with a timestamp of now
  static fbzmq::thrift::Counter createGauge(double value) {
    fbzmq::thrift::Counter retValue;
    retValue.value_ref() = value;
    retValue.valueType_ref() = fbzmq::thrift::CounterValueType::GAUGE;
    auto now = std::chrono::system_clock::now();
    // current unixtime in ms
    retValue.timestamp_ref() =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    return retValue;
  }

  // return a counter value with a timestamp of now
  static fbzmq::thrift::Counter createCounter(double value) {
    fbzmq::thrift::Counter retValue;
    retValue.value_ref() = value;
    retValue.valueType_ref() = fbzmq::thrift::CounterValueType::COUNTER;
    auto now = std::chrono::system_clock::now();
    // current unixtime in ms
    retValue.timestamp_ref() =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
    return retValue;
  }
};

} // namespace stats
} // namespace terragraph
} // namespace facebook
