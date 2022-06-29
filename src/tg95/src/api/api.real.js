/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

import axios from "axios";

// Prefix for API URLs
let apiPrefix = "/";

// Return a full API URL for the given endpoint.
function apiUrl(endpoint) {
  return apiPrefix + endpoint;
}

// Set the API URL prefix.
export function setApiUrlPrefix(prefix) {
  apiPrefix = prefix;
}

// Generic axios error handler.
function genericErrorHandler(error) {
  return error?.response?.data || error.message;
}

// Fetch a status report and execute the given callback functions.
export function getStatusReport(onSuccess, onError) {
  axios
    .get(apiUrl("status_report"))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Fetch a link dump and execute the given callback functions.
export function getLinkDump(onSuccess, onError) {
  axios
    .get(apiUrl("link_dump"))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Assoc a link and execute the given callback functions.
export function assocLink(initiatorMac, responderMac, onSuccess, onError) {
  axios
    .get(apiUrl(`link/assoc/${initiatorMac}/${responderMac}`))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Dissoc a link and execute the given callback functions.
export function dissocLink(initiatorMac, responderMac, onSuccess, onError) {
  axios
    .get(apiUrl(`link/disassoc/${initiatorMac}/${responderMac}`))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Start a topology scan and execute the given callback functions.
export function topoScan(radioMac, onSuccess, onError) {
  axios
    .get(apiUrl(`topo_scan/${radioMac}`))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Fetch node config and execute the given callback functions.
export function getNodeConfig(onSuccess, onError) {
  // pass empty "transformResponse" so axios returns raw text
  axios
    .get(apiUrl("node_config"), { transformResponse: [] })
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Fetch node config and execute the given callback functions.
export function setNodeConfig(config, onSuccess, onError) {
  axios
    .post(apiUrl("node_config"), config)
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}

// Send a reboot command and execute the given callback functions.
export function sendReboot(onSuccess, onError) {
  axios
    .get(apiUrl("reboot"))
    .then(response => onSuccess(response.data))
    .catch(error => onError(genericErrorHandler(error)));
}
