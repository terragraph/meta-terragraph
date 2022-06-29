/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include "Access.h"

/**
 * Initialize the MIB by registering our OID with the net-snmp library.
 */
void initAgent();

/**
 * Handler function for processing net-snmp request.
 *
 * This supports read-only operations (GET/GETNEXT/GETBULK requests)
 */
Netsnmp_Node_Handler requestHandler;

/**
 * Set ASN_OCTET_STR value into netsnmp_variable_list.
 */
void setStringValue(netsnmp_variable_list* var, const std::string& value);

/**
 * Set ASN_INTEGER value into netsnmp_variable_list.
 */
void setLongValue(netsnmp_variable_list* var, const long& value);

/**
 * Set ASN_GAUGE value into netsnmp_variable_list.
 */
void setULongValue(netsnmp_variable_list* var, const u_long& value);
