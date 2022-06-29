/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

// for uWebSockets
#include <App.h>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "StatsSubscriber.h"
#include "WebRoute.h"

using namespace facebook::terragraph;

using namespace std;

DEFINE_int32(http_port, 80, "Port to listen for HTTP requests");
DEFINE_string(
    static_resource_path,
    "",
    "Path to static resource directory to be served");

int
main(int argc, char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);

  FLAGS_logtostderr = true;

  // set static resource path for request handler
  if (!FLAGS_static_resource_path.empty() &&
      !WebRoute::setStaticResourcePath(FLAGS_static_resource_path)) {
    LOG(FATAL) << "Bad static resource path: " << FLAGS_static_resource_path;
  }

  // empty struct for WebSocket client storage
  struct NoData {};
  // event loop for stats subscriber thread
  auto loop = uWS::Loop::get();
  auto app = uWS::App()
                 .get("/link_dump", WebRoute::handlerLinkDump)
                 .get("/status_report", WebRoute::handlerStatusReport)
                 .get("/node_config", WebRoute::handlerConfigGet)
                 .post("/node_config", WebRoute::handlerConfigSet)
                 .get(
                     "/link/:action/:initiatorMac/:responderMac",
                     WebRoute::handlerLinkIgnition)
                 .get("/topo_scan/:radioMac", WebRoute::handlerTopologyScan)
                 .get("/reboot", WebRoute::handlerReboot)
                 .get("/*", WebRoute::handlerStaticResources)
                 .ws<NoData>(
                     "/link_stats",
                     {
                         .compression = uWS::SHARED_COMPRESSOR,
                         .idleTimeout = 60,
                         .open =
                             [](auto* ws) {
                               VLOG(1) << "Opened a websocket connection";
                               ws->subscribe("broadcast");
                             },
                         .message =
                             [](auto* ws,
                                std::string_view message,
                                uWS::OpCode opCode) {
                               ws->send(message, opCode, true);
                             },
                         .drain = [](auto* ws) { /* no-op */ },
                         .ping = [](auto* ws) { /* no-op */ },
                         .pong = [](auto* ws) { /* no-op */ },
                         .close =
                             [](auto* ws, int code, std::string_view message) {
                               VLOG(1) << "Closing websocket connection";
                             },
                     })
                 .listen(FLAGS_http_port, [](auto* token) {
                   if (token) {
                     LOG(INFO) << "Listening on port " << FLAGS_http_port;
                   } else {
                     LOG(FATAL)
                         << "Unable to listen on port " << FLAGS_http_port;
                   }
                 });

  // start broadcast thread for websocket connection publishing
  auto bcastThread = new std::thread([&loop, &app]() {
    // subscribe to stats stream
    std::unique_ptr<facebook::terragraph::StatsSubscriber> statsSubscriber =
        std::make_unique<facebook::terragraph::StatsSubscriber>(loop, &app);
    LOG(INFO) << "Starting StatsSubscriber thread for counter publishing.";
    statsSubscriber->run();
    statsSubscriber->stop();
    statsSubscriber->waitUntilStopped();
  });
  app.run();
  bcastThread->join();
}
