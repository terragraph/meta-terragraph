/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "PTPClockHelper.h"

#include <endian.h>
#include <fcntl.h>
#include <linux/ptp_clock.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/String.h>
#include <glog/logging.h>

namespace {
// Nanoseconds per second
const int64_t kNsecPerSec{1000000000L};

// Saturate at 50 ppm frequency offset
const int32_t kMaxDriftQ8{50000 << 8};
const int32_t kMinDriftQ8{-1 * (50000 << 8)};
const int32_t kOffsetResetNs{50000};

// Addend for 0 ppb offset: round(2^32*5/7)
const uint32_t kAddendBase{3067833783};

// Constants for PPS sync using PI control loop
const int64_t kPpsKpToQ8 = 171; // round(0.667*2^8)
const int64_t kPpsKiToQ8 = 57; // round(0.222*2^8)

// "index" value in PTP_EXTTS_REQUEST (0=DPRTC_EVENT_ETS1, 1=DPRTC_EVENT_ETS2)
const int kExtTsIndex{0};
} // namespace

namespace facebook {
namespace terragraph {

PTPClockHelper::~PTPClockHelper() {
  // Disable EXTTS events
  if (ptpFd_ >= 0) {
    VLOG(4) << "Disabling EXTTS events";
    togglePtpExtTs(ptpFd_, false);
    close(ptpFd_);
    ptpFd_ = 0;
  }
}

void
PTPClockHelper::initNxp(
    fbzmq::Context& zmqContext,
    const std::string& pairSockUrl,
    folly::Synchronized<std::vector<RadioState>> *radioState,
    const std::string& ptpDevice) {
  radioState_ = radioState;

  // Connect to ZMQ socket
  driverIfSock_ = fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_CLIENT>(zmqContext);
  LOG(INFO) << "Connecting to '" << pairSockUrl << "'";
  if (driverIfSock_.connect(fbzmq::SocketUrl{pairSockUrl}).hasError()) {
    LOG(FATAL) << "Error binding to '" << pairSockUrl << "'";
  }

  // Initialize DPAA2WriopPTPCtrl
  ptpCtrlNxp_ = std::make_unique<DPAA2WriopPTPCtrl>();

  // Open PTP clock fd
  ptpFd_ = open(ptpDevice.c_str(), O_RDWR);
  if (ptpFd_ < 0) {
    LOG(ERROR) << "Failed to open " << ptpDevice << ": "
               << folly::errnoStr(errno);
  } else {
    // Enable EXTTS events
    VLOG(4) << "Enabling EXTTS events on " << ptpDevice;
    togglePtpExtTs(ptpFd_, true);
  }
}

void
PTPClockHelper::initVsc(
    const std::string& ptpVscCtrlSocket, int ptpPortNo, bool syncInEventLoop) {
  vscSyncInEventLoop_ = syncInEventLoop;

  // Initialize VscPTPCtrl
  ptpCtrlVsc_ = std::make_unique<VscPTPCtrl>(ptpVscCtrlSocket, ptpPortNo);
}

void
PTPClockHelper::run(const std::atomic_bool& stop) {
  ptpEventLoopRunning_ = true;

  // Drain all buffered timestamps
  getLatestPpsTimestamp(ptpFd_);

  // Read 1PPS timestamps
  while (!stop) {
    if (ptpFd_ < 0) {
      LOG(ERROR) << "Bad file descriptor for PTP hardware clock";
      break;
    }

    // Read timestamp
    uint64_t tsNsec = readPpsTimestamp(ptpFd_);
    lastExtTsNsec_ = tsNsec;
    if (tsNsec == 0) {
      continue;  // invalid timestamp
    }
    thrift::GpsTimeValue gpsTimeValue;
    gpsTimeValue.unixTimeSecs = tsNsec / kNsecPerSec;
    gpsTimeValue.unixTimeNsecs = tsNsec % kNsecPerSec;
    thrift::DrvrStatsList drvrStats;
    drvrStats.gpsTimeUs = tsNsec / 1000;

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
      drvrStats.samples.push_back(createDrvrStatsSample(
          folly::sformat("tgd.ptp.{}.numTsSent", radioState.radioMac),
          radioTsCounters_[radioState.radioMac]));
    }
    lockedRadioState.unlock();  // lockedRadioState -> NULL

    // Sync VSC clock
    if (vscSyncInEventLoop_ && ptpCtrlVsc_) {
      VLOG(4) << "Using NXP timestamp for VSC PTP clock: " << tsNsec;
      handleTimestampImpl(
          tsNsec, PTPClockType::VSC, *ptpCtrlVsc_, syncStateVsc_);
    }

    // Send stats to driver-if
    if (!drvrStats.samples.empty()) {
      thrift::Message statsMsg;
      statsMsg.mType = thrift::MessageType::DR_STAT_PUSH;
      statsMsg.value = fbzmq::util::writeThriftObjStr(drvrStats, serializer_);
      sendToDriverIf(statsMsg);
    }
  }
  ptpEventLoopRunning_ = false;
}

void
PTPClockHelper::sendToDriverIf(const thrift::Message& message) {
  const auto ret = driverIfSock_.sendThriftObj(message, serializer_);
  if (ret.hasError()) {
    LOG(ERROR) << "Error routing message to driver-if socket: " << ret.error();
  }
}

void
PTPClockHelper::handleTimestamp(int64_t ts_nsec) {
  if (ts_nsec < 0) {
    LOG(ERROR) << "Dropping negative timestamp: " << ts_nsec;
    return;
  }
  if (ptpCtrlNxp_) {
    VLOG(4) << "Handling timestamp for NXP PTP clock: " << ts_nsec;
    handleTimestampImpl(
        ts_nsec, PTPClockType::NXP, *ptpCtrlNxp_, syncStateNxp_);
  }
  if (ptpCtrlVsc_) {
    VLOG(4) << "Handling timestamp for VSC PTP clock: " << ts_nsec;
    handleTimestampImpl(
        ts_nsec, PTPClockType::VSC, *ptpCtrlVsc_, syncStateVsc_);
  }
}

void
PTPClockHelper::handleTimestampImpl(
    int64_t ts_nsec,
    PTPClockHelper::PTPClockType clockType,
    PTPClockHelper::PTPCtrl& ptpCtrl,
    PTPClockHelper::SyncState& syncState) {
  // Read the latest PTP PPS timestamp
  uint64_t latched_ptp_time = 0;
  if (clockType == PTPClockType::NXP) {
    if (ptpEventLoopRunning_) {
      // run() loop is consuming the EXTTS events and setting lastExtTsNsec_,
      // so read the cached value and atomically reset it to zero.
      // NOTE: Assumes run() loop consumes the event before this is called!
      latched_ptp_time = lastExtTsNsec_.exchange(0);
    } else {
      // Read all buffered EXTTS events
      latched_ptp_time = getLatestPpsTimestamp(ptpFd_);
    }
    if (latched_ptp_time == 0) {
      // Couldn't read PTP time, skip this one
      VLOG(4) << "Could not read latched PTP time";
      // Try to re-enable EXTTS events (in case another process disabled them)
      togglePtpExtTs(ptpFd_, true);
      return;
    }
  } else if (clockType == PTPClockType::VSC) {
    // If we did not read a previously-armed value, the first read will return
    // the old value, then subsequently the latest value (if available), and
    // lastly an error (zero here) when no values are available.
    while (uint64_t ts = ptpCtrl.getPpsTimestamp()) {
      VLOG(4) << "Read VSC timestamp: " << ts;
      latched_ptp_time = ts;
    }
    if (latched_ptp_time == 0) {
      // Couldn't read PTP time, skip this one
      VLOG(4) << "Could not read latched PTP time";
      return;
    }
  }

  // NOTE: current_ptp_time is *not* used in any calculations (logging only)
  uint64_t current_ptp_time = ptpCtrl.getCurrentTimestampWithOffset();
  auto maybePtpDiff = u64_sub(current_ptp_time, latched_ptp_time);
  auto maybeOffset = u64_sub(ts_nsec, latched_ptp_time);
  if (!maybeOffset.has_value()) {
    LOG(ERROR) << "PTP time offset underflowed, skipping timestamp: "
               << ts_nsec;
    return;
  }
  int64_t offset = *maybeOffset;

  if (current_ptp_time && maybePtpDiff.has_value()) {
    VLOG(4) << folly::format(
        "1588 clock now: {}, {} ns since PPS", current_ptp_time, *maybePtpDiff);
  }

  int64_t driftPpbQ8 = 0, offsetAdjust = 0;
  if (abs(offset) > kOffsetResetNs) {
    // Reset state and do timing slam
    syncState.sampleCount = 1;
    driftPpbQ8 = 0;
    offsetAdjust = offset;
  } else if (syncState.sampleCount == 1) {
    // Set initial drift estimate assuming offset removed with first sample
    driftPpbQ8 = offset << 8;
    offsetAdjust = offset;
    syncState.sampleCount++;
  } else {
    // Tracking
    driftPpbQ8 = syncState.driftPpbQ8 + kPpsKiToQ8 * offset;
    offsetAdjust = (abs(kPpsKpToQ8 * offset) + (1 << 7)) >> 8;
    if (offset < 0) {
      offsetAdjust = -1 * offsetAdjust;
    }
  }
  // Clamp drift estimate to within (min, max) drift
  driftPpbQ8 = (driftPpbQ8 < kMinDriftQ8) ? kMinDriftQ8 : driftPpbQ8;
  driftPpbQ8 = (driftPpbQ8 > kMaxDriftQ8) ? kMaxDriftQ8 : driftPpbQ8;

  // Compute as: kAddendBase + round(kAddendBase * driftPpbQ8 / 1e9 / 2^8)
  int64_t scale = kNsecPerSec * (1 << 8);
  int64_t addendOffset = (kAddendBase * abs(driftPpbQ8) + scale / 2) / scale;
  uint32_t newAddend = (driftPpbQ8 > 0) ? kAddendBase + addendOffset
                                        : kAddendBase - addendOffset;

  // Apply frequency adjust
  if (clockType == PTPClockType::NXP) {
    ptpCtrl.setPpsAddend(newAddend);
  } else if (clockType == PTPClockType::VSC) {
    ptpCtrl.setClockRadeAdj(driftPpbQ8 << 8);
  }
  syncState.driftPpbQ8 = driftPpbQ8;

  // Apply phase adjust
  syncState.syncedOffset += offsetAdjust;
  if (clockType == PTPClockType::NXP) {
    ptpCtrl.setPpsOffset(syncState.syncedOffset, latched_ptp_time);
  } else if (clockType == PTPClockType::VSC) {
    ptpCtrl.setPpsOffset(offsetAdjust, latched_ptp_time);
  }

  VLOG(4) << folly::format(
      "timestamp: ref={}, pps={}, offset={}, count={}, offsetAdjust={}, "
      "driftPpbQ8={}, newAddend={}",
      ts_nsec,
      latched_ptp_time,
      offset,
      syncState.sampleCount,
      offsetAdjust,
      driftPpbQ8,
      newAddend);
}

bool
PTPClockHelper::togglePtpExtTs(int fd, bool flag) const {
  if (fd < 0) {
    return false;
  }

  struct ptp_extts_request extts_request;
  memset(&extts_request, 0, sizeof(extts_request));
  extts_request.index = kExtTsIndex;
  extts_request.flags = flag ? PTP_ENABLE_FEATURE : 0;
  if (ioctl(fd, PTP_EXTTS_REQUEST, &extts_request)) {
    LOG(ERROR) << "PTP_EXTTS_REQUEST failed: " << folly::errnoStr(errno);
    return false;
  }
  return true;
}

uint64_t
PTPClockHelper::getLatestPpsTimestamp(int fd) const {
  uint64_t ts = 0;
  if (fd < 0) {
    return ts;
  }

  // Break out of the loop when no more data is available
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(fd, &fds);
  struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

  // Keep reading timestamps out of the queue until it is empty
  // (we expect the last value to hold the most recent PPS assert)
  while (true) {
    int retval = select(fd + 1, &fds, NULL, NULL, &tv);
    if (retval == -1) {
      LOG(ERROR) << "select() failed: " << folly::errnoStr(errno);
      break;
    } else if (retval) {
      // Data now available
      uint64_t extts_nsec = readPpsTimestamp(fd);
      if (extts_nsec > ts) {
        ts = extts_nsec;
      }
    } else {
      // No more data
      break;
    }
  }

  return ts;
}

uint64_t
PTPClockHelper::readPpsTimestamp(int fd) const {
  if (fd < 0) {
    return 0;
  }

  struct ptp_extts_event event;
  int n = read(fd, &event, sizeof(event));
  if (n != sizeof(event)) {
    LOG(ERROR) << folly::format(
        "read() returned {} bytes, expected {}", n, sizeof(event));
    return 0;
  }

  // Convert to nanoseconds
  uint64_t ts = (event.t.sec * kNsecPerSec) + event.t.nsec;
  VLOG(5) << "Read EXTTS: " << ts;

  return ts;
}

uint32_t
PTPClockHelper::getDriftScaleFactor(int64_t drift_nsec, uint32_t addend) const {
  int64_t observed_freq = kNsecPerSec + drift_nsec;
  if (!observed_freq) {
    return addend;
  }

  // Actual frequency is 1e9 + drift_nsec Hz, while target frequency is 1e9.
  // Scale the existing multiplier accordingly.
  int64_t scaled_addend = addend;
  scaled_addend *= kNsecPerSec;
  scaled_addend /= observed_freq;

  return (uint32_t)scaled_addend;
}

std::optional<int64_t>
PTPClockHelper::u64_sub(uint64_t a, uint64_t b) const {
  int64_t ret = a - b;
  if ((ret < 0) ^ (a < b)) {
    return std::nullopt;
  }
  return ret;
}

// -----------------------------------------------------------------------------
// DPAA2WriopPTPCtrl
// -----------------------------------------------------------------------------

PTPClockHelper::DPAA2WriopPTPCtrl::DPAA2WriopPTPCtrl() {
  regs_ = dpaa2_wriop_reg_init(
      PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_GLOBAL_ADDR,
      PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_GLOBAL_LEN);
}

PTPClockHelper::DPAA2WriopPTPCtrl::~DPAA2WriopPTPCtrl() {
  dpaa2_wriop_reg_uninit(regs_);
}

struct PTPClockHelper::DPAA2WriopPTPCtrl::dpaa2_wriop_reg*
PTPClockHelper::DPAA2WriopPTPCtrl::dpaa2_wriop_reg_init(
    size_t phys_addr, size_t len) {
  struct dpaa2_wriop_reg *ret;
  int fd;
  void *mm;

  ret = (struct dpaa2_wriop_reg *)malloc(sizeof(*ret));
  if (!ret) {
    return nullptr;
  }

  fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd == -1) {
    goto out_free;
  }

  mm = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys_addr);
  close(fd);

  if (mm == MAP_FAILED) {
    goto out_free;
  }

  ret->mm = mm;
  ret->mm_len = len;
  return ret;

out_free:
  free(ret);
  return nullptr;
}

void
PTPClockHelper::DPAA2WriopPTPCtrl::dpaa2_wriop_reg_uninit(
    struct dpaa2_wriop_reg *regs) {
  if (!regs) {
    return;
  }

  munmap(regs->mm, regs->mm_len);
  free(regs);
}

uint32_t
PTPClockHelper::DPAA2WriopPTPCtrl::dpaa2_wriop_reg_read(
    struct dpaa2_wriop_reg *regs, uint32_t addr) {
  return le32toh(*(volatile uint32_t*)((uint8_t*)regs->mm + addr));
}

void
PTPClockHelper::DPAA2WriopPTPCtrl::dpaa2_wriop_reg_write(
    struct dpaa2_wriop_reg *regs, uint32_t addr, uint32_t val) {
  *((uint32_t*)((uint8_t*)regs->mm + addr)) = htole32(val);
}

uint64_t
PTPClockHelper::DPAA2WriopPTPCtrl::getCurrentTimestampWithOffset() {
  return getCurrentTimestamp() + getPpsOffset();
}

uint64_t
PTPClockHelper::DPAA2WriopPTPCtrl::getCurrentTimestamp() {
  // must read low first to latch the high portion
  uint64_t value = dpaa2_wriop_reg_read(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMR_CNT_L);
  value |= ((uint64_t)dpaa2_wriop_reg_read(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMR_CNT_H)) << 32;
  return value;
}

uint64_t
PTPClockHelper::DPAA2WriopPTPCtrl::getPpsTimestamp() {
  return 0;  // not implemented
}

uint32_t
PTPClockHelper::DPAA2WriopPTPCtrl::getPpsAddend() {
  return dpaa2_wriop_reg_read(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMR_ADD);
}

void
PTPClockHelper::DPAA2WriopPTPCtrl::setPpsAddend(uint32_t value) {
  dpaa2_wriop_reg_write(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMR_ADD, value);
}

int64_t
PTPClockHelper::DPAA2WriopPTPCtrl::getClockRadeAdj() {
  return 0;  // not implemented
}

void
PTPClockHelper::DPAA2WriopPTPCtrl::setClockRadeAdj(int64_t value) {
  (void)value;  // not implemented
}

uint64_t
PTPClockHelper::DPAA2WriopPTPCtrl::getPpsOffset() {
  // must read low first to latch the high portion
  uint64_t value = dpaa2_wriop_reg_read(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMROFF_L);
  value |= ((uint64_t)dpaa2_wriop_reg_read(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMROFF_H)) << 32;
  return value;
}

void
PTPClockHelper::DPAA2WriopPTPCtrl::setPpsOffset(
    int64_t offset, uint64_t refPpsNs) {
  (void)refPpsNs;
  dpaa2_wriop_reg_write(
      regs_, PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMROFF_H, offset >> 32);
  dpaa2_wriop_reg_write(
      regs_,
      PTPClockHelper::DPAA2WriopPTPCtrl::WRIOP_TMROFF_L,
      offset & 0xffffffff);
}

// -----------------------------------------------------------------------------
// VscPTPCtrl
// -----------------------------------------------------------------------------

PTPClockHelper::VscPTPCtrl::VscPTPCtrl(
    const std::string& ptpVscCtrlSocket, int portNo)
  : ptpVscCtrlSocket_(ptpVscCtrlSocket), portNo_{portNo} {
}

PTPClockHelper::VscPTPCtrl::~VscPTPCtrl() {
  if (sockfd_ >= 0) {
    close(sockfd_);
  }
}

std::optional<std::string>
PTPClockHelper::VscPTPCtrl::sendMessage(const std::string& msg) {
  // Create connection (if needed)
  if (sockfd_ < 0) {
    sockfd_ = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (sockfd_ < 0) {
      LOG(ERROR) << "Could not create datagram socket: "
                 << folly::errnoStr(errno);
      return std::nullopt;
    }

    // Client also needs to bind - autobind to abstract address
    struct sockaddr_un claddr;
    memset(&claddr, 0, sizeof(struct sockaddr_un));
    claddr.sun_family = AF_UNIX;
    int ret = bind(sockfd_, (struct sockaddr *) &claddr, sizeof(sa_family_t));
    if (ret < 0) {
      LOG(ERROR) << "bind failed: " << folly::errnoStr(errno);
      close(sockfd_);
      sockfd_ = -1;
      return std::nullopt;
    }
  }

  // Send message
  struct sockaddr_un svaddr;
  memset(&svaddr, 0, sizeof(struct sockaddr_un));
  svaddr.sun_family = AF_UNIX;
  strcpy(svaddr.sun_path, ptpVscCtrlSocket_.c_str());
  int ret = sendto(
      sockfd_,
      msg.c_str(),
      msg.length(),
      0,
      (struct sockaddr*)&svaddr,
      sizeof(struct sockaddr_un));
  if (ret < 0) {
    LOG(ERROR) << "sendto failed: " << folly::errnoStr(errno);
    return std::nullopt;
  }

  // Receive message
  char buf[256];
  int count = recvfrom(sockfd_, buf, sizeof(buf), 0, nullptr, nullptr);
  if (count == -1) {
    LOG(ERROR) << "recvfrom failed: " << folly::errnoStr(errno);
    return std::nullopt;
  }
  return std::string(buf, count);
}

std::vector<std::string>
PTPClockHelper::VscPTPCtrl::ctrlRequest(
    const std::string& method, const std::vector<std::string>& args) {
  std::vector<std::string> resp;

  // Build request string
  std::string req = folly::sformat("{},{}", method, portNo_);
  if (!args.empty()) {
    req = folly::sformat("{},{}", req, folly::join(",", args));
  }

  // Send request
  VLOG(5) << "Sending VSC request: " << req;
  auto maybeOutput = sendMessage(req);
  if (!maybeOutput.has_value() || maybeOutput->empty()) {
    return resp;
  }
  std::string output = folly::trimWhitespace(maybeOutput.value()).toString();

  // Parse response
  std::vector<std::string> outputVec;
  folly::split(",", output, outputVec);
  if (outputVec.empty()) {
    return resp;
  }
  if (outputVec[0] == "~OK") {
    VLOG(5) << "VSC response: " << output;
    resp = outputVec;
  } else {
    VLOG(5) << "VSC error: " << output;
  }

  return resp;
}

uint64_t
PTPClockHelper::VscPTPCtrl::getCurrentTimestampWithOffset() {
  return 0;  // not implemented
}

uint64_t
PTPClockHelper::VscPTPCtrl::getCurrentTimestamp() {
  return 0;  // not implemented
}

uint64_t
PTPClockHelper::VscPTPCtrl::getPpsTimestamp() {
  auto resp = ctrlRequest("ltc_get");
  if (resp.size() == 3) {
    auto sec = folly::tryTo<uint64_t>(resp[1]);
    auto nsec = folly::tryTo<uint32_t>(resp[2]);
    if (sec.hasValue() && nsec.hasValue()) {
      return sec.value() * kNsecPerSec + nsec.value();
    }
  }
  return 0;  // error
}

uint32_t
PTPClockHelper::VscPTPCtrl::getPpsAddend() {
  return 0;  // not implemented
}

void
PTPClockHelper::VscPTPCtrl::setPpsAddend(uint32_t value) {
  (void)value;  // not implemented
}

int64_t
PTPClockHelper::VscPTPCtrl::getClockRadeAdj() {
  auto resp = ctrlRequest("ltc_freq_get");
  if (resp.size() == 2) {
    auto adj = folly::tryTo<int64_t>(resp[1]);
    if (adj.hasValue()) {
      return adj.value();
    }
  }
  return 0;  // error
}

void
PTPClockHelper::VscPTPCtrl::setClockRadeAdj(int64_t value) {
  ctrlRequest("ltc_freq_adj", {std::to_string(value)});
}

uint64_t
PTPClockHelper::VscPTPCtrl::getPpsOffset() {
  return 0;  // not implemented
}

void
PTPClockHelper::VscPTPCtrl::setPpsOffset(int64_t offset, uint64_t refPpsNs) {
  if (offset == 0) {
    // Nothing to do
  } else if (abs(offset) < kNsecPerSec / 2) {
    // Under 1/2 second: use TOD offset
    ctrlRequest("ltc_tod_adj", {std::to_string((int64_t)offset * -1)});
  } else {
    // Large jump: use disruptive "set" operation (suspends timestamping)
    // We need to set the time at the *next* PPS, so add 1 second.
    uint64_t tsNsec = refPpsNs + offset + kNsecPerSec;
    uint64_t sec = tsNsec / kNsecPerSec;
    uint64_t nsec = tsNsec % kNsecPerSec;
    ctrlRequest("ltc_set", {std::to_string(sec), std::to_string(nsec)});
  }
}

} // namespace terragraph
} // namespace facebook
