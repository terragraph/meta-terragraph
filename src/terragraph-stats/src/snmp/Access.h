/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "StatCache.h"

#include <string>
#include <vector>

/**
 * Struct for all return data for each request.
 */
struct MibData {
  std::string ifName;
  std::string macAddr;
  std::string remoteMacAddr;
  facebook::terragraph::RadioStat radioStat;
};

/**
 * Loop/iterator data used for each request.
 */
struct LoopData {
  bool first;
  long index;
  std::unique_ptr<std::vector<MibData>> data;
};

/**
 * Row-level first access.
 *
 * getNextDataPoint is called immediately after for
 * the same data/request. Initialize the iterator(loop) data structure and
 * set first=true to be passed to the next function.
 */
Netsnmp_First_Data_Point getFirstDataPoint;

/**
 * Row-level access iterator.
 *
 * Used to initialize the data to be returned if called for the first time
 * on a request.
 *
 * Returns NULL if no data in the cache or we've reached the end of our data.
 * Returns a net-snmp 'put' request to tell net-snmp we have more data and
 * increments the index.
 */
Netsnmp_Next_Data_Point getNextDataPoint;

/**
 * Create a new data structure to hold the data we've received from our
 * iterator, which is later returned to the client.
 *
 * We can't directly return the struct we initialized in get_next_data_point
 * as it'll be free'd in loop_free before the data is returned.
 */
Netsnmp_Make_Data_Context convertContext;

/**
 * Called at the end of each request/iteration loop to free resources.
 */
Netsnmp_Free_Loop_Context loopFree;

/**
 * Called after returning the data to the client.
 */
Netsnmp_Free_Data_Context dataFree;
