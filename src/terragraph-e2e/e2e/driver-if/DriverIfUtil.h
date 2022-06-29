/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "DriverNlMessage.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

namespace {
const double k1e7 = 10000000.0;
const double k1e2 = 100.0;
const double k1e3 = 1000.0;
} // namespace

namespace facebook {
namespace terragraph {

/** Internal radio state (for BaseDriverIf). */
struct RadioState {
  /** The radio MAC address. */
  std::string radioMac;

  /**
   * Whether this radio is up.
   *
   * Here, "up" means we have received NODE_INIT_NOTIFY or
   * DR_DEV_UPDOWN_STATUS(UP) and no following DR_DEV_UPDOWN_STATUS(DOWN).
   */
  bool isUp;

  /**
   * Whether GPS_SET_POS_REQ has been sent for this radio.
   */
  bool positionSent;
};

/** Convert a DriverNlMessage to a corresponding thrift::Message. */
thrift::Message driverNl2IfMessage(const DriverNlMessage& drNlMsg);

/** Convert ECEF XYZ coordinates to geodetic coordinates. */
void convertEcefToGeodetic(
    double ecefX,
    double ecefY,
    double ecefZ,
    double& latitude,
    double& longitude,
    double& height);

inline thrift::DrvrStatsSample
createDrvrStatsSample(const std::string& key, uint64_t value) {
  thrift::DrvrStatsSample drvrStatsSample;
  drvrStatsSample.key = key;
  drvrStatsSample.value = value;
  return drvrStatsSample;
}

} // namespace terragraph
} // namespace facebook
