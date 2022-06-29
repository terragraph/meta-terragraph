/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "e2e/if/gen-cpp2/PassThru_types.h"

namespace facebook {
namespace terragraph {

/*
 * Vendor has a hard-limit of 1024 on fw-bound pass-through message size
 * which when taking out its own header overhead, constrains the effective
 * data size to about 1000.
 *
 * This is defined in the file recipes-radio/wireless-fw/files/20110/HMAC/
 * A_20110T1_RF/backhaul/wgc_backhaul.h, with following as Maximum size of
 * IOCTL response:
 * #define WGC_BH_IOCTL_BUFF_MAX 1000
 */
#define SB_PT_BUFF_LEN 1000

/*
 * creates south bound pass through message buffer
 * from the thrift object
 *
 * @param [in]  thriftMsg message to be sent to wireless-fw / driver
 * @param [out] buff      ptr to buff of len SB_PT_BUFF_LEN
 *
 * @return                sizeof buff used, 0 if there was error
 *
 */
size_t getPtBuff(const thrift::PassThruMsg& thriftMsg, uint8_t* buff);

/*
 * creates north bound pass through message thrift object
 * from the raw buffer from driver
 *
 * @param [in]  buff     ptr to buff sent by wireless-fw / driver
 * @param [in]  len      number of bytes in buff
 * @param [in]  radioMac MAC of radio that originated the message
 *
 * @return              thrift object for NB pass through message in buff
 */
thrift::PassThruMsg getPtThrift(
    const uint8_t* buff, size_t len, const std::string& radioMac = "");
} // namespace terragraph

} // namespace facebook
