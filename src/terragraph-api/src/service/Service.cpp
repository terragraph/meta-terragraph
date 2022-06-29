/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <cerrno>
#include <memory>

#include <fbzmq/zmq/Zmq.h>
#include <folly/executors/GlobalExecutor.h>
#include <folly/executors/CPUThreadPoolExecutor.h>
#include <folly/init/Init.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/IPAddress.h>
#include <folly/FileUtil.h>
#include <folly/Memory.h>
#include <folly/portability/GFlags.h>
#include <folly/portability/Unistd.h>
#include <folly/Random.h>
#include <folly/Synchronized.h>
#include <folly/system/ThreadName.h>
#include <proxygen/httpserver/HTTPServer.h>
#include <proxygen/httpserver/RequestHandler.h>
#include <proxygen/httpserver/RequestHandlerFactory.h>
#include <proxygen/httpserver/ResponseBuilder.h>

#include "RequestHandler.h"
#include "StaticHandler.h"
#include "Streamer.h"
#include "StreamRequestHandler.h"

using namespace facebook::terragraph;
using namespace api;

// controller/aggregator ip & ports
DEFINE_string(
    controller_ip, "", "[DEPRECATED] IP of the controller we talk to");
DEFINE_string(
    controller_host,
    "localhost",
    "The hostname or IP of the controller we talk to");
DEFINE_int32(controller_port, 17077, "The port controller listens on");
DEFINE_int32(controller_pub_port, 17078,
    "The port controller publishes events on");
DEFINE_string(
    aggregator_ip, "", "[DEPRECATED] IP of the aggregator we talk to");
DEFINE_string(
    aggregator_host,
    "localhost",
    "The hostname or IP of the aggregator we talk to");
DEFINE_int32(aggregator_port, 18100, "The port aggregator listens on");

// webserver configuration
DEFINE_int32(http_port, 8080, "Port to listen on with HTTP protocol");
DEFINE_string(ipv6_global_addressable_ifname, "", "[deprecated]");
DEFINE_string(ip_override, "",
    "A specific IP/hostname to bind to (if omitted, uses INADDR_ANY)");
DEFINE_int32(threads, 4, "Number of threads to listen on. Numbers <= 0 "
    "will use the number of cores on this machine.");
DEFINE_int32(idle_timeout_ms, 60000, "Idle timeout for HTTP connections (ms)");
DEFINE_bool(zlib_compression, true, "Enable zlib compression in API responses");
DEFINE_int32(zlib_compression_level, 6,
    "zlib compression level (-1 default, 9 slowest, 4~6 recommended)");

// apidoc directory
DEFINE_string(docs_dir, "/etc/apidoc/", "The apidoc directory on disk");

// URL routes
DEFINE_string(api_path, "/api/", "The expected URL path for API calls");
DEFINE_string(docs_path, "/docs/", "The expected URL path for static API docs");
DEFINE_string(stream_path, "/api/stream/",
    "The expected URL path for streaming API calls");

// validation + auth
DEFINE_string(
    public_key_file,
    "",
    "The key used to decode JWTs in requests (only enforced if present)");

// access log configuration
DEFINE_string(
    audit_log_path,
    "/data/audit_logs/api_audit.log",
    "Path to access api audit log file");
DEFINE_int32(
    audit_log_buffer_size,
    1000,
    "Max number of log lines the access log buffer will store");

namespace {
  // The ZMQ ID prefix (will have random suffix)
  const std::string kZmqId{"api-service-"};

  // Generate a random (hopefully unique) ZMQ ID
  std::string
  generateZmqId() {
    return kZmqId + std::to_string(folly::Random::rand64());
  }
}

// Handler for nonexistent paths
class NotFoundHandler : public proxygen::RequestHandler {
 public:
  void onRequest(std::unique_ptr<proxygen::HTTPMessage> /*headers*/)
      noexcept override  {
    proxygen::ResponseBuilder(downstream_)
        .status(404, "Not Found").sendWithEOM();
  }

  void onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept override {
  }

  void onEOM() noexcept override {
  }

  void onUpgrade(proxygen::UpgradeProtocol /*protocol*/) noexcept override {
  }

  void requestComplete() noexcept override {
    delete this;
  }

  void onError(proxygen::ProxygenError /*err*/) noexcept override {
    delete this;
  }
};

// Constructs a new RequestHandler for each request using a shared ZMQ context.
class RequestHandlerFactory : public proxygen::RequestHandlerFactory {
 public:
  RequestHandlerFactory(
      fbzmq::Context& context,
      const std::string& ctrlRouterUrl,
      const std::string& aggrRouterUrl,
      StreamRequestHandler::StreamClients& streamClients,
      const std::string& publicKey,
      std::shared_ptr<AuditLogger> auditor)
      : context_(context),
        ctrlRouterUrl_(ctrlRouterUrl),
        aggrRouterUrl_(aggrRouterUrl),
        streamClients_(streamClients),
        publicKey_(publicKey),
        auditor_(auditor) {
  }

  void onServerStart(folly::EventBase* /*evb*/) noexcept override {
    LOG(INFO) << "Server started!";
  }

  void onServerStop() noexcept override {
    LOG(INFO) << "Server stopped!";
  }

  proxygen::RequestHandler* onRequest(
      proxygen::RequestHandler* /*handler*/,
      proxygen::HTTPMessage* headers) noexcept override {
    // Route the request to the proper handler
    // Check for stream_path first
    if (headers->getPath().find(FLAGS_stream_path) == 0) {
      return new StreamRequestHandler(
          FLAGS_stream_path,
          folly::EventBaseManager::get()->getExistingEventBase(),
          streamClients_);
    } else if (headers->getPath().find(FLAGS_api_path) == 0) {
      std::string zmqId = generateZmqId();
      return new RequestHandler(
          FLAGS_api_path,
          context_,
          ctrlRouterUrl_,
          aggrRouterUrl_,
          zmqId,
          publicKey_,
          auditor_);
    } else if (headers->getPath().find(FLAGS_docs_path) == 0) {
      return new StaticHandler(FLAGS_docs_path, FLAGS_docs_dir);
    } else {
      return new NotFoundHandler();
    }
  }

 private:
  fbzmq::Context& context_;
  const std::string ctrlRouterUrl_;
  const std::string aggrRouterUrl_;
  StreamRequestHandler::StreamClients& streamClients_;
  const std::string publicKey_;
  const std::shared_ptr<AuditLogger> auditor_;
};

int
main(int argc, char** argv) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;

  // Build ZMQ socket urls
  std::string controller_host = FLAGS_controller_host;
  if (!google::GetCommandLineFlagInfoOrDie("controller_ip").is_default) {
    controller_host = folly::sformat("[{}]", FLAGS_controller_ip);
  }

  std::string aggregator_host = FLAGS_aggregator_host;
  if (!google::GetCommandLineFlagInfoOrDie("aggregator_ip").is_default) {
    aggregator_host = folly::sformat("[{}]", FLAGS_aggregator_ip);
  }

  std::string ctrlRouterUrl = folly::sformat(
      "tcp://{}:{}", controller_host, FLAGS_controller_port);
  std::string ctrlPubUrl = folly::sformat(
      "tcp://{}:{}", controller_host, FLAGS_controller_pub_port);
  std::string aggrRouterUrl = folly::sformat(
      "tcp://{}:{}", aggregator_host, FLAGS_aggregator_port);
  fbzmq::Context context;

  // Validate apidoc directory path (expect trailing '/')
  if (FLAGS_docs_dir.empty() || FLAGS_docs_dir.back() != '/') {
    FLAGS_docs_dir += '/';
  }

  // Read in Keycloak's public key on file
  std::string publicKey;
  if (!FLAGS_public_key_file.empty() &&
      !folly::readFile(FLAGS_public_key_file.c_str(), publicKey)) {
    LOG(FATAL) << "Unable to read public key file, " << FLAGS_public_key_file
               << ": " << std::strerror(errno);
  }

  // Determine IP address to bind to
  folly::SocketAddress socketAddr;
  if (!FLAGS_ip_override.empty()) {
    socketAddr = folly::SocketAddress(FLAGS_ip_override, FLAGS_http_port, true);
  } else {
    sockaddr_in6 addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin6_family = AF_INET6;
    addr.sin6_addr = in6addr_any;
    socketAddr = folly::SocketAddress(
        folly::IPAddress((sockaddr*) &addr), FLAGS_http_port);
  }
  LOG(INFO) << "Binding to " << socketAddr.getAddressStr() << " port "
            << FLAGS_http_port << "...";

  // Set holding stream requests that is shared between the
  // Streamer thread and Proxygen request handlers
  StreamRequestHandler::StreamClients streamClients;

  // Start Audit Logger
  auto auditor = std::make_shared<AuditLogger>(
      FLAGS_audit_log_path, FLAGS_audit_log_buffer_size);

  // Configure proxygen
  std::vector<proxygen::HTTPServer::IPConfig> IPs = {
      {socketAddr, proxygen::HTTPServer::Protocol::HTTP},
  };
  if (FLAGS_threads <= 0) {
    FLAGS_threads = sysconf(_SC_NPROCESSORS_ONLN);
    CHECK_GT(FLAGS_threads, 0);
  }
  proxygen::HTTPServerOptions options;
  options.threads = static_cast<size_t>(FLAGS_threads);
  options.idleTimeout = std::chrono::milliseconds(FLAGS_idle_timeout_ms);
  options.shutdownOn = {SIGINT, SIGTERM};
  options.enableContentCompression = FLAGS_zlib_compression;
  options.contentCompressionLevel = FLAGS_zlib_compression_level;
  options.handlerFactories = proxygen::RequestHandlerChain()
      .addThen<RequestHandlerFactory>(
          context,
          ctrlRouterUrl,
          aggrRouterUrl,
          streamClients,
          publicKey,
          auditor)
      .build();

  // Set CPU thread pool for StaticHandler to use
  auto diskIOThreadPool = std::make_shared<folly::CPUThreadPoolExecutor>(
      FLAGS_threads,
      std::make_shared<folly::NamedThreadFactory>("StaticDiskIOThread"));
  folly::setUnsafeMutableGlobalCPUExecutor(diskIOThreadPool);

  // Start http server
  proxygen::HTTPServer server(std::move(options));
  server.bind(IPs);
  std::thread t([&] () {
    LOG(INFO) << "Starting HTTP server thread...";
    folly::setThreadName("ProxygenHTTPServer");
    server.start();
    LOG(INFO) << "HTTP server thread got stopped";
  });

  // Start the streamer thread
  Streamer streamer(
      context,
      generateZmqId(),
      ctrlPubUrl,
      std::chrono::seconds(60),
      streamClients);
  std::thread streamerThread([&streamer]() noexcept {
    LOG(INFO) << "Starting Streamer thread...";
    folly::setThreadName("Streamer");
    streamer.run();
    LOG(INFO) << "Streamer thread got stopped";
  });
  streamer.waitUntilRunning();

  t.join();

  streamer.stop();
  streamer.waitUntilStopped();
  streamerThread.join();

  return 0;
}
