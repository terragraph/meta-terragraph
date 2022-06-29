/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.BWAllocation
namespace lua terragraph_thrift.BWAllocation

typedef i16 LinkId
typedef i16 SlotIdx

enum SlotAttrib {
  UNRSVD_SLOT = 0,
  BF_RSVD_SLOT = 1,
  MGMT_RSVD_SLOT = 2,
}

struct SlotInfo {
  1: LinkId id;
  2: SlotAttrib attrib;
}

struct AirtimeConfig {
  1: i16 txIdeal;       // Ideal TX airtime fraction to link. Unit: 1/100%.
  2: i16 txMin;         // Min TX airtime fraction to link. Unit: 1/100%.
  3: i16 txMax;         // Max TX airtime fraction to link. Unit: 1/100%.
  4: i16 rxIdeal;       // Ideal RX airtime fraction from link. Unit: 1/100%.
  5: i16 rxMin;         // Min RX airtime fraction from link. Unit: 1/100%.
  6: i16 rxMax;         // Max RX airtime fraction from link. Unit: 1/100%.
}

/**
 * @apiDefine LinkAirtime_SUCCESS
 * @apiSuccess (:LinkAirtime) {String} macAddress
 *                            The MAC address of the link
 * @apiSuccess (:LinkAirtime) {Int16} txIdeal
 *                            The ideal TX airtime to the link (in 1/100%)
 * @apiSuccess (:LinkAirtime) {Int16} txMin
 *                            The minimum TX airtime to the link (in 1/100%)
 * @apiSuccess (:LinkAirtime) {Int16} txMax
 *                            The maximum TX airtime to the link (in 1/100%)
 * @apiSuccess (:LinkAirtime) {Int16} rxIdeal
 *                            The ideal RX airtime from the link (in 1/100%)
 * @apiSuccess (:LinkAirtime) {Int16} rxMin
 *                            The minimum RX airtime from the link (in 1/100%)
 * @apiSuccess (:LinkAirtime) {Int16} rxMax
 *                            The maximum RX airtime from the link (in 1/100%)
 */
struct LinkAirtime {
  1: string macAddress; // MAC address of the link
  2: i16 txIdeal;       // Ideal TX airtime fraction to link. Unit: 1/100%.
  3: i16 txMin;         // Min TX airtime fraction to link. Unit: 1/100%.
  4: i16 txMax;         // Max TX airtime fraction to link. Unit: 1/100%.
  5: i16 rxIdeal;       // Ideal RX airtime fraction from link. Unit: 1/100%.
  6: i16 rxMin;         // Min RX airtime fraction from link. Unit: 1/100%.
  7: i16 rxMax;         // Max RX airtime fraction from link. Unit: 1/100%.
}

/**
 * @apiDefine NodeAirtime_SUCCESS
 * @apiSuccess (:NodeAirtime) {Object(LinkAirtime)[]} linkAirtimes
 *             The airtimes for each peer DN and peer CN
 */
struct NodeAirtime {
  1: list<LinkAirtime> linkAirtimes; // for each peer DN and peer CN.
}

/**
 * @apiDefine NetworkAirtime_SUCCESS
 * @apiSuccess {Map(String:Object(NodeAirtime))} nodeAirtimeMap
 *             The network airtime map (node name -> airtimes)
 */
struct NetworkAirtime {
  1: map<string, NodeAirtime> nodeAirtimeMap;  // keyed on node name
}
