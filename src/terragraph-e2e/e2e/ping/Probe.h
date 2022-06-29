/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

namespace facebook {
namespace terragraph {

// The size of the probe body. This should be
// enough to embed the id (signature) and timestamps
const int kProbeDataLen = 32;

//
// Defines the structure of the probe body. The below timestamps are all in
// usecs, and defined with 32-bit resolution. We assume that this is enough to
// complete a single probing round
//

struct ProbeBody {
  // the signature that the sender puts in, mainly
  // used to identify a valid probe
  uint32_t signature;

  // the timestamp when the probe was sent (usec)
  uint32_t pingerSentTime;

  // the timestamp when probe was received by target
  uint32_t targetRcvdTime;

  // the timestamp when target replied with this probe
  uint32_t targetRespTime;

  // traffic class used by this probe
  uint8_t tclass;

  // padding
  char padding[kProbeDataLen - 4 * sizeof(uint32_t) - sizeof(uint8_t)];
};

} // namespace terragraph
} // namespace facebook
