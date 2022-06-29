/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <glog/logging.h>
#include "StatCache.h"

extern "C" {
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>
}

#include "Access.h"
#include "Agent.h"

using facebook::terragraph::SnmpColumn;

namespace {
  // base OID to use for registration with net-snmp
  // matches SNMPv2-SMI::enterprises.15000::tgRadioMIB::interfaces
  //  ::tgRadioInterfacesTable
  // 15000 is randomly chosen not to conflict we existing MIB OIDs, but is not
  // registered
  const oid kBaseOid[] = {1, 3, 6, 1, 4, 1, 15000, 1, 1, 1};
}

void
initAgent() {
  netsnmp_table_registration_info* tableInfo;
  netsnmp_handler_registration* handlerRegistration;
  netsnmp_iterator_info* iteratorInfo;

  // create the table registration information structures
  tableInfo = SNMP_MALLOC_TYPEDEF(netsnmp_table_registration_info);
  iteratorInfo = SNMP_MALLOC_TYPEDEF(netsnmp_iterator_info);

  handlerRegistration = netsnmp_create_handler_registration(
      "tgRadioInterfacesTable",
      requestHandler,
      kBaseOid,
      OID_LENGTH(kBaseOid),
      HANDLER_CAN_RONLY);

  if (!handlerRegistration || !tableInfo || !iteratorInfo) {
    snmp_log(LOG_ERR, "malloc failed in initAgent");
    return;
  }

  netsnmp_table_helper_add_indexes(
      tableInfo, ASN_INTEGER /* ifIndex used as index */, 0);

  // minimum and maximum accessible columns
  // the index is 1, so the first real column is 2
  // the max column must be the highest enum value in SnmpColumn
  tableInfo->min_column = 2;
  tableInfo->max_column = 7;

  // iterator access routines
  iteratorInfo->get_first_data_point = getFirstDataPoint;
  iteratorInfo->get_next_data_point = getNextDataPoint;

  iteratorInfo->make_data_context = convertContext;
  iteratorInfo->free_data_context = dataFree;

  // free data at the end of each 'loop' which is a single snmp request
  iteratorInfo->free_loop_context_at_end = loopFree;

  iteratorInfo->table_reginfo = tableInfo;

  // register the table with the master net-snmp agent
  netsnmp_register_table_iterator2(handlerRegistration, iteratorInfo);
}

void
setStringValue(netsnmp_variable_list* var, const std::string& value) {
  snmp_set_var_typed_value(var, ASN_OCTET_STR, (char*)value.c_str(),
    value.size());
}

void
setLongValue(netsnmp_variable_list* var, const long& value) {
  snmp_set_var_typed_value(var, ASN_INTEGER, &value, sizeof(long));
}

void
setULongValue(netsnmp_variable_list* var, const u_long& value) {
  snmp_set_var_typed_value(var, ASN_GAUGE, &value, sizeof(u_long));
}

int
requestHandler(
    netsnmp_mib_handler* mibHandler,
    netsnmp_handler_registration* handlerRegistration,
    netsnmp_agent_request_info* agentRequestInfo,
    netsnmp_request_info* requests) {
  (void)mibHandler;
  (void)handlerRegistration;

  netsnmp_request_info* requestInfo;
  netsnmp_table_request_info* tableInfo;
  netsnmp_variable_list* var;

  void* dataContext = NULL;

  for (requestInfo = requests; requestInfo; requestInfo = requestInfo->next) {
    var = requestInfo->requestvb;
    if (requestInfo->processed != 0) {
      continue;
    }

    switch (agentRequestInfo->mode) {
      case MODE_GET: // 160
        dataContext = netsnmp_extract_iterator_context(requestInfo);
        if (dataContext == NULL) {
          netsnmp_set_request_error(
              agentRequestInfo, requestInfo, SNMP_NOSUCHINSTANCE);
          continue;
        }
        break;
      default:
        LOG(ERROR) << "Unmatched request mode: " << agentRequestInfo->mode;
    }

    tableInfo = netsnmp_extract_table_info(requestInfo);

    if (tableInfo == NULL) {
      continue;
    }

    MibData* dataInfo = (MibData*)dataContext;
    switch (agentRequestInfo->mode) {
      case MODE_GET:
        switch (tableInfo->colnum) {
          case SnmpColumn::IF_NAME:
            setStringValue(var, dataInfo->ifName);
            break;

          case SnmpColumn::MAC_ADDR:
            setStringValue(var, dataInfo->macAddr);
            break;

          case SnmpColumn::REMOTE_MAC_ADDR:
            setStringValue(var, dataInfo->remoteMacAddr);
            break;

          case SnmpColumn::MCS:
            setULongValue(var, dataInfo->radioStat.mcs);
            break;

          case SnmpColumn::SNR:
            setLongValue(var, dataInfo->radioStat.snr);
            break;

          case SnmpColumn::RSSI:
            setLongValue(var, dataInfo->radioStat.rssi);
            break;

          default:
            snmp_log(
                LOG_ERR,
                "problem encountered in requestHandler: "
                "unknown column\n");
        }
        break;

      default:
        snmp_log(
            LOG_ERR,
            "problem encountered in requestHandler: "
            "unsupported mode\n");
    }
  }

  return SNMP_ERR_NOERROR;
}
