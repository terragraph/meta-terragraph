/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Prefix for websocket URLs
let wsPrefix = `ws://${window.location.hostname}/`;

// Return a full websocket URL for the given endpoint.
function wsUrl(endpoint) {
  return wsPrefix + endpoint;
}

// Set the websocket URL prefix.
export function setWsUrlPrefix(prefix) {
  wsPrefix = prefix;
}

// Add common event listeners for websockets.
function addListeners(socket, dataCallback) {
  socket.addEventListener("open", function(event) {
    console.log("Websocket opened.", event);
    console.log();
  });
  socket.addEventListener("message", function(event) {
    dataCallback(JSON.parse(event.data));
  });
  socket.addEventListener("close", function(event) {
    console.log("Websocket closed.", event);
  });
}

// Listen for link stats.
export function getLinkStats(dataCallback) {
  const socket = new WebSocket(wsUrl("link_stats"));
  addListeners(socket, dataCallback);
  return socket;
}
