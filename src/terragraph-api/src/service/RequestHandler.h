/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <folly/Memory.h>
#include <proxygen/httpserver/RequestHandler.h>

#include "ApiClient.h"
#include "AuditLogger.h"

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Handler for all proxygen requests.
 */
class RequestHandler : public proxygen::RequestHandler {
 public:
  explicit RequestHandler(
      const std::string& urlPrefix,
      fbzmq::Context& context,
      const std::string& ctrlRouterUrl,
      const std::string& aggrRouterUrl,
      const std::string& zmqId,
      const std::string& publicKey,
      const std::shared_ptr<AuditLogger>& auditor);

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> message)
      noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

 private:
  // Information about the version of the API requested
  struct VersionInfo {
    // The API version (e.g. 1 or 2)
    size_t version;

    // The position in the URL that the version ends at (i.e. where the
    // method name starts)
    size_t version_prefix_offset;
  };
  VersionInfo getVersion(const std::string& url);

  // Translate between proxygen's `HTTPMethod` enum and
  // `ApiClient::RequestFunction::HTTPMethod`.
  bool areRequestMethodsEqual(
      const proxygen::HTTPMethod proxygenMethod,
      ApiClient::RequestFunction::HTTPMethod requestMethod);

  // Send a 400 error (Bad Request).
  void sendErrorResponse(const std::string& reason);

  // Send a 401 error (Unauthorized).
  void sendUnauthorizedResponse();

  // Send a 403 error (Forbidden).
  void sendForbiddenResponse();

  // Send a 503 error (Service Unavailable).
  void sendServiceUnavailableResponse(const std::string& reason);

  // The expected URL prefix
  const std::string urlPrefix_;

  // Keycloak's public key
  const std::string publicKey_;

  // The API client
    ApiClient apiClient_;

  // The HTTP headers
  std::unique_ptr<proxygen::HTTPMessage> message_;

  // The request body buffer
  std::unique_ptr<folly::IOBuf> body_;

  // AuditLogger instance for this request to log network changes to
  const std::shared_ptr<AuditLogger> auditor_;
};

} // namesapce api
} // namespace terragraph
} // namespace facebook
