/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

// for uWebSockets
#include <App.h>

namespace facebook {
namespace terragraph {

/**
 * Web route handlers
 */
class WebRoute {
 public:
  /** Set the static resource path, returning true upon success */
  static bool setStaticResourcePath(const std::string& path);

  /** Serve static resources */
  static void handlerStaticResources(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);

  /** Serve LinkStatusDump */
  static void handlerLinkDump(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Serve StatusReport */
  static void handlerStatusReport(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Process link ignition assoc/disassoc request */
  static void handlerLinkIgnition(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Serve JSON node config */
  static void handlerConfigGet(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Process setting node config */
  static void handlerConfigSet(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Serve ScanResp from a topology scan request */
  static void handlerTopologyScan(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);
  /** Process reboot request */
  static void handlerReboot(
      uWS::HttpResponse<false>* res, uWS::HttpRequest* req);

 private:
  /**
   * Send a response.
   *
   * This function handles HTTP status, headers, compression, etc.
   */
  static void sendReply(
      uWS::HttpRequest* req,
      uWS::HttpResponse<false>* res,
      int responseCode,
      const std::string& message = "",
      const std::unordered_map<std::string, std::string>& headers =
          std::unordered_map<std::string, std::string>());

  /** Serve static file to HttpResponse socket, returning true upon success */
  static bool staticFileHandler(
      uWS::HttpResponse<false>* res,
      uWS::HttpRequest* req,
      const std::string& filePath);

  /** Serialize an object to JSON */
  template <class T>
  static std::string serializeToJson(const T& obj);

  /** Static resource path */
  static std::string staticResourcePath_;
};

} // namespace terragraph
} // namespace facebook
