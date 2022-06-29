/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "GpsdClient.h"

#include <chrono>
#include <ctime>

#include <folly/MapUtil.h>
#include <gflags/gflags.h>
#include <libgpsmm.h>

#include "e2e/common/TimeUtils.h"

DEFINE_bool(
    send_gps_ts_without_fix,
    false,
    "Forward received GPS timestamps even with no fix (otherwise, drop "
    "messages without a 2D/3D fix)");

namespace {
// gpsd listen address (assume port DEFAULT_GPSD_PORT = 2947)
const std::string kGpsdListenAddr{"localhost"};

// Time to sleep between attempts to connect to gpsd on kGpsdListenAddr
const std::chrono::milliseconds kGpsdConnectRetryTime{2000};

// Time to wait (block) for gpsd messages
// gpsd messages normally arrive once per second, so this should be >100000
const int kGpsdWaitMicroseconds{2000000};  // 2 seconds

// 30 dBHz
const int kSnrThreshold = 30;

// Nanoseconds per second
const int64_t kNsecPerSec{1000000000L};
} // namespace

namespace facebook {
namespace terragraph {

GpsdClient::GpsdClient(
    fbzmq::Context& zmqContext,
    const std::string& pairSockUrl,
    folly::Synchronized<std::vector<RadioState>> *radioState,
    const std::string& gpsModule,
    std::shared_ptr<PTPClockHelper> ptpClockHelper)
    : driverIfSock_{zmqContext},
      radioState_{radioState},
      gpsModule_{gpsModule},
      ptpClockHelper_{ptpClockHelper} {
  LOG(INFO) << "Connecting to '" << pairSockUrl << "'";
  if (driverIfSock_.connect(fbzmq::SocketUrl{pairSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << pairSockUrl << "'";
  }
}

void
GpsdClient::run(const std::atomic_bool& stop) {
  while (!stop) {
    // Connect to gpsd
    gpsmm gps_rec(kGpsdListenAddr.c_str(), DEFAULT_GPSD_PORT);
    if (gps_rec.stream(WATCH_ENABLE | WATCH_JSON | WATCH_PPS) == NULL) {
      VLOG(4) << "No GPSD running, waiting...";
      connected_ = false;
      std::this_thread::sleep_for(kGpsdConnectRetryTime);
      continue;
    }
    VLOG(2) << "Connected to gpsd on " << kGpsdListenAddr << ":"
            << DEFAULT_GPSD_PORT;
    connected_ = true;

    // Listen for data
    while (!stop) {
      if (!gps_rec.waiting(kGpsdWaitMicroseconds)) {
        continue;
      }

      struct gps_data_t *data;
      if ((data = gps_rec.read()) == NULL) {
        // Read error, try connecting again...
        LOG(ERROR) << "gpsd read error, trying to reconnect...";
        break;
      } else {
        preprocessData(data);
        bool tsChanged = processData(data);
        syncPtpClock(data);
        updateStructures(data);
        sendStatsForData(data, tsChanged);
        updateFwPosition(data);
      }
    }
  }
  connected_ = false;
}

bool
GpsdClient::connected() {
  return connected_;
}

thrift::Location
GpsdClient::getLocation() {
  std::lock_guard<std::mutex> lock(lastGpsLocationMutex_);
  return lastGpsLocation_;
}

std::string
GpsdClient::getDevice() {
  std::lock_guard<std::mutex> lock(gpsDeviceMutex_);
  return gpsDevice_;
}

std::time_t
GpsdClient::readTs(struct gps_data_t *data) const {
  // Check if time is included.
  // This could be from either PPS or TPV (e.g. NMEA) messages.
  // - PPS is clearly preferable: read the "real" time if we can.
  // - "fix.time" is significantly delayed compared to PPS "real" time.
  std::time_t ts = 0;
  if (data->set & PPS_SET) {
    // Ignore tv_nsec (nanoseconds), which should be 0.
    ts = data->pps.real.tv_sec;
  } else if (data->set & TIME_SET) {
    // Ignore tv_nsec (nanoseconds), which should be 0.
    ts = data->fix.time.tv_sec;
  }
  return ts;
}

void
GpsdClient::preprocessData(struct gps_data_t *data) const {
  if (gpsModule_ == "Telit_SL869_T") {
    gps_mask_t locationFlags = LATLON_SET | ALTITUDE_SET;
    if ((data->set & locationFlags) == locationFlags) {
      // In $GPGGA, Telit provides altitude as height above the WGS84 ellipsoid
      // instead of the NMEA-recommended MSL (mean sea level) height.
      // gpsd assumes MSL, so we need to swap the fields and re-compute the
      // WGS84 height using the geoid separation (from the same NMEA message).
      double altHAE = data->fix.altMSL;
      data->fix.altMSL -= data->fix.geoid_sep;
      data->fix.altHAE = altHAE;
    }
  }
}

bool
GpsdClient::processData(struct gps_data_t *data) {
  // Check GPS fix
  if (!(data->set & MODE_SET)) {
    VLOG(4) << "No mode set";
    return false;
  }
  if (data->fix.mode < MODE_2D) {
    if (FLAGS_send_gps_ts_without_fix) {
      VLOG(4) << "WARNING: Bad fix (mode=" << data->fix.mode
              << ") but proceeding anyway...";
    } else {
      VLOG(4) << "Waiting for GPS fix";
      return false;
    }
  }

  // Read time
  std::time_t ts = readTs(data);
  if (ts == 0) {
    VLOG(4) << "No time received from gpsd";
    return false;
  }

  // We may get multiple different messages for the same pulse.
  // Don't send a duplicate timestamp to firmware.
  if (ts <= lastGpsTs_) {
    VLOG(5) << "Dropping duplicate time from gpsd (" << ts << ")";
    return false;
  }
  VLOG(4) << "Received new time from gpsd: " << ts;
  lastGpsTs_ = ts;
  lastGpsAggrMask_ = 0;

  // Convert to GPS time (expected by our firmware)
  uint64_t gpsTime = TimeUtils::unixTimeToGpsTime(ts);
  VLOG(4) << "Converting to GPS time: " << gpsTime;
  thrift::GpsTimeValue gpsTimeValue;
  gpsTimeValue.unixTimeSecs /* misleading name for GPS time */ = gpsTime;
  gpsTimeValue.unixTimeNsecs = 0;

  // Send time to driver-if
  auto lockedRadioState = radioState_->rlock();
  for (const auto& radioState : *lockedRadioState) {
    if (radioState.isUp) {
      sendToDriverIf(createDriverMessage(
          radioState.radioMac,
          thrift::MessageType::GPS_SEND_TIME,
          gpsTimeValue));
      radioTsCounters_[radioState.radioMac]++;
    } else {
      VLOG(4) << "Not forwarding timestamp to offline radio: "
              << radioState.radioMac;
    }
  }

  return true;
}

void
GpsdClient::syncPtpClock(struct gps_data_t *data) {
  if (!ptpClockHelper_) {
    return;  // not enabled
  }

  // NOTE: Use the TPV event, not the (earlier) PPS event, to avoid race
  // conditions where the PTP events/registers are not ready by the time gpsd
  // has already received the PPS event.
  if (data->set & TIME_SET) {
    std::time_t ts = data->fix.time.tv_sec;
    if (ts > lastPtpGpsTs_) {
      int64_t gpsTsNsec = TimeUtils::unixTimeToGpsTime(ts) * kNsecPerSec;
      ptpClockHelper_->handleTimestamp(gpsTsNsec);
      lastPtpGpsTs_ = ts;
    }
  }
}

void
GpsdClient::updateStructures(struct gps_data_t *data) {
  // Update latest location (if fields are set)
  gps_mask_t locationFlags = LATLON_SET | ALTITUDE_SET | HERR_SET | VERR_SET;
  if ((data->set & locationFlags) == locationFlags) {
    thrift::Location l;
    l.latitude = data->fix.latitude;
    l.longitude = data->fix.longitude;
    l.altitude = data->fix.altHAE;
    // Use SEP (spherical error probable) 3-D accuracy reported by gpsd.
    // Computed internally (in libgpsd_core.c) as:
    //   pdop * p_uere [19.0 without DGPS or 4.75 with DGPS, in meters]
    l.accuracy = data->fix.sep;
    std::lock_guard<std::mutex> lock(lastGpsLocationMutex_);
    lastGpsLocation_ = std::move(l);
  }

  // Update GPS device (if needed)
  if (data->set & MODE_SET && !(data->set & PPS_SET)) {
    std::string device(data->dev.path);
    if (!device.empty() && device != gpsDevice_) {
      std::lock_guard<std::mutex> lock(gpsDeviceMutex_);
      gpsDevice_ = std::move(device);
    }
  }
}

void
GpsdClient::sendStatsForData(struct gps_data_t *data, bool tsChanged) {
  // Get new stats only for this GPS timestamp
  // (we may get multiple messages for the same ts, and want to de-dupe fields)
  thrift::DrvrStatsList drvrStats = getStats(data, lastGpsAggrMask_);
  lastGpsAggrMask_ |= data->set;

  // Add custom stats
  if (tsChanged || !(data->set & MODE_SET) || data->fix.mode < MODE_2D) {
    auto lockedRadioState = radioState_->rlock();
    for (const auto& radioState : *lockedRadioState) {
        thrift::DrvrStatsSample drvrStatsSample;
        drvrStatsSample.key =
            folly::sformat("tgd.gpsStat.{}.numTsSent", radioState.radioMac);
        drvrStatsSample.value = radioTsCounters_[radioState.radioMac];
        drvrStats.samples.push_back(drvrStatsSample);
    }
  }

  // No (new) stats?
  if (drvrStats.samples.empty()) {
    return;
  }

  // Send stats to driver-if
  thrift::Message statsMsg;
  statsMsg.mType = thrift::MessageType::DR_STAT_PUSH;
  statsMsg.value = fbzmq::util::writeThriftObjStr(drvrStats, serializer_);
  sendToDriverIf(statsMsg);
}

void
GpsdClient::updateFwPosition(struct gps_data_t *data) {
  // Quit if we don't have GPS fix
  if (!(data->set & MODE_SET) || data->fix.mode < MODE_2D) {
    return;
  }

  // First check if we actually require a position update, then update if needed
  std::vector<int> idxVec;
  auto lockedRadioState = radioState_->ulock();
  for (int i = 0, n = lockedRadioState->size(); i < n; i++) {
    const auto& radioState = lockedRadioState->at(i);
    if (radioState.isUp && !radioState.positionSent) {
      VLOG(2) << "Sending position to radio: " << radioState.radioMac;
      idxVec.push_back(i);
    }
  }
  if (!idxVec.empty()) {
    thrift::Location location = getLocation();

    // Upgrade to write-lock
    auto wlock = lockedRadioState.moveFromUpgradeToWrite();
    for (int idx : idxVec) {
      sendToDriverIf(createDriverMessage(
          wlock->at(idx).radioMac,
          thrift::MessageType::GPS_SET_POS_REQ,
          location));
      wlock->at(idx).positionSent = true;
    }
  }
}

thrift::DrvrStatsList
GpsdClient::getStats(struct gps_data_t *data, gps_mask_t ignoreMask) const {
  thrift::DrvrStatsList drvrStats;
  std::time_t ts = readTs(data);
  if (ts > 0) {
    // Some messages after PPS still contain an old timestamp. To avoid
    // confusion, replace the old timestamp with the latest received.
    ts = std::max(ts, lastGpsTs_);
    // convert to GPS time
    drvrStats.gpsTimeUs = (int64_t)TimeUtils::unixTimeToGpsTime(ts) * 1000000;
  } else {
    // default to system time
    drvrStats.gpsTimeUs = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
  }

  std::vector<thrift::DrvrStatsSample>& samps = drvrStats.samples;
  if (data->set & MODE_SET) {  // always set (may differ in messages at same ts)
    // NOTE: values differ from u-blox t_gps_pos_info::fix_type
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.fixType",
        data->fix.mode));
  }
  if (data->set & PPS_SET && !(ignoreMask & PPS_SET)) {
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.unixTs", data->pps.real.tv_sec));
  }
  if (data->set & LATLON_SET && !(ignoreMask & LATLON_SET)) {
    samps.push_back(
      createDrvrStatsSample("tgd.gpsStat.latitude",
      (int64_t) (data->fix.latitude * 1e7)));

    samps.push_back(
      createDrvrStatsSample("tgd.gpsStat.longitude",
      (int64_t) (data->fix.longitude * 1e7)));
  }
  if (data->set & ALTITUDE_SET && !(ignoreMask & ALTITUDE_SET)) {
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.heightMsl",
        (int64_t) (data->fix.altMSL * 1000)));

    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.heightEllipsoid",
        (int64_t) (data->fix.altHAE * 1000)));
  }
  if (data->set & HERR_SET && !(ignoreMask & HERR_SET)) {
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.epx",
        (int64_t) (data->fix.epx * 1000)));

    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.epy",
        (int64_t) (data->fix.epy * 1000)));
  }
  if (data->set & VERR_SET && !(ignoreMask & VERR_SET)) {
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.epv",
        (int64_t) (data->fix.epv * 1000)));
  }
  if (data->set & SATELLITE_SET && !(ignoreMask & SATELLITE_SET)) {
    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.fixNumSat",
        data->satellites_used));

    samps.push_back(
        createDrvrStatsSample("tgd.gpsStat.visibleNumSat",
        data->satellites_visible));

    int numSatGoodSnr = 0;
    for (int i = 0; i < data->satellites_visible; i++) {
      short PRN = data->skyview[i].PRN;

      samps.push_back(createDrvrStatsSample(
          folly::sformat("tgd.gpsStat.{}.used", PRN), data->skyview[i].used));

      samps.push_back(createDrvrStatsSample(
          folly::sformat("tgd.gpsStat.{}.snr", PRN),
          (int64_t)data->skyview[i].ss));

      samps.push_back(createDrvrStatsSample(
          folly::sformat("tgd.gpsStat.{}.azimuth", PRN),
          data->skyview[i].azimuth));

      samps.push_back(createDrvrStatsSample(
          folly::sformat("tgd.gpsStat.{}.elevation", PRN),
          data->skyview[i].elevation));
      if (data->skyview[i].ss >= kSnrThreshold) {
        numSatGoodSnr++;
      }
    }
    samps.push_back(createDrvrStatsSample(
        "tgd.gpsStat.numGoodSnr",
        numSatGoodSnr));
  }

  return drvrStats;
}

void
GpsdClient::sendToDriverIf(const thrift::Message& message) {
  const auto ret = driverIfSock_.sendThriftObj(message, serializer_);
  if (ret.hasError()) {
    LOG(ERROR) << "Error routing message to driver-if socket: " << ret.error();
  }
}

} // namespace terragraph
} // namespace facebook
