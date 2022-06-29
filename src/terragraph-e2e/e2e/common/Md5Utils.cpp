/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Md5Utils.h"

#include <fstream>
#include <iomanip>
#include <openssl/md5.h>
#include <sstream>

namespace facebook {
namespace terragraph {

std::string
Md5Utils::computeMd5(const std::string& input) {
  MD5_CTX context;
  MD5_Init(&context);
  MD5_Update(&context, input.c_str(), input.size());
  unsigned char result[MD5_DIGEST_LENGTH];
  MD5_Final(result, &context);
  return bytesToHex(result, MD5_DIGEST_LENGTH);
}

std::string
Md5Utils::computeFileMd5(const std::string& path, size_t skipHeaderSize) {
  std::ifstream stream(path, std::ifstream::binary);
  if (skipHeaderSize > 0) {
    stream.ignore(skipHeaderSize);
  }

  MD5_CTX context;
  MD5_Init(&context);
  char buf[1024 * 16];
  while (stream.good()) {
    stream.read(buf, sizeof(buf));
    MD5_Update(&context, buf, stream.gcount());
  }
  unsigned char result[MD5_DIGEST_LENGTH];
  MD5_Final(result, &context);
  return bytesToHex(result, MD5_DIGEST_LENGTH);
}

std::string
Md5Utils::bytesToHex(const unsigned char bytes[], size_t size) {
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (size_t i = 0; i < size; i++) {
    ss << std::setw(2) << (int) bytes[i];
  }
  return ss.str();
}

} // namespace terragraph
} // namespace facebook
