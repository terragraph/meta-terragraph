/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigUtil.h"

#include <regex>

#include <folly/Conv.h>
#include <folly/String.h>
#include <glog/logging.h>

#include "Consts.h"

namespace facebook {
namespace terragraph {

SwVersion::SwVersion(const std::string& ver) {
  const std::string kVersionPrefix = "RELEASE_";
  const std::string kVersionPostfix = "(";
  const std::string kVersionDelimiters = "-_";
  const std::string kMajorMinorVersionRegex = "RELEASE_M(\\d+)(_(\\d+))?";

  this->fullVersion = folly::trimWhitespace(ver).str();
  if (this->fullVersion.empty()) {
    return;
  }

  try {
    // Trim off the prefix and suffix substrings
    std::string s = this->fullVersion;
    size_t index;
    if ((index = s.find(kVersionPrefix)) != std::string::npos) {
      s = s.substr(index);
    }
    if ((index = s.find(kVersionPostfix)) != std::string::npos) {
      s = s.substr(0, index);
    }
    s = folly::trimWhitespace(s).str();

    // Tokenize the version body
    std::string v;
    while ((index = s.find_first_of(kVersionDelimiters)) != std::string::npos) {
      std::string token = s.substr(0, index);
      v += token + E2EConsts::kSwVersionDelimiter;
      this->tokens.push_back(token);

      s = s.substr(index + 1);
    }
    v += s;
    this->tokens.push_back(s);
    this->version = v;

    // Extract major/minor versions (if possible)
    std::smatch match;
    if (std::regex_search(v, match, std::regex(kMajorMinorVersionRegex))) {
      this->majorVersion = folly::to<size_t>(match[1].str());
      if (match.size() > 3 && match[3].length() > 0) {
        this->minorVersion = folly::to<size_t>(match[3].str());
      }
    }
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error parsing software version string '"
               << this->fullVersion << "': " << ex.what();
  }
}

FwVersion::FwVersion(const std::string& ver) {
  const std::string kMajorMinorFwVersionRegex =
      "([^.]*\\.[^.]*\\.[^.]*)\\.(\\d+)";

  this->fullVersion = ver;

  // Extract major/minor versions (if possible)
  std::smatch match;
  if (std::regex_search(ver, match, std::regex(kMajorMinorFwVersionRegex))) {
    this->majorVersion = match[1].str();
    this->minorVersion = folly::to<size_t>(match[2].str());
  } else {
    this->majorVersion = ver;
    this->minorVersion = 0;
  }
}

} // namespace terragraph
} // namespace facebook
