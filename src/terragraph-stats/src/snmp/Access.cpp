/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/MacAddress.h>
#include <glog/logging.h>

extern "C" {
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
}

#include "Access.h"
#include "StatCache.h"

using facebook::terragraph::RadioStat;
using facebook::terragraph::StatCache;

namespace {
  // max allowed age of node statistics before dropping the data
  const int kStatsMaxAgeSeconds{5};
}

netsnmp_variable_list*
getFirstDataPoint(
    void** loopContext,
    void** dataContext,
    netsnmp_variable_list* putIndexData,
    netsnmp_iterator_info* mydata) {
  // allocate initial loop context
  LoopData* loopctx;
  loopctx = SNMP_MALLOC_TYPEDEF(LoopData);
  if (!loopctx) {
    return NULL;
  }
  loopctx->first = true;
  *loopContext = loopctx;
  return getNextDataPoint(loopContext, dataContext, putIndexData, mydata);
}

netsnmp_variable_list*
getNextDataPoint(
    void** loopContext,
    void** dataContext,
    netsnmp_variable_list* putIndexData,
    netsnmp_iterator_info* mydata) {
  (void)dataContext;
  (void)mydata;
  LoopData* loopctx = (LoopData*)*loopContext;
  if (loopctx == NULL) {
    return NULL;
  }
  // lookup (and copy) radio stats on first call
  if (loopctx->first) {
    auto radioStats = StatCache::getRadioStatsInstance()->rlock();
    // no link data, return
    if (radioStats->empty()) {
      loopContext = NULL;
      return NULL;
    }
    loopctx->data = std::make_unique<std::vector<MibData>>();
    std::time_t curTime = std::time(nullptr);
    for (const auto& localMacMap : *radioStats) {
      for (const auto& remoteMacMap : localMacMap.second) {
        if (remoteMacMap.second.lastUpdated < curTime - kStatsMaxAgeSeconds) {
          VLOG(2) << "Skipping outdated metrics from: "
                  << remoteMacMap.second.ifName;
          continue;
        }
        MibData storeInfo;
        // copy all data on first run
        storeInfo.ifName = remoteMacMap.second.ifName;
        storeInfo.macAddr = localMacMap.first;
        storeInfo.remoteMacAddr = remoteMacMap.first;
        storeInfo.radioStat = remoteMacMap.second;
        loopctx->data->push_back(storeInfo);
      }
    }
    if (loopctx->data->empty()) {
      loopContext = NULL;
      return NULL;
    }
    loopctx->first = false;
    loopctx->index = 1;
  } else {
    loopctx->index++;
  }
  if ((size_t)loopctx->index > loopctx->data->size()) {
    loopContext = NULL;
    return NULL;
  }
  snmp_set_var_value(putIndexData, (void*)&loopctx->index, sizeof(long));
  return putIndexData;
}

void*
convertContext(void* loopContext, netsnmp_iterator_info* iteratorInfo) {
  (void)iteratorInfo;
  LoopData* loopctx = (LoopData*)loopContext;
  // use loop index for data lookup
  MibData* datactx = (MibData*)&(*loopctx->data)[loopctx->index - 1];
  // alloc new here, freed by data_free later
  MibData* data = SNMP_MALLOC_TYPEDEF(MibData);
  if (!datactx) {
    return NULL;
  }
  // copy full struct
  *data = *datactx;
  return (void*)data;
}

void
dataFree(void* data, netsnmp_iterator_info* iteratorInfo) {
  (void)iteratorInfo;
  MibData* dataInfo = (MibData*)data;
  delete dataInfo;
}

void
loopFree(void* loopctx, netsnmp_iterator_info* iteratorInfo) {
  (void)iteratorInfo;
  LoopData* loopContext = (LoopData*)loopctx;
  // free unique ptr
  if (loopContext->data) {
    loopContext->data.reset();
  }
  free(loopContext);
}
