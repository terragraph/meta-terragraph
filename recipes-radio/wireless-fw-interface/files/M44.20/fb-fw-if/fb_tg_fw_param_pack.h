/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef FB_TG_FW_PARAM_H_
#define FB_TG_FW_PARAM_H_

#ifndef TG_FIRMWARE
enum { FB_FALSE, FB_TRUE };
#include <stdint.h>
#define EXTSTR(s) s
#define DEFINE_GLOBAL_EXTSTR(ID)
#define USE_GLOBAL_EXTSTR(ID) #ID
#endif

#include <stddef.h>

#ifdef TG_FIRMWARE
#include "wgc_backhaul_fb.h"
#endif

//*****************************************************************************
#define FW_PARAM_SET_ENUMS(ID, LEN, THRIFT_NAME) ID,

#define FW_PARAM_SET_TLVR_STRINGS(ID, LEN, THRIFT_NAME) DEFINE_GLOBAL_EXTSTR(ID)

#define FW_PARAM_SET_TLVR_DEFAULTS(ID, LEN, THRIFT_NAME)                       \
    {.name = USE_GLOBAL_EXTSTR(ID), .len = LEN},

#define eFW_PARAM_BLER_TO_PER_LOWER_MASK 0x0f
#define eFW_PARAM_BLER_TO_PER_LOWER_SHIFT 0
#define eFW_PARAM_BLER_TO_PER_UPPER_MASK 0xf0
#define eFW_PARAM_BLER_TO_PER_UPPER_SHIFT 4
//-----------------------------------------------------------------------------
// For backward compatibility do not modify existing parameters!!!

//  SET( PARAM_ENUM_ID/NAME, SIZE_IN_BYTES, parm_name_in_thrift_obj )
#define FW_PARAM_CONFIGURE_ALL(SET)                                            \
    SET(eFW_PARAM_GPIO_CONFIG, 4, gpioConfig)                                  \
                                                                               \
    SET(eFW_PARAM_MCS, 1, mcs)                                                 \
    SET(eFW_PARAM_TX_POWER, 2, txPower)                                        \
    SET(eFW_PARAM_RX_BUFFER, 1, rxBuffer)                                      \
    SET(eFW_PARAM_BEAM_CONFIG, 1, beamConfig)                                  \
    SET(eFW_PARAM_TX_BEAM_INDEX, 2, txBeamIndex)                               \
    SET(eFW_PARAM_RX_BEAM_INDEX, 2, rxBeamIndex)                               \
                                                                               \
    SET(eFW_PARAM_NUM_OF_HB_LOSS_TO_FAIL, 4, numOfHbLossToFail)                \
    SET(eFW_PARAM_STATS_LOG_INTERVAL, 4, statsLogInterval)                     \
    SET(eFW_PARAM_STATS_PRINT_INTERVAL, 4, statsPrintInterval)                 \
                                                                               \
    SET(eFW_PARAM_FORCE_GPS_DISABLE, 1, forceGpsDisable)                       \
                                                                               \
    SET(eFW_PARAM_LSM_ASC_RSP_TMO, 2, lsmAssocRespTimeout)                     \
    SET(eFW_PARAM_LSM_SEND_ASC_REQ_RETRY, 1, lsmSendAssocReqRetry)             \
    SET(eFW_PARAM_LSM_ASC_RSP_ACK_TMO, 2, lsmAssocRespAckTimeout)              \
    SET(eFW_PARAM_LSM_SEND_ASC_RSP_RETRY, 1, lsmSendAssocRespRetry)            \
    SET(eFW_PARAM_LSM_REPEAT_ACK_INTERVAL, 2, lsmRepeatAckInterval)            \
    SET(eFW_PARAM_LSM_REPEAT_ACK, 1, lsmRepeatAck)                             \
    SET(eFW_PARAM_LSM_FIRST_HEARTB_TMO, 2, lsmFirstHeartbTimeout)              \
                                                                               \
    SET(eFW_PARAM_TX_SLOT0_START, 2, txSlot0Start)                             \
    SET(eFW_PARAM_TX_SLOT0_END, 2, txSlot0End)                                 \
    SET(eFW_PARAM_TX_SLOT1_START, 2, txSlot1Start)                             \
    SET(eFW_PARAM_TX_SLOT1_END, 2, txSlot1End)                                 \
    SET(eFW_PARAM_TX_SLOT2_START, 2, txSlot2Start)                             \
    SET(eFW_PARAM_TX_SLOT2_END, 2, txSlot2End)                                 \
                                                                               \
    SET(eFW_PARAM_RX_SLOT0_START, 2, rxSlot0Start)                             \
    SET(eFW_PARAM_RX_SLOT0_END, 2, rxSlot0End)                                 \
    SET(eFW_PARAM_RX_SLOT1_START, 2, rxSlot1Start)                             \
    SET(eFW_PARAM_RX_SLOT1_END, 2, rxSlot1End)                                 \
    SET(eFW_PARAM_RX_SLOT2_START, 2, rxSlot2Start)                             \
    SET(eFW_PARAM_RX_SLOT2_END, 2, rxSlot2End)                                 \
                                                                               \
    SET(eFW_PARAM_BF_AGC, 2, bfAgc)                                            \
    SET(eFW_PARAM_LINK_AGC, 2, linkAgc)                                        \
    SET(eFW_PARAM_RESP_NODE_TYPE, 1, respNodeType)                             \
    SET(eFW_PARAM_TX_GOLAY_INDEX, 1, txGolayIdx)                               \
    SET(eFW_PARAM_RX_GOLAY_INDEX, 1, rxGolayIdx)                               \
    SET(eFW_PARAM_TPC_ENABLE, 1, tpcEnable)                                    \
    SET(eFW_PARAM_TPC_REF_RSSI, 2, tpcRefRssi)                                 \
    SET(eFW_PARAM_TPC_REF_STF_SNR_STEP1, 2, tpcRefStfSnrStep1)                 \
    SET(eFW_PARAM_TPC_REF_STF_SNR_STEP2, 2, tpcRefStfSnrStep2)                 \
    SET(eFW_PARAM_TPC_REF_DEL_POWER_STEP1, 2, tpcDelPowerStep1)                \
    SET(eFW_PARAM_TPC_REF_DEL_POWER_STEP2, 2, tpcDelPowerStep2)                \
                                                                               \
    SET(eFW_PARAM_BF_MODE, 1, bfMode)                                          \
    SET(eFW_PARAM_TPC_REF_STF_SNR_STEP3, 2, tpcRefStfSnrStep3)                 \
    SET(eFW_PARAM_TPC_REF_DEL_POWER_STEP3, 2, tpcDelPowerStep3)                \
    SET(eFW_PARAM_MIN_TX_POWER, 2, minTxPower)                                 \
    SET(eFW_PARAM_TPC_ALPHA_UP_RSSI, 2, tpcAlphaUpRssiStep3Q10)                \
    SET(eFW_PARAM_TPC_ALPHA_DOWN_RSSI, 2, tpcAlphaDownRssiStep3Q10)            \
    SET(eFW_PARAM_LA_INV_PER_TARGET, 2, laInvPERTarget)                        \
    SET(eFW_PARAM_LA_CONVERGENCE_FACTOR, 2, laConvergenceFactordBperSFQ8)      \
    SET(eFW_PARAM_LA_MAX_MCS, 1, laMaxMcs)                                     \
    SET(eFW_PARAM_LA_MIN_MCS, 1, laMinMcs)                                     \
    SET(eFW_PARAM_MAX_AGC_ENABLED, 1, maxAgcTrackingEnabled)                   \
    SET(eFW_PARAM_MAX_AGC_MARGIN, 1, maxAgcTrackingMargindB)                   \
    SET(eFW_PARAM_NO_LINK_TIMEOUT, 2, noLinkTimeout)                           \
    SET(eFW_PARAM_WSEC_ENABLE, 1, wsecEnable)                                  \
    SET(eFW_PARAM_KEY0, 4, key0)                                               \
    SET(eFW_PARAM_KEY1, 4, key1)                                               \
    SET(eFW_PARAM_KEY2, 4, key2)                                               \
    SET(eFW_PARAM_KEY3, 4, key3)                                               \
    SET(eFW_PARAM_CTRL_SUPERFRAME, 1, controlSuperframe)                       \
    SET(eFW_PARAM_TPC_ALPHA_UP_TARGET_RSSI, 2, tpcAlphaUpTargetRssiStep3Q10)   \
    SET(eFW_PARAM_CRS_SCALE, 1, crsScale)                                      \
    SET(eFW_PARAM_TPC_ALPHA_DOWN_TARGET_RSSI, 2,                               \
        tpcAlphaDownTargetRssiStep3Q10)                                        \
    SET(eFW_PARAM_LA_TPC_LDPC, 1, latpcUseIterations)                          \
    SET(eFW_PARAM_MAX_TX_POWER, 1, maxTxPower)                                 \
    SET(eFW_PARAM_POLARITY, 1, polarity)                                       \
    SET(eFW_PARAM_LINK_IMPAIRMENT_ENABLE, 1, linkImpairmentDetectionEnable)    \
    SET(eFW_PARAM_LINK_IMPAIRMENT_SHORTPENDING, 2, linkImpairmentShortPending) \
    SET(eFW_PARAM_LINK_IMPAIRMENT_SHORTUP, 2, linkImpairmentShortUp)           \
    SET(eFW_PARAM_LINK_IMPAIRMENT_LONGPENDING, 2, linkImpairmentLongPending)   \
    SET(eFW_PARAM_MAX_TX_POWER_PER_MCS, 4, maxTxPowerPerMcs)                   \
    SET(eFW_PARAM_TOPO_SCAN_ENABLE, 1, topoScanEnable)                         \
    SET(eFW_PARAM_RESTRICT_TO_SF_PARITY, 1, restrictToSfParity)                \
    SET(eFW_PARAM_MAX_AGC_IF_GAIN_PER_INDEX, 2, maxAgcIfGaindBperIndexQ8)      \
    SET(eFW_PARAM_MAX_AGC_MAX_RF_GAIN, 1, maxAgcMaxRfGainIndex)                \
    SET(eFW_PARAM_MAX_AGC_MIN_RF_GAIN, 1, maxAgcMinRfGainIndex)                \
    SET(eFW_PARAM_MAX_AGC_MAX_IF_GAIN, 1, maxAgcMaxIfGainIndex)                \
    SET(eFW_PARAM_MAX_AGC_MIN_IF_GAIN, 1, maxAgcMinIfGainIndex)                \
    SET(eFW_PARAM_MAX_AGC_RAW_SCALE, 2, maxAgcRawAdcScaleFactorQ8)             \
    SET(eFW_PARAM_MAX_AGC_RF_SCALE, 2, maxAgcRfGaindBperIndexQ8)               \
    SET(eFW_PARAM_MAX_AGC_RF_HILO, 2, maxAgcRfGainHiLo)                        \
    SET(eFW_PARAM_MAX_AGC_TARGET_RAW_ADC, 1, maxAgcTargetRawAdc)               \
    SET(eFW_PARAM_MAX_AGC_USE_MIN_RSSI, 1, maxAgcUseMinRssi)                   \
    SET(eFW_PARAM_MAX_AGC_USE_SAME_STA, 1, maxAgcUseSameForAllSta)             \
    SET(eFW_PARAM_MAX_AGC_IF_SWEET_MAX, 1, maxAgcMaxIfSweetGainRange)          \
    SET(eFW_PARAM_MAX_AGC_IF_SWEET_MIN, 1, maxAgcMinIfSweetGainRange)          \
    SET(eFW_PARAM_MAX_AGC_MIN_RSSI, 1, maxAgcMinRssi)                          \
    SET(eFW_PARAM_CB2_ENABLE, 1, cb2Enable)                                    \
    SET(eFW_PARAM_MAX_TX_POWER_PER_MCS_EDMG, 4, maxTxPowerPerMcsEdmg)          \
    SET(eFW_PARAM_MAX_MCS_FALLBACK, 1, noTrafficMaxMcsFallback)                \
    SET(eFW_PARAM_TX_POWER_TABLE_LINEAR, 1, txPowerTableLinear)                \
    SET(eFW_PARAM_AUTO_PBF_ENABLE, 1, autoPbfEnable)                           \
    SET(eFW_PARAM_LINK_IMPAIR_CONFIG, 4, latpcLinkImpairConfig)                \
    SET(eFW_PARAM_LA_TPC_100_PER, 2, latpc100PercentPERDrop)                   \
    SET(eFW_PARAM_IBF_PROCEDURE_TYPE, 1, ibfProcedureType)                     \
    SET(eFW_PARAM_IBF_NUMBER_OF_BEAMS, 1, ibfNumberOfBeams)                    \
    SET(eFW_PARAM_IBF_SET_1_RFIC_BITMAP, 1, ibfSet1RficBitmap)                 \
    SET(eFW_PARAM_IBF_SET_2_RFIC_BITMAP, 1, ibfSet2RficBitmap)                 \
    SET(eFW_PARAM_IBF_CODEBOOK_VARIANT, 1, ibfCodebookVariant)                 \
    SET(eFW_PARAM_USE_UPDATE_AWV_FOR_PBF, 1, useUpdateAwvForPbf)               \
    SET(eFW_PARAM_BLER_TO_PER, 1, latpcBlerToPer)                              \
    SET(eFW_PARAM_MTPO_ENABLED, 1, mtpoEnabled)                                \
    SET(eFW_PARAM_MTPO_HYSTERESIS, 2, mtpoPhaseHysteresis_dBQ2)                \
    SET(eFW_PARAM_IBF_USE_RSSI, 1, ibfUseRssi)                                 \
    SET(eFW_PARAM_MCS_TABLE_1_4, 4, mcsLqmQ3_1_4)                              \
    SET(eFW_PARAM_MCS_TABLE_5_8, 4, mcsLqmQ3_5_8)                              \
    SET(eFW_PARAM_MCS_TABLE_9_12, 4, mcsLqmQ3_9_12)                            \
    SET(eFW_PARAM_MCS_TABLE_13_16, 4, mcsLqmQ3_13_16)                          \
    SET(eFW_PARAM_MAX_TX_POWER_SET1, 1, maxTxPowerSet1)                        \
    SET(eFW_PARAM_AUTO_PBF_MTPO_TX_POWER, 1, autoPbfMtpoTxPower)               \
    SET(eFW_PARAM_RX_MAX_MCS, 1, rxMaxMcs)                                     \
    SET(eFW_PARAM_TCP_TUNING_CONFIG, 2, tcpTuningConfig)                       \
    SET(eFW_PARAM_HTSF_MSG_INTERVAL, 1, htsfMsgInterval)                       \
    SET(eFW_PARAM_HTSF_SYNC_ENABLE, 1, htsfSyncEnable)                         \
    SET(eFW_PARAM_HTSF_RF_SYNC_KP_KI, 4, htsfRfSyncKpKi)                       \
    SET(eFW_PARAM_HTSF_PPS_SYNC_KP_KI, 4, htsfPpsSyncKpKi)                     \
    SET(eFW_PARAM_TPC_PB_ENABLE, 1, tpcPBEnable)                               \
    SET(eFW_PARAM_MSDU_PER_MPDU, 1, msduPerMpdu)

//   ^                                                           |
//   |                                                           |
//   \---- - ADD NEW PARAMS HERE.                                |
//         - Do not modify existing parameters!                  |
//         - Macro will add new params to fw_par_cfg_tlvs,       |
//           C++ interface FbTgFwParam.cpp and                   |
//           the unit test FbTgFwParamTest.cpp                   |
//---------------------------------------------------------------+

//*****************************************************************************
// FW_PAR_PACK_MAX_SIZE <= MAX_VAR_DATA_LEN
#define FW_PAR_PACK_MAX_SIZE ((size_t)512) // align the size to 4 Bytes
typedef uint16_t fw_par_pack_size_t; // must store FW_PAR_PACK_MAX_SIZE value
typedef uint8_t fw_par_id_size_t;    // param enum min size

//*****************************************************************************
typedef struct __attribute__((__packed__)) {
    fw_par_pack_size_t data_outlen;
    uint8_t data[FW_PAR_PACK_MAX_SIZE - sizeof(fw_par_pack_size_t)];
} fw_par_pack_t;

//*****************************************************************************
typedef enum FwParamId {
    FW_PARAM_CONFIGURE_ALL(FW_PARAM_SET_ENUMS)

    // INTERNAL enums --------------------------------------------
    eFW_PARAM_LIST_LEN,
    eFW_PARAM_NONE = ((fw_par_id_size_t)-1) >> 1,
} fwParamId;

//*****************************************************************************

//*****************************************************************************
typedef struct fw_par_hnd fw_par_hnd_t;

//*****************************************************************************
typedef struct __attribute__((__packed__)) {
    uint32_t val;
    int8_t flag; // validity
} fw_par_val_t;

//-----------------------------------------------------------------------------
typedef struct {
    const char *name;
    size_t len;
} fw_par_tlv_t;

//*****************************************************************************
struct fw_par_hnd {
    fw_par_pack_t *pack_buf_pt;
    size_t pack_buf_size;

    uint8_t *pack_data_pt;
    size_t pack_data_max_len;
    size_t pack_data_to_decode_len;
    size_t pack_idx;

    const fw_par_tlv_t *cfg_tlvs_pt;
};

//*****************************************************************************
//   PUBLIC APIs
//*****************************************************************************

/*****************************************************************************/
/** Initializes param pack handler.
 *
 * @param [in/out]  *par_hnd      handler allocated externally
 * @param [in]      *pack_buf_pt  pack buffer allocated externally
 * @param [in]       buf_size     pack buffer size in bytes
 * @param [in]      *tlvs_cfg     params configuration
 *
 * @return pointer to handler or NULL on error
 *****************************************************************************/
fw_par_hnd_t *fw_param_get_hnd(fw_par_hnd_t *par_hnd,
                               const uint8_t *pack_buf_pt, size_t buf_size,
                               const fw_par_tlv_t *tlvs_cfg);

/*****************************************************************************/
/** Initializes param pack handler with default configuration.
 *
 * @param [in/out]  *par_hnd      handler allocated externally
 * @param [in]      *pack_buf_pt  pack buffer allocated externally
 * @param [in]       buf_size     pack buffer size in bytes
 *
 * @return pointer to handler or NULL on error
 *****************************************************************************/
fw_par_hnd_t *fw_param_get_hnd_def(fw_par_hnd_t *par_hnd,
                                   const uint8_t *pack_buf_pt, size_t buf_size);

/*****************************************************************************/
/** Get parameter name in c string.
 *
 * @param [in]       id     parameter enum id
 *
 * @return pointer to const c string with parameter name or NULL on error
 *****************************************************************************/
const char *fw_param_get_str(fwParamId id);

/*****************************************************************************/
/** Get parameter length in bytes.
 *
 * @param [in]       id     parameter enum id
 *
 * @return returns param length in bytes or 0 on error
 *****************************************************************************/
size_t fw_param_cfg_get_len(fwParamId id);

/*****************************************************************************/
/** Adds param to pack identified by handler.
 *  Iterator is stored in the handler.
 *
 * @param [in/out]  *hnd      handler allocated externally
 * @param [in]       par_id   parameter enum id
 * @param [in]       val      parameter value
 *
 * @return FB_TRUE when parameter added successfully to pack else FB_FALSE
 *****************************************************************************/
int8_t fw_param_add(fw_par_hnd_t *hnd, fwParamId par_id, uint32_t val);

/*****************************************************************************/
/** Extracts all params from pack identified by handler and copies the data
 *  to storage addressed by val pointer.
 *
 * @param [in]    *buf        packet buffer
 * @param [in]     len        size of packet buffer
 * @param [out]   *val        storage for parameter data
 * @param [in]     vals_len   max # of the parameters in the storage
 *
 * @return FB_TRUE when parameters extracted successfully from pack else
 *FB_FALSE
 *****************************************************************************/
int8_t fw_param_get_all(const uint8_t *buf, size_t len, fw_par_val_t *vals,
                        size_t vals_len);

/*****************************************************************************/
/** Log all params stored by fw_param_get_all function in *vals array.
 *
 * @param [in]  *vals     array of parameters
 *
 * @return N/A
 *****************************************************************************/
void fw_param_print(const fw_par_val_t *vals);

/*****************************************************************************/
/** Extracts param pack length in bytes from handler.
 *
 * @param [in]  *hnd      handler allocated externally
 *
 * @return length if extracted successfully else 0
 *****************************************************************************/
size_t fw_param_get_pack_len(fw_par_hnd_t *hnd);

/*****************************************************************************/
/** Extracts pointer to param pack from handler.
 *
 * @param [in]  *hnd      handler allocated externally
 *
 * @return pointer to param pack if extracted successfully else NULL
 *****************************************************************************/
uint8_t *fw_param_get_pack_pt(const fw_par_hnd_t *hnd);

/*****************************************************************************/
/** Extracts param pack info from handler.
 *  Should be called only once after adding all needed params (fw_param_add).
 *
 * @param [in]   *hnd    handler allocated externally
 * @param [out]  *len    storage for pack length value
 *
 * @return pointer to param pack if info extracted successfully else NULL
 *****************************************************************************/
uint8_t *fw_param_get_pack(fw_par_hnd_t *hnd, size_t *len);

/*****************************************************************************/
/** Extracts param of given id from pack identified by handler.
 *
 * @param [in]     par_id     parameter enum id
 * @param [in]    *buf        pack with params
 * @param [in]     len        length of data in buf
 * @param [out]   *val_out    extracted parameter value
 *
 * @return FB_TRUE when parameter value extracted successfully else FB_FALSE
 *****************************************************************************/
int8_t fw_par_get_by_id(fwParamId req_par_id, const uint8_t *buf, size_t len,
                        uint32_t *val_out);

/*****************************************************************************/
/** Destructor for param handler object.
 *
 * @param [in]  *hnd      pointer to handler
 *
 * @return FB_TRUE/FB_FALSE
 *****************************************************************************/
int8_t fw_param_free_hnd(fw_par_hnd_t *hnd);

#endif
