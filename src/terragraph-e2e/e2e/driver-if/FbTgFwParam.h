/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/FwOptParams_types.h"

namespace facebook {
namespace terragraph {

/*****************************************************************************/
/** Serializes parameters from thrift object to
 *  fw msg packet in given buffer.
 *
 * @param [in]   params   thrift obj with input parameters
 * @param [out]  buf      output buf for data packet
 * @param [in]   buflen   buf size in bytes
 *
 * @return       actual size of the data packet in bytes
 *****************************************************************************/
size_t fbFwParams2Pack(
    const facebook::terragraph::thrift::FwOptParams& params,
    uint8_t* buf,
    size_t bufLen);

/*****************************************************************************/
/** De-serializes msg TLV packet in given buffer to thrift object
 *
 * @param [in]   buf      output buf for data packet
 * @param [in]   buflen   buf size in bytes
 * @param [out]  params   thrift obj with output parameters
 *
 * @return       True if success false otherwise
 *****************************************************************************/
bool fbFwParamsUnpack(
    const uint8_t* buf,
    const size_t bufLen,
    facebook::terragraph::thrift::FwOptParams& params);

} // namespace terragraph
} // namespace facebook
