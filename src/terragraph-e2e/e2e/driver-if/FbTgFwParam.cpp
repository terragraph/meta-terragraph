/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "FbTgFwParam.h"

#include "DriverNlMessage.h"
#include <folly/Format.h>

extern "C" {
#include <fb-fw-if/fb_tg_fw_param_pack.h>
}

namespace facebook {
namespace terragraph {

//*****************************************************************************
#define FW_PARAM_ADD2PACK(FW_PARAM_ID, LEN, PARAM_THRIFT)                 \
  if (params.PARAM_THRIFT ## _ref().has_value()) {                        \
    /* VLOG(6) << #PARAM_THRIFT"=" << params.PARAM_THRIFT; */             \
    if (!fw_param_add(hnd, FW_PARAM_ID, params.PARAM_THRIFT ## _ref().value()))\
    {                                                                     \
      LOG(ERROR) << "Error fw_param_add " #FW_PARAM_ID " " #PARAM_THRIFT; \
    }                                                                     \
  }

#define FW_PARAM_ADD2THRIFT(FW_PARAM_ID, LEN, PARAM_THRIFT)    \
  if (fwParams[FW_PARAM_ID].flag) {                            \
    params.PARAM_THRIFT ## _ref() = (uint32_t)fwParams[FW_PARAM_ID].val; \
  }

/*****************************************************************************/
/** Serializes thrift object to msg packet in given buffer.
 *
 * @param [in]   params   thrift obj with input parameters
 * @param [out]  buf      output buf for data packet
 * @param [in]   buflen   buf size in bytes
 *
 * @return       actual size of the data packet in bytes
 *****************************************************************************/
size_t
fbFwParams2Pack(
    const facebook::terragraph::thrift::FwOptParams& params,
    uint8_t* buf,
    size_t bufLen) // TODO currently length limited by driver
{
  if (buf == nullptr || bufLen == 0) {
    LOG(ERROR) << "Error input args buf, bufLen";
    return 0;
  }
  fw_par_hnd_t fw_par_hnd;
  fw_par_hnd_t* hnd = fw_param_get_hnd_def(&fw_par_hnd, buf, bufLen);
  if (hnd == nullptr) {
    LOG(ERROR) << "Error get hnd=nullptr";
    return 0;
  }

  FW_PARAM_CONFIGURE_ALL(FW_PARAM_ADD2PACK);

  size_t pack_size = fw_param_get_pack_len(hnd);
  fw_param_free_hnd(hnd);

  return pack_size;
}

/*****************************************************************************/
/** De-serializes msg TLV packet in given buffer to thrift object
 *
 * @param [in]   buf      output buf for data packet
 * @param [in]   buflen   buf size in bytes
 * @param [out]  params   thrift obj with output parameters
 *
 * @return       True if success false otherwise
 *****************************************************************************/
bool
fbFwParamsUnpack(
    const uint8_t* buf,
    const size_t bufLen,
    facebook::terragraph::thrift::FwOptParams& params) {

  fw_par_val_t fwParams[eFW_PARAM_LIST_LEN];
  memset(&fwParams, 0, sizeof(fwParams));

  if (!fw_param_get_all(buf, bufLen, &fwParams[0], eFW_PARAM_LIST_LEN)) {
    LOG(ERROR) << " Failed to parse FW Params";
    return false;
  }

  FW_PARAM_CONFIGURE_ALL(FW_PARAM_ADD2THRIFT);

  return true;
}

} // namespace terragraph
} // namespace facebook
