/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UpgradeUtils.h"
#include "Md5Utils.h"

#include <folly/FileUtil.h>

#include <regex>
#include <stdexcept>

namespace {
const std::string kHeaderSizePrefix{"HDRSIZE="};
const std::string kPreambleBlockSizePrefix{"PREAMBLE_BLOCK_SIZE="};
// Expecting all the image parameters to appear near the beginning of
// the upgrade binary, in the first few lines of the upgrade script.
const size_t kImageParamMaxPosition{1024};
} // namespace

namespace facebook {
namespace terragraph {

static size_t
getImageHeaderSize(const std::string& imageFile) {
  std::string buf;
  if (!folly::readFile(imageFile.c_str(), buf, kImageParamMaxPosition)) {
    throw std::runtime_error(std::string("Can't read ") + imageFile);
  }

  // Find image header size
  std::regex reg_expr(kHeaderSizePrefix + "([0-9]+)");
  std::smatch m;
  if (!std::regex_search(buf, m, reg_expr)) {
    throw std::runtime_error(
        std::string("Can't find image header size in ") + imageFile);
  }
  const int sz = std::stoi(m[0].str().substr(kHeaderSizePrefix.length()));
  if (sz <= m.position(0)) {
    throw std::runtime_error(
        std::string("Bad image header size in ") + imageFile);
  }

  // Identify legacy image
  reg_expr = kPreambleBlockSizePrefix + "([0-9]+)";
  const bool legacy = !std::regex_search(buf, m, reg_expr);

  return legacy ? 2 * sz : sz;
}

std::string
UpgradeUtils::getImageMd5(const std::string& path) {
  return Md5Utils::computeFileMd5(path, getImageHeaderSize(path));
}

void
UpgradeUtils::verifyImage(
    const std::string& path, const std::string& expectedMd5) {
  const std::string md5 = getImageMd5(path);
  if (expectedMd5 != md5) {
    throw std::runtime_error(
        std::string("Bad MD5 in ") + path + ". expected=" + expectedMd5 +
        " computed=" + md5);
  }
}

} // namespace terragraph
} // namespace facebook
