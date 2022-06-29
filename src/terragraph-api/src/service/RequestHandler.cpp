/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "RequestHandler.h"

#include <chrono>
#include <ctime>

#include <folly/dynamic.h>
#include <jwt/jwt.hpp>
#include <proxygen/httpserver/ResponseBuilder.h>

using namespace jwt::params;
using namespace proxygen;

DEFINE_string(jwt_algorithm, "RS256", "The algorithm used for decoding JWTs");

namespace facebook {
namespace terragraph {
namespace api {

RequestHandler::RequestHandler(
    const std::string& urlPrefix,
    fbzmq::Context& context,
    const std::string& ctrlRouterUrl,
    const std::string& aggrRouterUrl,
    const std::string& zmqId,
    const std::string& publicKey,
    const std::shared_ptr<AuditLogger>& auditor)
    : urlPrefix_(urlPrefix),
      publicKey_(publicKey),
      apiClient_{context, ctrlRouterUrl, aggrRouterUrl, zmqId},
      auditor_{auditor} {}

RequestHandler::VersionInfo
RequestHandler::getVersion(const std::string& url) {
  static std::vector<std::pair<std::string, size_t>> validVersions = {
    {"v2/", 2},
    {"v1/", 1}
  };
  for (const auto& version : validVersions) {
    // Search for the version string at the start of the url
    if (url.rfind(version.first, 0) == 0) {
      return RequestHandler::VersionInfo{version.second,
                                         version.first.length()};
    }
  }
  return RequestHandler::VersionInfo{1, 0};
}

bool
RequestHandler::areRequestMethodsEqual(
    const proxygen::HTTPMethod proxygenMethod,
    const ApiClient::RequestFunction::HTTPMethod requestMethod) {
  return (requestMethod == ApiClient::RequestFunction::POST &&
          proxygenMethod == proxygen::HTTPMethod::POST) ||
         (requestMethod == ApiClient::RequestFunction::GET &&
          proxygenMethod == proxygen::HTTPMethod::GET);
}

void
RequestHandler::onRequest(std::unique_ptr<HTTPMessage> message) noexcept {
  LOG(INFO) << "[" << message->getClientIP()
            << "] Request path: " << message->getPath();
  message_ = std::move(message);
}

void
RequestHandler::onBody(std::unique_ptr<folly::IOBuf> body) noexcept {
  if (body_) {
    body_->prependChain(std::move(body));
  } else {
    body_ = std::move(body);
  }
}

void
RequestHandler::onEOM() noexcept {
  // Find the API method
  if (!message_ || message_->getPath().find(urlPrefix_) != 0) {
    return sendErrorResponse(
        "EOM was received but no HTTP message for " + urlPrefix_);
  }

  std::string requestPath, requestUsername, requestEmail, requestClient;
  requestPath = message_->getPath();
  auto versionInfo = getVersion(requestPath.substr(urlPrefix_.length()));
  auto methodName = requestPath.substr(
      urlPrefix_.length() + versionInfo.version_prefix_offset);
  auto makeMethodRequest = apiClient_.get(methodName);
  if (!makeMethodRequest) {
    return sendErrorResponse("Could not determine method from client");
  }

  auto httpMethod = message_->getMethod();
  if (!httpMethod) {
    return sendErrorResponse("Request has no HTTP method");
  } else if (
      versionInfo.version == 1 && httpMethod != proxygen::HTTPMethod::POST) {
    return sendErrorResponse(
        "Did not receive expected HTTP method for endpoint");
  } else if (
      versionInfo.version != 1 &&
      !areRequestMethodsEqual(*httpMethod, makeMethodRequest->method())) {
    // Only validate httpMethod for v2 API. Everything in v1 API is a POST
    // request, so those requests can skip this step.
    return sendErrorResponse(
        "Did not receive expected HTTP method for endpoint");
  }

  // Verify the issuer of the request if a public key is provided
  if (!publicKey_.empty() && !makeMethodRequest->isAuthless()) {
    std::string auth = message_->getHeaders().getSingleOrEmpty("Authorization");
    if (auth.empty()) {
      return sendUnauthorizedResponse();
    }

    // Extract the JSON web token, e.g. "Authorization: Bearer <token>"
    std::string prefix = "Bearer ";
    if (auth.compare(0, prefix.size(), prefix)) {
      return sendUnauthorizedResponse();
    }

    std::string token = auth.substr(prefix.size());
    try {
      jwt::jwt_payload payload =
          jwt::decode(
              token, algorithms({FLAGS_jwt_algorithm}), secret(publicKey_))
              .payload();

      std::vector<std::string> roles;
      if (payload.has_claim("realm_access")) {
        json_t realm_access = payload.get_claim_value<json_t>("realm_access");
        if (realm_access.find("roles") != realm_access.end()) {
          roles = realm_access.at("roles").get<std::vector<std::string>>();
        } else {
          return sendUnauthorizedResponse();
        }
      } else if (payload.has_claim("roles")) {
        roles = payload.get_claim_value<std::vector<std::string>>("roles");
      } else {
        return sendUnauthorizedResponse();
      }

      if (!makeMethodRequest->hasPermission(roles)) {
        return sendForbiddenResponse();
      }
      // Get user, email and client (a.k.a. azp) info
      if (payload.has_claim("preferred_username")) {
        requestUsername =
            payload.get_claim_value<std::string>("preferred_username");
      }
      if (payload.has_claim("email")) {
        requestEmail = payload.get_claim_value<std::string>("email");
      }
      if (payload.has_claim("azp")) {
        requestClient = payload.get_claim_value<std::string>("azp");
      }

    } catch (const jwt::InvalidSignatureError& e) {
      LOG(ERROR) << "JWT verification failed: " << e.what();
      return sendForbiddenResponse();
    } catch (const std::exception& e) {
      LOG(ERROR) << "An error occurred while decoding the JSON web token: "
                 << e.what();
      return sendUnauthorizedResponse();
    }
  }

  std::string requestBody = "{}";
  if (httpMethod == proxygen::HTTPMethod::POST) {
    // Check for empty POST requests (smallest valid payload is "{}")
    if (!body_ || body_->length() < 2) {
      return sendErrorResponse(
          "Request body must be valid JSON ({} for an empty request");
    }

    // Reconstruct the request body
    auto byteRange = body_->coalesce();
    requestBody = std::string(byteRange.begin(), byteRange.end());
    VLOG(1) << "[" << message_->getClientIP()
            << "] POST request body: " << requestBody;
  } else if (httpMethod != proxygen::HTTPMethod::GET) {
    return sendErrorResponse("Only GET and POST methods are accepted");
  }

  // Make the request and return the response to the client
  auto resp = makeMethodRequest->applyFunction(&apiClient_, requestBody);
  if (!resp) {
    return sendServiceUnavailableResponse("No response from method");
  }

  // Reflect network changes in audit log
  if (makeMethodRequest->isWriteOperation()) {
    // Get unix timestamp for audit log entry
    uint64_t req_timestamp =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    auditor_->logNetworkChange(folly::dynamic::object
        ("time", req_timestamp)
        ("username", requestUsername)
        ("email", requestEmail)
        ("client", requestClient)
        ("path", requestPath)
        ("body", requestBody));
  }

  ResponseBuilder(downstream_)
      .status(200, "OK")
      .header<std::string>("Content-Type", "application/json")
      .body<std::string>(std::move(resp.value()))
      .sendWithEOM();
}

void
RequestHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
  // handler doesn't support upgrades
}

void
RequestHandler::requestComplete() noexcept {
  delete this;
}

void
RequestHandler::onError(ProxygenError /*err*/) noexcept {
  delete this;
}

void
RequestHandler::sendErrorResponse(const std::string& reason) {
  VLOG(3) << "[Bad Request] " << reason;
  ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
}

void
RequestHandler::sendUnauthorizedResponse() {
  ResponseBuilder(downstream_).status(401, "Unauthorized").sendWithEOM();
}

void
RequestHandler::sendForbiddenResponse() {
  ResponseBuilder(downstream_).status(403, "Forbidden").sendWithEOM();
}

void
RequestHandler::sendServiceUnavailableResponse(const std::string& reason) {
  VLOG(3) << "[Service Unavailable] " << reason;
  ResponseBuilder(downstream_).status(503, "Service Unavailable").sendWithEOM();
}

} // namespace api
} // namespace terragraph
} // namespace facebook
