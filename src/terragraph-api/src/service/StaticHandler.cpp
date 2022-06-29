/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "StaticHandler.h"

#include <folly/io/async/EventBaseManager.h>
#include <folly/FileUtil.h>
#include <folly/executors/GlobalExecutor.h>
#include <limits.h>
#include <proxygen/httpserver/ResponseBuilder.h>
#include <sys/stat.h>
#include <sys/types.h>

using namespace proxygen;

namespace facebook {
namespace terragraph {
namespace api {

StaticHandler::StaticHandler(
    const std::string& urlPrefix, const std::string& docsDir)
    : urlPrefix_(urlPrefix), docsDir_(docsDir) {
  // Warn if docs directory doesn't exist
  struct stat st;
  if (stat(docsDir.c_str(), &st) != 0 || !(st.st_mode & S_IFDIR)) {
    LOG(ERROR) << "Unable to access apidoc directory: " << docsDir;
  }
}

void
StaticHandler::onRequest(std::unique_ptr<HTTPMessage> headers)
    noexcept {
  VLOG(1) << "[" << headers->getClientIP() << "] "
          << headers->getMethodString() << " " << headers->getPath();

  if (headers->getPath().find(urlPrefix_) != 0) {
    ResponseBuilder(downstream_).status(400, "Bad Request").sendWithEOM();
  }
  if (headers->getMethod() != HTTPMethod::GET) {
    ResponseBuilder(downstream_).status(400, "Bad Method").sendWithEOM();
    return;
  }

  // Get the file path
  std::string filename = headers->getPath().substr(urlPrefix_.length());
  if (filename.empty()) {
    filename = "index.html";
  }
  // Verify the file path exists and isn't malicious:
  // compare the resolved path against the static directory path
  std::string filepath = docsDir_ + filename;
  char resolvedPath[PATH_MAX];
  if (!realpath(filepath.c_str(), resolvedPath) ||
      std::string(resolvedPath).find(docsDir_) != 0) {
    ResponseBuilder(downstream_).status(404, "Not Found").sendWithEOM();
    return;
  }

  // Open the file
  try {
    file_ = std::make_unique<folly::File>(filepath.c_str());
  } catch (const std::system_error& ex) {
    ResponseBuilder(downstream_).status(404, "Not Found").sendWithEOM();
    return;
  }
  ResponseBuilder(downstream_).status(200, "OK").send();

  // Use a CPU executor since read(2) of a file can block
  readFileScheduled_ = true;
  folly::getUnsafeMutableGlobalCPUExecutor()->add(
      std::bind(
          &StaticHandler::readFile,
          this,
          folly::EventBaseManager::get()->getEventBase()));
}

void
StaticHandler::readFile(folly::EventBase* evb) {
  folly::IOBufQueue buf;
  while (file_ && !paused_) {
    // read 4k-ish chunks and forward each one to the client
    auto data = buf.preallocate(4000, 4000);
    auto rc = folly::readNoInt(file_->fd(), data.first, data.second);
    if (rc < 0) {
      // error
      VLOG(4) << "Read error=" << rc;
      file_.reset();
      evb->runInEventBaseThread([this] {
        LOG(ERROR) << "Error reading file";
        downstream_->sendAbort();
      });
      break;
    } else if (rc == 0) {
      // done
      file_.reset();
      VLOG(4) << "Read EOF";
      evb->runInEventBaseThread([this] {
        ResponseBuilder(downstream_).sendWithEOM();
      });
      break;
    } else {
      buf.postallocate(rc);
      evb->runInEventBaseThread([this, body=buf.move()]() mutable {
        ResponseBuilder(downstream_).body(std::move(body)).send();
      });
    }
  }

  // Notify the request thread that we terminated the readFile loop
  evb->runInEventBaseThread([this] {
    readFileScheduled_ = false;
    if (!checkForCompletion() && !paused_) {
      VLOG(4) << "Resuming deferred readFile";
      onEgressResumed();
    }
  });
}

void
StaticHandler::onEgressPaused() noexcept {
  // This will terminate readFile soon
  VLOG(4) << "StaticHandler paused";
  paused_ = true;
}

void
StaticHandler::onEgressResumed() noexcept {
  VLOG(4) << "StaticHandler resumed";
  paused_ = false;
  // If readFileScheduled_, it will reschedule itself
  if (!readFileScheduled_ && file_) {
    readFileScheduled_ = true;
    folly::getUnsafeMutableGlobalCPUExecutor()->add(
        std::bind(
            &StaticHandler::readFile,
            this,
            folly::EventBaseManager::get()->getEventBase()));
  } else {
    VLOG(4) << "Deferred scheduling readFile";
  }
}

void
StaticHandler::onBody(std::unique_ptr<folly::IOBuf> /*body*/) noexcept {
  // ignore, only support GET
}

void
StaticHandler::onEOM() noexcept {
}

void
StaticHandler::onUpgrade(UpgradeProtocol /*protocol*/) noexcept {
  // handler doesn't support upgrades
}

void
StaticHandler::requestComplete() noexcept {
  finished_ = true;
  paused_ = true;
  checkForCompletion();
}

void
StaticHandler::onError(ProxygenError /*err*/) noexcept {
  finished_ = true;
  paused_ = true;
  checkForCompletion();
}

bool
StaticHandler::checkForCompletion() {
  if (finished_ && !readFileScheduled_) {
    VLOG(4) << "deleting StaticHandler";
    delete this;
    return true;
  }
  return false;
}

} // namesapce api
} // namespace terragraph
} // namespace facebook
