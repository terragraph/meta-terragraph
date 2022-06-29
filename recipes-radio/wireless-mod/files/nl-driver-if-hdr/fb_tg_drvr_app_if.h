/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Facebook Terragraph driver - firmware message definitions
 */

#ifndef FB_TG_DRVR_APP_H
#define FB_TG_DRVR_APP_H

struct __attribute__((__packed__)) t_gps_space_veh_info {
  char sat_id;
  char flags;
  char qlty;
  char snr;
  char elev;
};

struct __attribute__((__packed__)) t_gps_space_veh_rsp_data {
  unsigned char hdr[4];
  unsigned int num_space_veh;
  struct t_gps_space_veh_info space_veh_info[];
};

struct __attribute__((__packed__)) t_gps_pos_fix {
    int32_t latitude;
    int32_t longitude;
    int32_t hght_msl;
    int32_t hght_elipsd;
    int32_t num_sat_used;
    int32_t fix_type;
    int32_t ecef_x;
    int32_t ecef_y;
    int32_t ecef_z;
    uint32_t num_pos_observed;
    uint32_t variance_3d;
};

struct __attribute__((__packed__)) t_tim_puls_freq {
    int32_t  gnss_tim_ofset_ns;
    uint32_t gnss_tim_uncert_ns;
    int32_t  int_osc_ofset_ppb;
    uint32_t int_osc_uncert_ppb;
    uint32_t discp_src;
    uint32_t tim_tos_flag;
};

struct __attribute__((__packed__)) t_gps_stat {
    struct t_gps_pos_fix gps_pos_fix;
    struct t_tim_puls_freq tim_pulse_freq;
    struct t_gps_space_veh_rsp_data sat_in_view;   //should be the last
};

struct __attribute__((__packed__)) TgdDrvrStat {
    uint16_t msgType;
    uint32_t gps_time_in_sec;
    union /* implicit */ __attribute__((__packed__)) {
        struct t_gps_stat gps_stat;
    } data;
};

#endif
