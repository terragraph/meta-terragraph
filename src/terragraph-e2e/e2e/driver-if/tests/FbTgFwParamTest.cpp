/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <gtest/gtest.h>

#include <folly/init/Init.h>

#include "../FbTgFwParam.h"

extern "C" {
#include <fb-fw-if/fb_tg_fw_param_pack.h>
}

using namespace facebook::terragraph;

#define SET_PARAM(FW_PARAM_ID, LEN, PARAM_THRIFT) \
  fwOptParams.PARAM_THRIFT ## _ref() = valU32FromI64;

#define TESTING_VAL_INCREMENT 0x33
// 0x33 increment guarantees values iterate from <0 to UINT*_MAX> inclusive

// Verify serialized values against original ones
TEST(FbTgFwParam, One) {
  facebook::terragraph::thrift::FwOptParams fwOptParams;
  uint8_t buf[1024];
  size_t buflen = sizeof(buf);

  size_t serialLen;
  size_t packLen;

  uint8_t valU8;
  uint16_t valU16;
  uint32_t valU32;

  int64_t valU32FromI64;

  auto verifyParam = [&buf, &buflen](fwParamId parId, uint32_t actualVal) {
    uint32_t serializedVal;
    size_t serializedParamLen = 0;
    bool ret = fw_par_get_by_id(parId, buf, buflen, &serializedVal);
    EXPECT_TRUE(ret);
    if (ret) {
      EXPECT_EQ(actualVal, serializedVal)
          << "Incorrect serialized Val=" << serializedVal
          << " of param Id=" << parId << ". Expected actual Val=" << actualVal;
      serializedParamLen =
          sizeof(fw_par_id_size_t) + fw_param_cfg_get_len(parId);
    }
    return serializedParamLen;
  };

  auto alignTo4Bytes = [](size_t input) { return ((input + (4 - 1)) / 4) * 4; };

  for (uint32_t v = 0; v <= UINT8_MAX; v += TESTING_VAL_INCREMENT) // Test value
  {
    valU8 = static_cast<uint8_t>(v);
    valU16 = static_cast<uint16_t>(v * (UINT16_MAX / UINT8_MAX));
    valU32 = v * (UINT32_MAX / UINT8_MAX);
    valU32FromI64 = valU32;

    FW_PARAM_CONFIGURE_ALL(SET_PARAM);

    // invalid buffer
    serialLen = fbFwParams2Pack(fwOptParams, nullptr, buflen);
    EXPECT_EQ(0, serialLen);
    serialLen = fbFwParams2Pack(fwOptParams, buf, 0);
    EXPECT_EQ(0, serialLen);

    serialLen = fbFwParams2Pack(fwOptParams, buf, buflen);
    packLen = sizeof(fw_par_pack_size_t);
    // Verify all params
    for (int i = 0; i < eFW_PARAM_LIST_LEN; ++i) {
      fwParamId id = (fwParamId)i;
      size_t parLen = fw_param_cfg_get_len(id);
      // Select corresponding size of the test value
      switch (parLen) {
        case 1:
          packLen += verifyParam(id, valU8);
          break;
        case 2:
          packLen += verifyParam(id, valU16);
          break;
        case 4:
          packLen += verifyParam(id, valU32);
          break;
        default:
          EXPECT_TRUE(false)
              << "Incorrect serialLen=" << serialLen << " of param id=" << id;
      }
    }
    EXPECT_EQ(alignTo4Bytes(packLen), serialLen);
  }
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
