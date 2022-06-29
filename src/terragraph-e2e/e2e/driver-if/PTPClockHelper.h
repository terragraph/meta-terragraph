/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <fbzmq/zmq/Zmq.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "DriverIfUtil.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"

namespace facebook {
namespace terragraph {

/**
 * PTP clock utilities.
 *
 * This class syncs a PTP hardware clock to a provided nanosecond timestamp
 * reference by correcting for phase offset and drift.
 *
 * For NXP PTP clocks, this directly reads/writes DPAA2 registers:
 * - Timer Counter Register (TMR_CNT_H, TMR_CNT_L)
 * - Timer Offset Register (TMROFF_H, TMROFF_L)
 * - Timer Drift Compensation Addend Register (TMR_ADD)
 *
 * For VSC 10G "Malibu" PHYs, this uses a custom datagram socket protocol to
 * interface with the chip and synchronize its LTC clock (over the user-space
 * "MESA" API).
 *
 * TODO:
 * Instead of DPAA2 registers, use the Linux PTP driver APIs, which can
 * read/write most of the above values using the following syscalls:
 * - clock_gettime()
 * - clock_adjtime() with modes ADJ_FREQUENCY, ADJ_OFFSET
 *
 * Currently, the DPAA2 PTP driver does not support phase adjustment. We would
 * need to backport the `ptp_clock_info:adjphase()` function and then implement
 * it in dpaa2-ptp.c.
 */
class PTPClockHelper {
 public:
  PTPClockHelper() {}
  ~PTPClockHelper();

  /**
   * Initialize structures when using NXP PTP clock.
   *
   * @param zmqContext the ZMQ context
   * @param pairSockUrl the ZMQ `PAIR` socket URL on which driverIfSock_ binds
   * @param radioState the radio state (see BaseDriverIf for details)
   * @param ptpDevice The PTP device, e.g. "/dev/ptp0"
   */
  void initNxp(
      fbzmq::Context& zmqContext,
      const std::string& pairSockUrl,
      folly::Synchronized<std::vector<RadioState>> *radioState,
      const std::string& ptpDevice);

  /**
   * Initialize structures when using VSC PTP clock.
   *
   * The event loop (`run()` method) does not support polling from VSC.
   *
   * @param ptpVscCtrlSocket the path for the VSC control datagram socket
   * @param ptpPortNo the VSC port on which PTP timestamping is enabled
   * @param syncInEventLoop whether to sync the VSC PTP clock to the NXP clock
   *                        in the event loop (if running), only to be used if
   *                        no other time source is available (NOT THREAD-SAFE!)
   */
  void initVsc(
      const std::string& ptpVscCtrlSocket, int ptpPortNo, bool syncInEventLoop);

  /** Run the PTP event loop. */
  void run(const std::atomic_bool& stop);

  /**
   * Handle a nanosecond timestamp, usually called at every 1PPS pulse.
   *
   * Set the phase offset and drift as necessary to bring the PTP clock into
   * alignment with the reference clock.
   */
  void handleTimestamp(int64_t ts_nsec);

  /** PTP hardware clock control interface. */
  class PTPCtrl {
   public:
    virtual ~PTPCtrl() {}
    virtual uint64_t getCurrentTimestampWithOffset() = 0;
    virtual uint64_t getCurrentTimestamp() = 0;
    virtual uint64_t getPpsTimestamp() = 0;
    virtual uint32_t getPpsAddend() = 0;
    virtual void setPpsAddend(uint32_t value) = 0;
    virtual int64_t getClockRadeAdj() = 0;
    virtual void setClockRadeAdj(int64_t value) = 0;
    virtual uint64_t getPpsOffset() = 0;
    virtual void setPpsOffset(int64_t offset, uint64_t refPpsNs) = 0;
  };

 private:
  /** Supported PTP clock types. */
  enum class PTPClockType { NXP, VSC };

  /** Synchronization state used in handleTimestamp() algorithm. */
  struct SyncState {
    int64_t syncedOffset{0};
    int64_t driftPpbQ8{0};
    int16_t sampleCount{0};
  };

  /**
   * Handle a nanosecond timestamp using the given clock type, controller, and
   * synchronization state.
   */
  void handleTimestampImpl(
      int64_t ts_nsec,
      PTPClockType clockType,
      PTPCtrl& ptpCtrl,
      SyncState& syncState);

  /**
   * Given [a, b] as unsigned 64-bit integers, returns (a - b) as a signed
   * 64-bit integer, or std::nullopt upon underflow.
   */
  std::optional<int64_t> u64_sub(uint64_t a, uint64_t b) const;

  /** Send a message through driverIfSock_. */
  void sendToDriverIf(const thrift::Message& message);

  /** Wrap the given message in a thrift::Message and thrift::DriverMessage. */
  template <class T>
  thrift::Message
  createDriverMessage(
      const std::string& radioMac,
      thrift::MessageType mType,
      const T& obj) const {
    thrift::DriverMessage driverMsg;
    driverMsg.value = fbzmq::util::writeThriftObjStr(obj, serializer_);
    driverMsg.radioMac = radioMac;
    thrift::Message msg;
    msg.mType = mType;
    msg.value = fbzmq::util::writeThriftObjStr(driverMsg, serializer_);
    return msg;
  }

  /** Enable or disable EXTTS on the given PTP clock descriptor. */
  bool togglePtpExtTs(int fd, bool flag) const;

  /**
   * Read the latest nanosecond timestamp on the given PTP clock descriptor.
   *
   * This function is non-blocking. If no data is available, returns zero.
   */
  uint64_t getLatestPpsTimestamp(int fd) const;

  /**
   * Read a nanosecond timestamp on the given PTP clock descriptor.
   *
   * This function will block until data is available. If `read()` failed or
   * `fd` is invalid, returns zero.
   */
  uint64_t readPpsTimestamp(int fd) const;

  /**
   * Given a drift of @drift_nsec nanoseconds during a 1 second measurement
   * period and an existing clock scaling factor in @addend that results in
   * a 1 GHz nominal clock frequency, compute a new value for addend that
   * will adjust the frequency to correct for that drift.
   *
   * Drift is from perspective of PTP clock (i.e. positive drift means we
   * are faster compared to the time reference) so should be negated if timing
   * offset is calculated as (reference - PTP).
   */
  uint32_t getDriftScaleFactor(int64_t drift_nsec, uint32_t addend) const;

  /** DPAA2 WRIOP PTP control implementation. */
  class DPAA2WriopPTPCtrl : public PTPCtrl {
   public:
    DPAA2WriopPTPCtrl();
    ~DPAA2WriopPTPCtrl();

    uint64_t getCurrentTimestampWithOffset();
    uint64_t getCurrentTimestamp();
    uint64_t getPpsTimestamp();  // not implemented
    uint32_t getPpsAddend();
    void setPpsAddend(uint32_t value);
    int64_t getClockRadeAdj();  // not implemented
    void setClockRadeAdj(int64_t value);  // not implemented
    uint64_t getPpsOffset();
    void setPpsOffset(int64_t offset, uint64_t refPpsNs);

   private:
    // Registers
    enum {
      // Registers below are relative to WRIOP_GLOBAL_ADDR
      WRIOP_GLOBAL_ADDR = 0x8b80000,
      WRIOP_GLOBAL_LEN = 0x16000,

      /** Timer Counter Register (high 32 bits) */
      WRIOP_TMR_CNT_H = 0x15098,
      /** Timer Counter Register (low 32 bits) */
      WRIOP_TMR_CNT_L = 0x1509c,
      /** Timer Drift Compensation Addend Register */
      WRIOP_TMR_ADD = 0x150a0,
      /** Timer Offset Register (high 32 bits) */
      WRIOP_TMROFF_H = 0x150b0,
      /** Timer Offset Register (low 32 bits) */
      WRIOP_TMROFF_L = 0x150b4,
    };
    struct dpaa2_wriop_reg {
      void *mm;
      size_t mm_len;
    };

    /**
     * Allocate and return a memory-mapped region, or return NULL upon failure.
     */
    struct dpaa2_wriop_reg* dpaa2_wriop_reg_init(size_t phys_addr, size_t len);

    /** Unmap and free a memory-mapped region. */
    void dpaa2_wriop_reg_uninit(struct dpaa2_wriop_reg *regs);

    /** Read a register. */
    uint32_t dpaa2_wriop_reg_read(struct dpaa2_wriop_reg *regs, uint32_t addr);

    /** Write a register. */
    void dpaa2_wriop_reg_write(
        struct dpaa2_wriop_reg *regs, uint32_t addr, uint32_t val);

    /** Memory-mapped DPAA2 1588 timer module registers. */
    struct dpaa2_wriop_reg *regs_{nullptr};
  };

  /** VSC 10G PHY custom datagram socket PTP control implementation. */
  class VscPTPCtrl : public PTPCtrl {
   public:
    VscPTPCtrl(const std::string& ptpVscCtrlSocket, int portNo);
    ~VscPTPCtrl();

    uint64_t getCurrentTimestampWithOffset();  // not implemented
    uint64_t getCurrentTimestamp();  // not implemented
    uint64_t getPpsTimestamp();
    uint32_t getPpsAddend();  // not implemented
    void setPpsAddend(uint32_t value);  // not implemented
    int64_t getClockRadeAdj();
    void setClockRadeAdj(int64_t value);
    uint64_t getPpsOffset();  // not implemented
    void setPpsOffset(int64_t offset, uint64_t refPpsNs);

   private:
    /**
     * Send a message and receive a response, or returns std::nullopt on error.
     */
    std::optional<std::string> sendMessage(const std::string& msg);

    /**
     * Send a control request.
     *
     * Returns a vector of return value(s) upon success, or empty upon error.
     */
    std::vector<std::string> ctrlRequest(
        const std::string& method, const std::vector<std::string>& args = {});

    /** The path for the VSC control datagram socket. */
    std::string ptpVscCtrlSocket_;

    /** The VSC port number. */
    int portNo_;

    /** The datagram socket file descriptor. */
    int sockfd_{-1};
  };

  /** The underlying PTP clock type. */
  PTPClockType clockType_;

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};

  /** The ZMQ socket to talk to driver-if. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_CLIENT> driverIfSock_;

  /** Radio up/down state (updated by BaseDriverIf). */
  folly::Synchronized<std::vector<RadioState>> *radioState_;

  /** Per-radio counters recording the number of timestamps sent to firmware. */
  std::unordered_map<std::string /* radioMac */, int64_t> radioTsCounters_;

  /** Is the PTP event loop running (e.g. via `run()`)? */
  std::atomic_bool ptpEventLoopRunning_{false};

  /** Whether to sync the VSC PTP clock to the NXP clock in the event loop. */
  bool vscSyncInEventLoop_{false};

  /** The PTP hardware clock controller instance for NXP. */
  std::unique_ptr<PTPCtrl> ptpCtrlNxp_{nullptr};

  /** The PTP hardware clock controller instance for VSC. */
  std::unique_ptr<PTPCtrl> ptpCtrlVsc_{nullptr};

  /** Synchronization state for NXP. */
  SyncState syncStateNxp_{};

  /** Synchronization state for VSC. */
  SyncState syncStateVsc_{};

  /** The PTP clock descriptor. */
  int ptpFd_{-1};

  /**
   * The last EXTTS nanosecond timestamp read in the `run()` loop.
   *
   * This value is consumed and reset within handleTimestamp().
   */
  std::atomic<uint64_t> lastExtTsNsec_{0};
};

} // namespace terragraph
} // namespace facebook
