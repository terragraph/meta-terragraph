/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * gps_sync.c
 */
#define _XOPEN_SOURCE 500 /* strdup & strptime */
#define _DEFAULT_SOURCE   /* MAP_ANONYMOUS */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>

#include <vlib/vlib.h>
#include <vlib/unix/unix.h>

#define ABS(a) (((a) < 0) ? -(a) : (a))

#include "ptptc.h"

#define NSEC_PER_MSEC 1000000L
#define NSEC_PER_SEC 1000000000L

/* Saturate around +/- 50 ppm fractional frequency offset */
#define ADDEND_MIN 3067800000
#define ADDEND_MAX 3067899999

/*
 * Given a drift of @drift_ns nanoseconds during a 1 second measurement
 * period and an existing clock scaling factor in @addend that results in
 * a 1 GHz nominal clock frequency, compute a new value for addend that
 * will adjust the frequency to correct for that drift.
 *
 * Drift is from perspective of PTP clock (i.e. positive drift means we
 * are faster compared to GPS) so should be negated if timing offset is
 * calculated as (GPS - PTP).
 */
uint32_t get_drift_scale_factor (int64_t drift_ns, uint32_t addend)
{
  int64_t observed_freq = NSEC_PER_SEC + drift_ns;
  int64_t scaled_addend = addend;

  if (!observed_freq)
    return addend;

  /*
   * Actual frequency is 1e9 + drift_ns Hz, while target frequency is 1e9.
   * Scale the existing multiplier accordingly.
   */
  scaled_addend *= NSEC_PER_SEC;
  scaled_addend /= observed_freq;

  return (uint32_t)scaled_addend;
}

/*
 * Parse time out of the $GPRMC NMEA sentence, returning it in
 * gps_time as UNIX time in nanoseconds.
 *
 * TODO: Instead of unix time use GPS time which is unaffected
 * by leap seconds.
 */
static int parse_msg_time (const char *buf, uint64_t *gps_time)
{
  char *tmp, *save_ptr, *token;
  char *timeofday = NULL;
  char *datestr = NULL;
  time_t secs;
  struct tm tm = {0};
  int field = 0;
  int ret;

  char date_field[20];

  if (strncmp (buf, "$GPRMC", 6) != 0)
    return -1;

  tmp = strdup (buf);
  if (!tmp)
    return -1;

  token = strtok_r (tmp, ",", &save_ptr);
  while (field < 9 && token)
    {
      token = strtok_r (NULL, ",", &save_ptr);
      switch (++field)
        {
        case 1:
          timeofday = token;
          break;
        case 9:
          datestr = token;
          break;
        }
    }
  if (!timeofday || !datestr)
    {
      ret = -1;
      goto out;
    }

  strncpy (date_field, datestr, sizeof (date_field));
  strncpy (date_field + strlen (datestr), timeofday,
           sizeof (date_field) - strlen (datestr));
  date_field[sizeof (date_field) - 1] = 0;

  strptime (date_field, "%d%m%y %H%M%S", &tm);
  secs = mktime (&tm);
  *gps_time = ((int64_t)secs) * NSEC_PER_SEC;

  ret = 0;
out:
  free (tmp);
  return ret;
}

/*
 * Called once for every 1PPS after the next NMEA sentence has
 * arrived, check the PTP clock compared to the GPS clock and
 * set the offset and drift registers as necessary to bring PTP
 * clock back into alignment with GPS.
 */
static void handle_pps (struct dpaa2_wriop_reg *regs, int64_t pps_nsecs)
{
  int64_t latched_ptp_time;
  int64_t current_ptp_time;
  int64_t ptp_diff;
  int64_t offset, offset_diff;
  uint32_t pps_addend, new_addend;
  struct timeval tv;

  static bool first_time = true;
  static int64_t last_offset;
  static int64_t synced_offset;

  ptptc_debug ("PPS %ld...\n", pps_nsecs);

  if (first_time)
    {
      /* clear hardware timestamp buffer, process on next PPS */
      latched_ptp_time = dpaa2_get_pps_timestamp (regs);
      first_time = false;
      return;
    }

  latched_ptp_time = dpaa2_get_pps_timestamp (regs);
  if (latched_ptp_time == -1)
    {
      /*
       * Couldn't read ptp time, skip this one.  This could
       * happen if there is no fix, or if we processed an
       * NMEA sentence too late and wound up reading this
       * buffered timestamp in a previous iteration.
       */
      return;
    }

  current_ptp_time = dpaa2_get_current_timestamp_with_offset (regs);

  ptp_diff = current_ptp_time - latched_ptp_time;
  offset = pps_nsecs - latched_ptp_time;
  offset_diff = offset - last_offset;

  gettimeofday (&tv, NULL);

  ptptc_debug ("PPS real:            %ld.%09ld\n", tv.tv_sec,
               tv.tv_usec * 1000);
  ptptc_debug ("1588 clock (at PPS): %ld.%09ld\n",
               latched_ptp_time / NSEC_PER_SEC,
               latched_ptp_time % NSEC_PER_SEC);
  ptptc_debug ("1588 clock (now):    %ld.%09ld\n",
               current_ptp_time / NSEC_PER_SEC,
               current_ptp_time % NSEC_PER_SEC);
  ptptc_debug ("Report to wakeup:    %ld.%09ld\n", ptp_diff / NSEC_PER_SEC,
               ptp_diff % NSEC_PER_SEC);
  ptptc_debug ("GPS to PTP offset:   %ld.%09ld (+%09ld ns)\n",
               offset / NSEC_PER_SEC, offset % NSEC_PER_SEC, offset_diff);

  static int64_t offset_adjust_ns = 0;

  pps_addend = dpaa2_get_pps_addend (regs);
  ptptc_debug ("pps: gps:%llu pps:%llu offset:%lld offset_diff:%lld "
               "offset_adjust_ns:%lld pps_addend:%u synced_offset:%lld\n",
               pps_nsecs, latched_ptp_time, offset, offset_diff,
               offset_adjust_ns, pps_addend, synced_offset);

  /* Ignore >100 ppm drift (likely caused by missed PPS) */
  if (ABS (offset_diff) > 100000)
    {
      last_offset = offset;
      offset_adjust_ns = 0;
      return;
    }

  /* Set filter constants based on locked state */
  int64_t alpha_scale, alpha_freq, alpha_offset, offset_prohibit_ns;
  if (ABS (offset) > 1000 || ABS (offset_adjust_ns) > 1000)
    {
      /* Not locked: use single sample for frequency and phase offset
         correction GPS time in ns requires 61 bits, so use alpha_scale 1 to
         avoid overflow */
      alpha_scale = 1;
      alpha_freq = 1 * alpha_scale;
      alpha_offset = 1 * alpha_scale;
      offset_prohibit_ns = 900e6; /* 0.9 s */
    }
  else
    {
      /* Locked: apply filtering */
      alpha_scale = 256;
      alpha_freq = 26;            /* round(0.1*256) */
      alpha_offset = 77;          /* round(0.3*256) */
      offset_prohibit_ns = 900e6; /* 0.9 s */
    }

  /* Remove previous offset correction from drift calculation */
  int64_t offset_change_ppb = offset_diff + offset_adjust_ns;
  int64_t ffo_adjust_ppb = -1 * alpha_freq * offset_change_ppb / alpha_scale;
  pps_addend = dpaa2_get_pps_addend (regs);
  new_addend = get_drift_scale_factor (ffo_adjust_ppb, pps_addend);
  new_addend = (new_addend < ADDEND_MIN) ? ADDEND_MIN : new_addend;
  new_addend = (new_addend > ADDEND_MAX) ? ADDEND_MAX : new_addend;
  dpaa2_set_pps_addend (regs, new_addend);
  ptptc_debug ("pps_freq: offset_change_ppb:%lld ffo_adjust_ppb:%lld "
               "old_addend:%u new_addend:%u\n",
               offset_change_ppb, ffo_adjust_ppb, pps_addend, new_addend);

  /* Apply phase offset correction */
  static int64_t last_offset_adjust;
  if (pps_nsecs - last_offset_adjust > offset_prohibit_ns)
    {
      offset_adjust_ns = alpha_offset * offset / alpha_scale;
      dpaa2_set_pps_offset (regs, synced_offset + offset_adjust_ns);
      last_offset_adjust = pps_nsecs;
    }
  else
    {
      offset_adjust_ns = 0;
    }
  ptptc_debug ("pps_offset: offset_adjust_ns:%lld old_offset:%lld "
               "new_offset:%lld last_offset_adjust:%lld\n",
               offset_adjust_ns, synced_offset,
               synced_offset + offset_adjust_ns, last_offset_adjust);
  synced_offset += offset_adjust_ns;

  /* Update state */
  last_offset = offset;
}

static int _gps_report_ready (struct gps_main *gm)
{
  struct dpaa2_wriop_reg *regs = gps_main_to_pmp (gm)->wriop_regs;
  uint64_t gps_time;
  char *token, *save_ptr, *part = NULL;
  ssize_t sz;
  int sockfd;
  int off;
  char *buf;

  sockfd = gm->sockfd;
  off = gm->offset;
  gm->offset = 0;

  sz = recv (sockfd, gm->read_buf + off, sizeof (gm->read_buf) - off - 1, 0);
  if (sz < 0)
    return errno;

  sz += off;
  buf = gm->read_buf;
  buf[sz] = 0;

  /* partial strings held for next call */
  if (sz && buf[sz - 1] != '\n')
    {
      part = strrchr (buf, '\n');
      if (part)
        *part++ = 0;
    }

  token = strtok_r (buf, "\n", &save_ptr);
  while (token)
    {
      if (parse_msg_time (token, &gps_time) == 0)
        {
          handle_pps (regs, gps_time);
        }
      token = strtok_r (NULL, "\n", &save_ptr);
    }

  if (part)
    {
      memmove (buf, part, strlen (part) + 1);
      gm->offset = strlen (part);
    }
  return 0;
}

static void reset_timer_defaults (struct dpaa2_wriop_reg *regs)
{
  /* no offset from PTP clock */
  dpaa2_set_pps_offset (regs, 0);
  /* 700 MHz input clock -> 500 MHz output clock = 5/7 */
  dpaa2_set_pps_addend (regs, 0xb6db6db7);

  /*Disabling all of 1588 hardware interrupts*/
  /*This is a Hack to stop the kernel from emptying the timestamp buffer*/
  dpaa2_disable_1588_interrupts (regs);
}

static int connect_to_gpsd ()
{
  int sockfd, rc;
  struct sockaddr_in gpsd = {
      .sin_family = AF_INET,
  };
  char gps_enable_cmd[] =
      "?WATCH={\"enable\":true,\"nmea\":true,\"raw\":1};\n";

  gpsd.sin_addr.s_addr = inet_addr ("127.0.0.1");
  gpsd.sin_port = htons (2947);
  sockfd = socket (AF_INET, SOCK_STREAM, 0);

  if ((connect (sockfd, (struct sockaddr *)&gpsd, sizeof (gpsd))) < 0)
    {
      rc = errno;
      fprintf (stderr, "Failed to connect to gpsd (%d)\n", rc);
      return -rc;
    }

  send (sockfd, gps_enable_cmd, sizeof (gps_enable_cmd), 0);
  return sockfd;
}

static clib_error_t *gps_report_ready (clib_file_t *uf)
{
  int ret;

  struct gps_main *gm = (struct gps_main *)uf->private_data;
  ret = _gps_report_ready (gm);
  if (ret)
    return clib_error_return (0, "Could not read from GPSD");

  return NULL;
}

int gps_sync_enable (struct gps_main *gm)
{
  int fd;
  clib_file_t template = {0};

  struct dpaa2_wriop_reg *regs = gps_main_to_pmp (gm)->wriop_regs;

  /* already enabled? */
  if (gm->sockfd)
    return 0;

  reset_timer_defaults (regs);

  fd = connect_to_gpsd ();
  if (fd < 0)
    return fd;

  memset (gm, 0, sizeof (*gm));
  gm->sockfd = fd;

  template.read_function = gps_report_ready;
  template.file_descriptor = gm->sockfd;
  template.private_data = (uword)gm;

  gm->clib_file_index = clib_file_add (&file_main, &template);
  return 0;
}

void gps_sync_disable (struct gps_main *gm)
{
  struct dpaa2_wriop_reg *regs = gps_main_to_pmp (gm)->wriop_regs;
  reset_timer_defaults (regs);

  /* already disabled? */
  if (!gm->sockfd)
    return;

  clib_file_del (&file_main, file_main.file_pool + gm->clib_file_index);
  close (gm->sockfd);
  memset (gm, 0, sizeof (*gm));
}
