/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "SensorCounters.h"

#include <folly/Format.h>

using std::map;
using std::string;

namespace facebook {
namespace terragraph {
namespace stats {

SensorCounters::SensorCounters() {
  if (sensors_init(nullptr) != 0) {
    LOG(ERROR) << "Unable to init sensors library";
  }
}

SensorCounters::~SensorCounters() { sensors_cleanup(); }

string
SensorCounters::parseChipName(const sensors_chip_name* chip) {
  const int bufSize = 200;
  char chipName[bufSize];
  if (sensors_snprintf_chip_name(chipName, sizeof(chipName), chip) < 0) {
    return std::string("error-not-parseable");
  }
  std::string retName(chipName);
  return retName;
}

std::unordered_map<string, fbzmq::thrift::Counter>
SensorCounters::fetchStats() {
  std::unordered_map<string, fbzmq::thrift::Counter> ret;
  // fetch chips
  const sensors_chip_name* chipName;
  int chipIdx = 0;
  // chip
  while ((chipName = sensors_get_detected_chips(nullptr, &chipIdx)) !=
         nullptr) {
    std::string chipNameStr(parseChipName(chipName));
    sensors_feature const* feature;
    int featureIdx = 0;
    // feature
    while ((feature = sensors_get_features(chipName, &featureIdx)) != 0) {
      sensors_subfeature const* subFeature;
      int subFeatureIdx = 0;
      // sub feature
      while ((subFeature = sensors_get_all_subfeatures(
                  chipName, feature, &subFeatureIdx)) != 0) {
        double sensorValue;
        if (subFeature->flags & SENSORS_MODE_R) {
          int rc =
              sensors_get_value(chipName, subFeature->number, &sensorValue);
          if (rc >= 0) {
            std::string formattedName(chipNameStr + "_" + subFeature->name);
            ret[formattedName] = createGauge(sensorValue);
          }
        }
      }
    }
  }
  return ret;
}

} // stats
} // terragraph
} // facebook
