/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "fb_tg_fw_param_pack.h"

#ifdef TG_FIRMWARE
#include "fb_types.h"
#include "fb_vendor_defines.h"
#include "fb_tg_fw_pt_if.h"
#include "fb_tg_fw_log.h"
#include "fb_tg_fw_trace.h"
#else // E2E Driver Interface (e2e-driver-if)
#include <string.h>
#define TGF_DEBUG(form, ...)
#define TGF_ERROR(form, ...)
#endif

//*****************************************************************************
FW_PARAM_CONFIGURE_ALL(FW_PARAM_SET_TLVR_STRINGS)

static const fw_par_tlv_t fw_par_cfg_tlvs[eFW_PARAM_LIST_LEN] = {
    FW_PARAM_CONFIGURE_ALL(FW_PARAM_SET_TLVR_DEFAULTS)};

//*****************************************************************************
//*****************************************************************************
const char *
fw_param_get_str(fwParamId id) {
    if (id >= eFW_PARAM_LIST_LEN) {
        return EXTSTR("WrongParamId!");
    }
    return (fw_par_cfg_tlvs[id].name);
}

//*****************************************************************************
size_t
fw_param_cfg_get_len(fwParamId id) {
    if (id >= eFW_PARAM_LIST_LEN) {
        return 0;
    }
    return (fw_par_cfg_tlvs[id].len);
}

//*****************************************************************************
fw_par_hnd_t *
fw_param_get_hnd(fw_par_hnd_t *par_hnd, const uint8_t *pack_buf_pt,
                 size_t buf_size, const fw_par_tlv_t *tlvs_cfg) {
    fw_par_hnd_t *hnd;
    fw_par_pack_t *pack_buf;
    fw_par_pack_size_t pack_buf_size = buf_size;

    if (par_hnd == NULL || pack_buf_pt == NULL) {
        TGF_DEBUG("Unsupported fw_param_get_hnd parameters\n");
        return NULL;
    }

    if (buf_size > FW_PAR_PACK_MAX_SIZE) {
        pack_buf_size = FW_PAR_PACK_MAX_SIZE;
        TGF_DEBUG("Warning buf size=%uB truncated to %uB\n", (uint32_t)buf_size,
                  pack_buf_size);
    }
    TGF_DEBUG("BUF pack size=%uB", pack_buf_size);

    if (pack_buf_size == 0) {
        TGF_ERROR("Error pack size=0\n");
        return NULL;
    }

    hnd = par_hnd;

    pack_buf = (fw_par_pack_t *)pack_buf_pt;
    hnd->pack_buf_size = pack_buf_size;
    hnd->pack_buf_pt = pack_buf;

    hnd->pack_data_pt = pack_buf->data;
    hnd->pack_data_max_len = hnd->pack_buf_size - sizeof(pack_buf->data_outlen);
    hnd->pack_data_to_decode_len = pack_buf->data_outlen;
    hnd->pack_idx = sizeof(pack_buf->data_outlen);

    hnd->cfg_tlvs_pt = tlvs_cfg;
    return hnd;
}

//*****************************************************************************
fw_par_hnd_t *
fw_param_get_hnd_def(fw_par_hnd_t *par_hnd, const uint8_t *pack_buf_pt,
                     size_t buf_size) {
    return fw_param_get_hnd(par_hnd, pack_buf_pt, buf_size, fw_par_cfg_tlvs);
}

//*****************************************************************************
int8_t
fw_param_add(fw_par_hnd_t *hnd, fwParamId par_id, uint32_t val) {
    size_t len;

    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return FB_FALSE;
    }

    if (par_id >= eFW_PARAM_LIST_LEN) {
        return FB_FALSE;
    }

    len = hnd->cfg_tlvs_pt[par_id].len;

    if ((hnd->pack_idx + len + sizeof(fw_par_id_size_t) > hnd->pack_buf_size) ||
        (len > sizeof(val))) {
        TGF_ERROR("Error param len=%u > MAX\n", (uint32_t)len);
        return FB_FALSE;
    }

    *hnd->pack_data_pt = (fw_par_id_size_t)par_id;
    hnd->pack_data_pt += sizeof(fw_par_id_size_t);
    hnd->pack_idx += sizeof(fw_par_id_size_t);

    memcpy(hnd->pack_data_pt, (uint8_t *)&val, len);

    hnd->pack_data_pt += len;
    hnd->pack_idx += len;

    return FB_TRUE;
}

/*****************************************************************************/
/** Extracts param from pack identified by handler.
 *  Iterator is stored in the handler.
 *
 * @param [in/out]  *hnd      handler allocated externally
 * @param [out]     *par_id   storage parameter enum id
 * @param [out]     *val      storage for parameter value
 *
 * @return FB_TRUE when parameter extracted successfully from pack else FB_FALSE
 *****************************************************************************/
static int8_t
fw_param_get(fw_par_hnd_t *hnd, fw_par_id_size_t *par_id, uint32_t *val) {
    uint8_t *p = hnd->pack_data_pt;
    size_t i = hnd->pack_idx;
    size_t len;
    fw_par_id_size_t id;

    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return FB_FALSE;
    }

    if (i + sizeof(fw_par_id_size_t) > hnd->pack_data_to_decode_len) {
        return FB_FALSE;
    }
    id = *(fw_par_id_size_t *)p;
    p += sizeof(fw_par_id_size_t);
    i += sizeof(fw_par_id_size_t);

    if (id >= eFW_PARAM_LIST_LEN) {
        TGF_ERROR("Error id=%u > eFW_PARAM_LEN\n", id);
        return FB_FALSE;
    }

    len = fw_par_cfg_tlvs[id].len;

    if (len > hnd->pack_data_to_decode_len ||
        i + len > hnd->pack_data_to_decode_len) {
        TGF_ERROR("Error i+len=%u > dataLen=%u\n", (uint32_t)(i + len),
                  (uint32_t)hnd->pack_data_to_decode_len);
        return FB_FALSE;
    }

    if (par_id) {
        *par_id = id;
    }
    if (val) {
        *val = 0;

        if (len) {
            memcpy(val, p, len);
            // hnd->cfg_tlvs_pt[id].name
            TGF_DEBUG("[%u]=0x%x (d%u)len=%u", id, *val, *val, (uint32_t)len);
        }
    }
    p += len;
    i += len;

    hnd->pack_data_pt = p;
    hnd->pack_idx = i;

    return FB_TRUE;
}

//*****************************************************************************
int8_t
fw_param_get_all(const uint8_t *buf, size_t len, fw_par_val_t *vals,
                 size_t vals_len) {
    fw_par_hnd_t fw_par_hnd;
    fw_par_id_size_t par_id;
    uint32_t val;
    fw_par_hnd_t *hnd;

    if (buf == NULL || len == 0 || vals == NULL || vals_len == 0) {
        TGF_ERROR("Error buf=%p len=%u vals=%p vals_len=%u\n", buf,
                  (uint32_t)len, vals, (uint32_t)vals_len);
        return FB_FALSE;
    }

    hnd = fw_param_get_hnd_def(&fw_par_hnd, buf, len);
    if (hnd == NULL) {
        TGF_ERROR("Error hnd=NULL\n");
        return FB_FALSE;
    }

    while (fw_param_get(hnd, &par_id, &val)) {
        vals[par_id].flag = FB_TRUE;
        vals[par_id].val = val;
    }

    fw_param_free_hnd(hnd);
    return FB_TRUE;
}

//*****************************************************************************
void
fw_param_print(const fw_par_val_t *vals) {
    fwParamId i;

    if (vals == NULL) {
        TGF_ERROR("Error vals=NULL\n");
        return;
    }
    TGF_DEBUG("FW parameters: enum size=%u maxsize=%uU",
              (uint32_t)sizeof(fwParamId), eFW_PARAM_NONE);
    for (i = (fwParamId)0; i < eFW_PARAM_LIST_LEN; i++) {
        if (vals[i].flag) {
            // fw_param_get_str(i)
            TGF_DEBUG("[%d]=%u", i, vals[i].val);
        }
    }
}

//*****************************************************************************
int8_t
fw_par_get_by_id(fwParamId req_par_id, const uint8_t *buf, size_t len,
                 uint32_t *val_out) {
    fw_par_hnd_t fw_par_hnd;
    fw_par_id_size_t par_id;
    uint32_t val;
    int8_t ret = FB_FALSE;
    fw_par_hnd_t *hnd;

    if (buf == NULL || len == 0) {
        TGF_ERROR("Error buf=%p len=%u\n", buf, (uint32_t)len);
        return FB_FALSE;
    }

    hnd = fw_param_get_hnd_def(&fw_par_hnd, buf, len);
    if (hnd == NULL) {
        TGF_ERROR("Error hnd=NULL\n");
        return FB_FALSE;
    }

    while (fw_param_get(hnd, &par_id, &val)) {
        if (par_id == (fw_par_id_size_t)req_par_id) {
            if (val_out) {
                *val_out = val;
                ret = FB_TRUE;
                break;
            }
        }
    }

    fw_param_free_hnd(hnd);
    return ret;
}

//*****************************************************************************
uint8_t *
fw_param_get_pack_pt(const fw_par_hnd_t *hnd) {
    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return NULL;
    }
    return (uint8_t *)hnd->pack_buf_pt;
}

//*****************************************************************************
uint8_t *
fw_param_get_pack(fw_par_hnd_t *hnd, size_t *len) {
    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return NULL;
    }
    if (hnd->pack_idx > hnd->pack_buf_size) {
        TGF_ERROR("Error pack_idx=%u > buf_size\n", (uint32_t)hnd->pack_idx);
        return NULL;
    }

    hnd->pack_buf_pt->data_outlen = hnd->pack_idx;

    // Vendor needs the size aligned to 4B
    hnd->pack_idx = ((hnd->pack_idx + (4 - 1)) / 4) * 4;

    if (len != NULL) {
        *len = hnd->pack_idx;
    }

    return (uint8_t *)hnd->pack_buf_pt;
}

//*****************************************************************************
size_t
fw_param_get_pack_len(fw_par_hnd_t *hnd) {
    uint8_t *data_p;
    size_t data_len = 0;
    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return 0;
    }

    data_p = fw_param_get_pack(hnd, &data_len);

    if (data_p == NULL) {
        return 0;
    }

    return data_len;
}

//*****************************************************************************
int8_t
fw_param_free_hnd(fw_par_hnd_t *hnd) {
    if (hnd == NULL) {
        TGF_ERROR("Error hnd==NULL\n");
        return FB_FALSE;
    }
    return FB_TRUE;
}

//*****************************************************************************
//*****************************************************************************
