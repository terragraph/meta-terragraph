/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <atomic>
#include <mutex>
#include <unordered_map>

#include <fbzmq/zmq/Zmq.h>
#include <folly/Synchronized.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "DriverIfUtil.h"
#include "PTPClockHelper.h"
#include "e2e/if/gen-cpp2/Controller_types.h"
#include "e2e/if/gen-cpp2/DriverMessage_types.h"
#include "e2e/if/gen-cpp2/Topology_types.h"

/* Forward declarations to avoid exposing libgpsd headers */
typedef uint64_t gps_mask_t;
struct gps_data_t;

namespace facebook {
namespace terragraph {

/**
 * gpsd client class.
 *
 * The client loop continuously reads from the gpsd socket and forwards relevant
 * information through driver-if over a ZMQ `PAIR` socket.
 */
class GpsdClient {
 public:
  /**
   * Constructor.
   * @param zmqContext the ZMQ context
   * @param pairSockUrl the ZMQ `PAIR` socket URL on which driverIfSock_ binds
   * @param radioState the radio state (see BaseDriverIf for details)
   * @param gpsModule the GPS module (for module-specific functionality)
   * @param ptpClockHelper the PTPClockHelper instance if GPS timestamps should
   *                       be used to synchronize the PTP hardware clock, else
   *                       NULL
   */
  GpsdClient(
      fbzmq::Context& zmqContext,
      const std::string& pairSockUrl,
      folly::Synchronized<std::vector<RadioState>> *radioState,
      const std::string& gpsModule,
      std::shared_ptr<PTPClockHelper> ptpClockHelper);

  /** Run the client loop. */
  void run(const std::atomic_bool& stop);

  /** Return whether we are connected to gpsd. */
  bool connected();

  /**
   * Return the latest GPS location.
   *
   * NOTE: This call acquires a lock on lastGpsLocationMutex_!
   */
  thrift::Location getLocation();

  /**
   * Return the GPS device (if any).
   *
   * NOTE: This call acquires a lock on gpsDeviceMutex_!
   */
  std::string getDevice();

 private:
  /** Pre-process data from gpsd before passing it to other functions. */
  void preprocessData(struct gps_data_t *data) const;

  /**
   * Process data received from gpsd and forward timestamps to driver-if.
   *
   * Returns true if the data represents a new and valid timestamp.
   */
  bool processData(struct gps_data_t *data);

  /** If configured, synchronize the PTP hardware clock using the given data. */
  void syncPtpClock(struct gps_data_t *data);

  /** Update internal structures for the given gpsd data. */
  void updateStructures(struct gps_data_t *data);

  /** Send stats for the given gpsd data to driver-if. */
  void sendStatsForData(struct gps_data_t *data, bool tsChanged);

  /** If needed, send the last GPS position to driver-if. */
  void updateFwPosition(struct gps_data_t *data);

  /** Convert the given gpsd data to a Thrift stats structure. */
  thrift::DrvrStatsList getStats(
      struct gps_data_t *data, gps_mask_t ignoreMask) const;

  /**
   * Read the timestamp (in seconds) from a gpsd message, or return 0 if no time
   * is present.
   *
   * This prefers PPS time to NMEA time.
   */
  std::time_t readTs(struct gps_data_t *data) const;

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

  /** All messages are serialized using this serializer. */
  apache::thrift::CompactSerializer serializer_{};

  /** The ZMQ socket to talk to driver-if. */
  fbzmq::Socket<ZMQ_PAIR, fbzmq::ZMQ_CLIENT> driverIfSock_;

  /** Radio up/down state (updated by BaseDriverIf). */
  folly::Synchronized<std::vector<RadioState>> *radioState_;

  /** The GPS module (for module-specific functionality). */
  std::string gpsModule_;

  /** The PTPClockHelper instance (or NULL if disabled). */
  std::shared_ptr<PTPClockHelper> ptpClockHelper_{nullptr};

  /** Are we connected to gpsd? */
  std::atomic_bool connected_{false};

  /** Last received GPS timestamp. */
  time_t lastGpsTs_{0};

  /** Last GPS timestamp sent to PTPClockHelper instance. */
  time_t lastPtpGpsTs_{0};

  /** Aggregated 'set' mask bits for all messages received during lastGpsTs_. */
  gps_mask_t lastGpsAggrMask_{0};

  /** Last received GPS location. */
  thrift::Location lastGpsLocation_{};

  /** Mutex guarding lastGpsLocation_. */
  std::mutex lastGpsLocationMutex_;

  /** Received GPS device. */
  std::string gpsDevice_;

  /** Mutex guarding gpsDevice_. */
  std::mutex gpsDeviceMutex_;

  /** Per-radio counters recording the number of timestamps sent to firmware. */
  std::unordered_map<std::string /* radioMac */, int64_t> radioTsCounters_;
};

} // namespace terragraph
} // namespace facebook
