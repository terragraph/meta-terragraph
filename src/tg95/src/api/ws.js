/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// Comment/uncomment the code below as needed.
// (No simpler way... there's no support for conditional imports/exports.)

/*
 * Use mock websocket APIs.
 */
// export * from "./ws.mock";

/*
 * Use real websocket APIs.
 * If needed, set the API URL (if not running server locally).
 */
// import { setWsUrlPrefix } from "./ws.real";
// setWsUrlPrefix("ws://[::1]/");
export * from "./ws.real";
