/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#pragma once

#include <folly/Memory.h>
#include <folly/File.h>
#include <proxygen/httpserver/RequestHandler.h>

namespace facebook {
namespace terragraph {
namespace api {

/**
 * Handler for apidoc static resources.
 *
 * Only supports GET, and uses a CPU thread pool since read(2) is blocking.
 * If egress pauses, file reading is also paused.
 *
 * NOTE: This file is based on the proxygen sample class
 * proxygen::StaticService::StaticHandler in the proxygen repository.
 */
class StaticHandler : public proxygen::RequestHandler {
 public:
  explicit StaticHandler(
      const std::string& urlPrefix, const std::string& docsDir);

  void onRequest(std::unique_ptr<proxygen::HTTPMessage> headers)
      noexcept override;

  void onBody(std::unique_ptr<folly::IOBuf> body) noexcept override;

  void onEOM() noexcept override;

  void onUpgrade(proxygen::UpgradeProtocol proto) noexcept override;

  void requestComplete() noexcept override;

  void onError(proxygen::ProxygenError err) noexcept override;

  void onEgressPaused() noexcept override;

  void onEgressResumed() noexcept override;

 private:
  void readFile(folly::EventBase* evb);

  bool checkForCompletion();

  // The expected URL prefix
  const std::string urlPrefix_;

  // The apidoc directory
  const std::string docsDir_;

  std::unique_ptr<folly::File> file_;
  bool readFileScheduled_{false};
  std::atomic<bool> paused_{false};
  bool finished_{false};
};

} // namesapce api
} // namespace terragraph
} // namespace facebook
