/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "CurlUtil.h"

#include <cstdio>
#include <curl/curl.h>
#include <folly/FileUtil.h>
#include <folly/Format.h>
#include <folly/Uri.h>
#include <sys/stat.h>

extern "C" {
static size_t
curlWrite(void* ptr, size_t size, size_t nmemb, FILE* stream) {
  return fwrite(ptr, size, nmemb, stream);
}
}

namespace facebook {
namespace terragraph {

namespace {
const mode_t IMAGE_PERMS = S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH;
}

bool
CurlUtil::download(
    const std::string& url,
    const std::string& savePath,
    std::string& retError) {
  auto curl = curl_easy_init();
  if (!curl) {
    retError = "Unable to initialize curl";
    return false;
  }

  auto fp = fopen(savePath.c_str(), "w");
  if (!fp) {
    retError = folly::sformat("Unable to open file {}", savePath);
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

  std::string uriScheme;
  try {
    uriScheme = folly::Uri(url).scheme();
  } catch (std::invalid_argument& ex) {
    retError = folly::sformat("Invalid url: {}", url);
    return false;
  }

  if (uriScheme == "https") {
    // require use of SSL for this, or fail
    curl_easy_setopt(curl, CURLOPT_USE_SSL, CURLUSESSL_ALL);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &curlWrite);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, fp);
  curl_easy_setopt(curl, CURLOPT_FAILONERROR, true);
  auto res = curl_easy_perform(curl);

  // clean up
  curl_easy_cleanup(curl);
  fclose(fp);

  if (res != CURLE_OK) {  // check curl status
    retError = folly::sformat(
        "CURL error: {}",
        std::string(curl_easy_strerror(res)));
  } else if (chmod(savePath.c_str(), IMAGE_PERMS) < 0) {  // fix permissions
    retError = folly::sformat("chmod failed on {}", savePath);
  } else {
    return true;
  }

  // download failed, clean up empty file
  if (access(savePath.c_str(), F_OK) != -1) {
    struct stat st;
    if (stat(savePath.c_str(), &st) == 0 && st.st_size == 0) {
      std::remove(savePath.c_str());
    }
  }
  return false;
}

bool
CurlUtil::upload(
    const std::string& url, const std::string& path, std::string& retError) {
  CURL *curl = curl_easy_init();
  curl_mime *form = curl_mime_init(curl);
  curl_mimepart *file = curl_mime_addpart(form);
  curl_mime_name(file, "file");
  curl_mime_filedata(file, path.c_str());
  curl_easy_setopt(curl, CURLOPT_MIMEPOST, form);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 1L);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0);
  CURLcode res = curl_easy_perform(curl);
  curl_mime_free(form);
  curl_easy_cleanup(curl);
  if (res != CURLE_OK) {
    retError = folly::sformat(
        "CURL error: {}", std::string(curl_easy_strerror(res)));
    return false;
  }
  return true;
}

} // namespace terragraph
} // namespace facebook
