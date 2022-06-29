/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "WebRoute.h"

#include <limits.h>
#include <stdlib.h>

// for uWebSockets
#include <PerMessageDeflate.h>

#include <folly/FileUtil.h>
#include <glog/logging.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "Consts.h"
#include "e2e/clients/MinionClient.h"

DEFINE_string(index_page, "index.html", "Name of index page");
DEFINE_bool(enable_cors, true, "Add CORS headers on API responses");
DEFINE_string(
    static_assets_prefix,
    "/static/",
    "Path prefix for static assets, used for cache configuration");
DEFINE_string(
    static_assets_cache_control,
    "max-age=31536000",
    "HTTP Cache-Control header value for static assets");
DEFINE_string(
    default_cache_control,
    "no-cache",
    "HTTP Cache-Control header default value");

namespace facebook {
namespace terragraph {

std::string WebRoute::staticResourcePath_ = "";

bool
WebRoute::setStaticResourcePath(const std::string& path) {
  // Resolve path using 'realpath'
  char resolvedPath[PATH_MAX];
  char *p = realpath(path.c_str(), resolvedPath);
  if (p == nullptr) {
    return false;
  } else {
    staticResourcePath_ = std::string(p);
    return true;
  }
}

bool
WebRoute::staticFileHandler(
    uWS::HttpResponse<false>* res,
    uWS::HttpRequest* req,
    const std::string& filePath) {
  std::string fileContents;
  bool readSuccess = folly::readFile(filePath.c_str(), fileContents);
  if (readSuccess) {
    VLOG(3) << "Serving file: " << filePath;
    std::unordered_map<std::string, std::string> headers;
    auto idx = filePath.find(FLAGS_static_assets_prefix);
    if (!FLAGS_static_assets_cache_control.empty() &&
        idx == staticResourcePath_.length()) {
      headers["Cache-Control"] = FLAGS_static_assets_cache_control;
    } else if (!FLAGS_default_cache_control.empty()) {
      headers["Cache-Control"] = FLAGS_default_cache_control;
    }
    sendReply(req, res, 200, fileContents, headers);
    return true;
  } else {
    LOG(ERROR) << "Unable to serve static content: " << filePath;
    return false;
  }
}

void
WebRoute::handlerStaticResources(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  if (!staticResourcePath_.empty()) {
    // Support index pages
    std::string url(req->getUrl());
    if (url == "/") {
      url += FLAGS_index_page;
    }

    // Resolve requested path using 'realpath'
    std::string file = staticResourcePath_ + url;
    char resolvedPath[PATH_MAX];
    char *p = realpath(file.c_str(), resolvedPath);
    if (p != nullptr) {
      std::string filePath(p);
      if (filePath.find(staticResourcePath_) == 0) {
        if (WebRoute::staticFileHandler(res, req, filePath)) {
          return;
        }
      } else {
        LOG(WARNING) << "Trying to access forbidden path: " << file;
      }
    } else {
      VLOG(3) << "File not found: " << file;
    }
  }
  sendReply(req, res, 404);
}

void
WebRoute::sendReply(
    uWS::HttpRequest* req,
    uWS::HttpResponse<false>* res,
    int responseCode,
    const std::string& message,
    const std::unordered_map<std::string, std::string>& headers) {
  switch (responseCode) {
    case 200:
      res->writeStatus("200 OK");
      break;
    case 400:
      res->writeStatus("400 Bad Request");
      break;
    case 404:
      res->writeStatus("404 Not Found");
      break;
    case 500:
      res->writeStatus("500 Internal Server Error");
      break;
  }

  // Add CORS headers
  if (FLAGS_enable_cors) {
    res->writeHeader("Access-Control-Allow-Origin", "*");
    res->writeHeader("Access-Control-Allow-Headers", "*");
  }

  // Add custom headers
  for (const auto& kv : headers) {
    res->writeHeader(kv.first, kv.second);
  }

  // Should we compress our response?
  std::string acceptEncoding = std::string(req->getHeader("accept-encoding"));
  if (acceptEncoding.find("deflate") != std::string::npos) {
    // DEFLATE format
    uWS::ZlibContext zlibContext;
    uWS::DeflationStream deflationStream(
        uWS::CompressOptions::SHARED_COMPRESSOR);
    res->writeHeader("Content-Encoding", "deflate");
    res->end(deflationStream.deflate(&zlibContext, message, false /* reset */));
  } else {
    // Uncompressed
    res->end(message);
  }
}

template <class T>
std::string
WebRoute::serializeToJson(const T& obj) {
  return apache::thrift::SimpleJSONSerializer::serialize<std::string>(obj);
}

void
WebRoute::handlerLinkDump(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  auto resp = minionClient.getLinkStatusDump();
  if (resp) {
    sendReply(req, res, 200, serializeToJson(resp.value()));
  } else {
    sendReply(req, res, 500);
  }
}

void
WebRoute::handlerStatusReport(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  auto resp = minionClient.getStatusReport();
  if (resp) {
    sendReply(req, res, 200, serializeToJson(resp.value()));
  } else {
    sendReply(req, res, 500);
  }
}

void
WebRoute::handlerLinkIgnition(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  std::string action{req->getParameter(0)};
  std::string initiatorMac{req->getParameter(1)};
  std::string responderMac{req->getParameter(2)};
  if (action != "assoc" && action != "disassoc") {
    sendReply(
        req, res, 500, "You must specify an action of 'assoc' or 'dissoc'");
    return;
  }
  thrift::LinkStatusType linkStatusType =
      action == "assoc" ? thrift::LinkStatusType::LINK_UP
                        : thrift::LinkStatusType::LINK_DOWN;

  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  bool sent = minionClient.sendSetLinkStatus(
      linkStatusType, initiatorMac, responderMac);
  if (sent) {
    sendReply(req, res, 200, "Request Sent");
  } else {
    sendReply(req, res, 500);
  }
}

void
WebRoute::handlerConfigGet(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  auto resp = minionClient.getNodeConfig();
  if (resp) {
    sendReply(req, res, 200, resp.value().config);
  } else {
    sendReply(req, res, 500);
  }
}

void
WebRoute::handlerConfigSet(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  std::string configBuffer;
  /* Move it to storage of lambda */
  res->onData([res, req, configBuffer = std::move(configBuffer)](
                  std::string_view data, bool last) mutable {
    /* Mutate the captured data */
    configBuffer.append(data.data(), data.length());
    if (last) {
      if (configBuffer.length() == 0) {
        sendReply(req, res, 400, "Empty Request");
        return;
      }

      // ensure valid JSON
      try {
        folly::parseJson(configBuffer);
      } catch (const std::runtime_error& ex) {
        sendReply(req, res, 400, "Invalid JSON Body");
        return;
      }

      fbzmq::Context context;
      facebook::terragraph::MinionClient minionClient(context);
      bool sent = minionClient.setNodeConfig(configBuffer);
      if (sent) {
        sendReply(req, res, 200, "Request Sent");
      } else {
        sendReply(req, res, 500);
      }
    }
  });
  // library ensures this is called, but nothing to implement
  res->onAborted([]() {});
}

void
WebRoute::handlerTopologyScan(
    uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  std::string radioMac{req->getParameter(0)};
  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  auto resp = minionClient.getTopoScan(radioMac);
  if (resp) {
    sendReply(req, res, 200, serializeToJson(resp.value()));
  } else {
    sendReply(req, res, 500);
  }
}

void
WebRoute::handlerReboot(uWS::HttpResponse<false>* res, uWS::HttpRequest* req) {
  fbzmq::Context context;
  facebook::terragraph::MinionClient minionClient(context);
  bool sent = minionClient.sendRebootCmd();
  if (sent) {
    sendReply(req, res, 200, "Request Sent");
  } else {
    sendReply(req, res, 500);
  }
}

} // namespace terragraph
} // namespace facebook
