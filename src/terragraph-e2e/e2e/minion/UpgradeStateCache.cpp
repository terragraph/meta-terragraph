/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UpgradeStateCache.h"

#include <fbzmq/zmq/Zmq.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include <cstdio>
#include <glog/logging.h>
#include <iomanip>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "e2e/common/Md5Utils.h"

DEFINE_string(
    upgrade_cache_file, "/data/upgradeCache.json", "Upgrade cache file");

namespace {
const std::string kStatusKey{"status"};
const std::string kImageKey{"image"};
const std::string kMd5Key{"md5"}; // protects upgrade status only
const bool kDontResetStatus{false};
const size_t kMaxCacheFileSize{4096};
} // namespace

namespace facebook {
namespace terragraph {
namespace minion {

static std::string
getActiveImageVersion(const std::string& versionFile) {
  std::string version;
  if (!folly::readFile(versionFile.c_str(), version)) {
    folly::throwSystemError("readFile");
  }
  return version;
}

UpgradeStateCache::UpgradeStateCache(
    const std::string& versionFile)
    : versionFile_(versionFile) {
  // Assume that we won't find a (valid) cache
  upgradeStatus_.usType = thrift::UpgradeStatusType::NONE;

  struct stat statbuf;
  if (stat(FLAGS_upgrade_cache_file.c_str(), &statbuf) != 0) {
    if (errno != ENOENT) {
      LOG(ERROR) << FLAGS_upgrade_cache_file << " stat:"
          << folly::errnoStr(errno);
    }
    return; // no cache file
  }

  // Check cache file size
  if (statbuf.st_size > (off_t)kMaxCacheFileSize || statbuf.st_size == 0) {
    LOG(ERROR) << FLAGS_upgrade_cache_file << " bad size " << statbuf.st_size;
    invalidate();
    return;
  }

  // Load UpgradeStatus
  try {
    std::string cacheString;
    if (!folly::readFile(FLAGS_upgrade_cache_file.c_str(), cacheString)) {
      folly::throwSystemError("readFile");
    }
    auto cache = folly::parseJson(cacheString);
    if (cache[kStatusKey].empty() || cache[kImageKey].empty() ||
        cache[kMd5Key].empty()) {
      throw std::runtime_error("incomplete json");
    }

    // Check UpgradeStatus
    std::string md5 = Md5Utils::computeMd5(cache[kStatusKey].getString());
    if (md5.compare(cache[kMd5Key].getString()) != 0) {
      throw std::runtime_error("corrupt upgrade status");
    }

    // Check image version
    std::string activeVersion = getActiveImageVersion(versionFile_);
    if (activeVersion.compare(cache[kImageKey].getString()) != 0) {
      throw std::runtime_error("unexpected image version");
    }
    // Deserialize the upgrade status
    apache::thrift::SimpleJSONSerializer serializer;
    thrift::UpgradeStatus upgradeStatus =
        fbzmq::util::readThriftObjStr<thrift::UpgradeStatus>(
            cache[kStatusKey].getString(), serializer);
    upgradeStatus_ = upgradeStatus;
    LOG(INFO) << "Using upgrade cache " << FLAGS_upgrade_cache_file
              << " status: " << cache[kStatusKey].getString();
  } catch (std::exception& e) {
    LOG(ERROR) << "Failed to load upgrade cache "
        << FLAGS_upgrade_cache_file << ":"
               << e.what();
    invalidate();
  }
}

void
UpgradeStateCache::save(const thrift::UpgradeStatus& upgradeStatus) {
  upgradeStatus_ = upgradeStatus;

  // The stale cache file is dead, but keep the new status even if serialization
  // fails.
  invalidate(kDontResetStatus);

  try {
    apache::thrift::SimpleJSONSerializer serializer;
    auto statusString = fbzmq::util::writeThriftObjStr<thrift::UpgradeStatus>(
        upgradeStatus_, serializer);
    folly::dynamic cache = folly::dynamic::object(kStatusKey, statusString)(
        kImageKey, getActiveImageVersion(versionFile_))(
        kMd5Key, Md5Utils::computeMd5(statusString));

    if (!folly::writeFile(folly::toPrettyJson(cache),
          FLAGS_upgrade_cache_file.c_str())) {
      folly::throwSystemError("writeFile");
    }
    LOG(INFO) << "Updated upgrade cache " << FLAGS_upgrade_cache_file;
  } catch (std::exception& e) {
    LOG(ERROR) << "Failed to update upgrade cache "
        << FLAGS_upgrade_cache_file << ":"
               << e.what();
    invalidate(kDontResetStatus);
  }
}

void
UpgradeStateCache::invalidate(bool resetStatus) {
  std::remove(FLAGS_upgrade_cache_file.c_str());
  if (resetStatus) {
    upgradeStatus_.usType = thrift::UpgradeStatusType::NONE;
  }
}

} // namespace minion
} // namespace terragraph
} // namespace facebook
