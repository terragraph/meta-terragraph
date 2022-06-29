/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.DriverMessage
namespace lua terragraph_thrift.DriverMessage

include "Controller.thrift"
include "FwOptParams.thrift"
include "PassThru.thrift"
include "Topology.thrift"

// wrapper structure for driver messages which includes the associated radio MAC
struct DriverMessage {
  1: binary value;
  2: string radioMac;
}

// driver link status type (tgLinkStatus in fb_tgd_fw_common.h)
enum DriverLinkStatusType {
  NONE = 0,
  LINK_INIT = 1,
  LINK_UP = 2,
  LINK_PAUSE = 3,
  LINK_DOWN = 4,
}

// cause of LINK_DOWN (tgLinkFailureCause in fb_tgd_fw_common.h)
enum LinkDownCause {
  NOT_APPLICABLE = 0,
  UNEXPECTED_COMMAND = 1,
  SYSTEM_GPS_SYNC_IN_PROG = 2,
  SYSTEM_NOT_CONFIGURED = 3,
  BF_TRAINING_FAILED = 4,
  BF_TRAINING_SHUTDOWN = 5,
  HB_KA_LOSS_DETECTED = 6,
  LINK_SHUTDOWN_RECVD = 7,
  CHANNEL_NOT_CONFIGURED = 8,
  CHANNEL_NOT_SET_WAITING_LINK_DOWN = 9,
  CHANNEL_IS_BEING_SET = 10,
  UNSUPPORTED_CAPABILITY = 11,
  WSEC_MISMATCH = 12,
  DISASSOC_RCVD_FROM_PEER = 13,
}

// when e2e sends a command south bound, there can be two north bound replies.
// First, driver relays it to firmware and immediately sends back an
// acknowledgement, which we call DriverResp here
// Second, firmware processes the command and sends a message north.
// Driver parses it and sends a message further north, which we call
// XXXNotify/FwAck here.
//

// --- south bound messages ---

// to firmware (through driver)
struct DriverNodeInitReq {
  1: FwOptParams.FwOptParams optParams;
}

struct DriverSetLinkStatus {
  // is assoc or dissoc
  1: bool isAssoc;
  2: string responderMac;
  3: FwOptParams.FwOptParams optParams;
}

struct FwSetNodeParams {
  1: list<PassThru.PassThruMsg> passThruMsgs;
  2: optional Topology.Location location;
}

struct FwConfigParams {
    1: list<PassThru.PassThruMsg> passThruMsgs;
}

struct FwConfigResp {
    // only one of the fields below will be used
    1: PassThru.GetFwParamsResp getFwConfigResp;
    2: PassThru.SetFwParamsResp setFwConfigResp;
}

struct PhyGolaySequenceConfigReq {
  1: list<list<i64>> transmitSequence; // Transmit Sequence
  2: list<i32> receiveSequence; // Receive Sequence
}
// --- north bound messages ---

// from driver
// driver response for all types of southbound messages destined for fw
struct DriverResp {
  1: Controller.MessageType reqType;
  2: bool success;
}

// driver notification to allow insecure connection on an interface
struct DriverWsecStatus {
  1: string ifname;
  2: bool status;  // wsec state (should always be false)
}

// driver notification that the secure port is authorized
struct DriverWsecLinkupStatus {
  1: string ifname;
}

// driver notification that the backhaul baseband device went up or down
// (the baseband MAC address is contained in DriverMessage.radioMac)
struct DriverDevUpDownStatus {
  1: bool status;  // 'true' for up, 'false' for down
}

// for messages terminated at driver
struct DriverAck {
  1: Controller.MessageType reqType;
  2: bool success;
}

// from fw (through driver)
struct FwAck {
  1: Controller.MessageType reqType;
  2: bool success;
}

struct DriverNodeInitNotif {
  1: bool success;
  2: string macAddr;
  3: string vendor;
}

struct DriverLinkStatus {
  1: bool valid;  // is message format expected, does not mean link is up
  2: string macAddr;
  3: DriverLinkStatusType drLinkStatusType;
  4: LinkDownCause linkDownCause;
  5: string ifname;  // set in north bound messages, not used in south bound
  6: Topology.NodeType selfNodeType;
  7: Topology.NodeType peerNodeType;
}

// stats
struct DriverFwStatsReport {
  1: PassThru.Stats stats;
}

//------------------------- For Driver NB stats push ----
struct DrvrStatsSample {
  1: string key;
  2: i64 value;
}

//Ublox SNR values for all satellite
struct DrvrStatsList {
  1: i64 gpsTimeUs;
  2: list<DrvrStatsSample> samples;
}

struct DriverDevAllocReq {
   1: string macAddr;
}

// for messages terminated at driver
struct DriverDevAllocRes {
  1: bool success;
  2: string macAddr;
  4: i32 ifindex;
  3: string ifname;
}

//------------------------- For X86 emulation ----
// messages between mock drivers
struct X86PeerDriverMsg {
  // is assoc or dissoc
  1: bool isAssoc;
}
