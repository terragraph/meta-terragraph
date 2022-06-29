/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <string>
#include <vector>

namespace facebook {
namespace terragraph {

/** Software version information. */
class SwVersion {
 public:
  /** Empty constructor. */
  SwVersion() {}

  /**
    * Construct SwVersion by parsing a software version string.
    *
    * Example string:
    *    ... Terragraph Release RELEASE_M16_RC1-104-gc442bb5-talkhasib (...
    *
    * The parser looks for the prefix "RELEASE_", then extracts and tokenizes
    * the substring until reaching the postfix "(" (or end of string):
    *    "RELEASE_M16_RC1-104-gc442bb5-talkhasib"
    *
    * Tokens are later used to find the best match possible.
    *
    * Additionally, major/minor versions are parsed when the version string
    * body is of form:
    *    "RELEASE_M<major>_<minor>"
    *
    * For example, given "RELEASE_M20_1", this parses [major=20, minor=1].
    */
  explicit SwVersion(const std::string& ver);

  // Version number comparison
  // If major and minor versions are both zero (i.e. not parsed) for *both*
  // objects, then this will fall back to a string comparison on `fullVersion`.
  /** \{ */
  inline bool operator<(const SwVersion& v) const {
    if (majorVersion == 0 && minorVersion == 0 &&
        v.majorVersion == 0 && v.minorVersion == 0) {
      return fullVersion < v.fullVersion;
    } else {
      if (majorVersion != v.majorVersion) {
        return majorVersion < v.majorVersion;
      }
      return minorVersion < v.minorVersion;
    }
  }
  inline bool operator==(const SwVersion& v) const {
    if (majorVersion == 0 && minorVersion == 0 &&
        v.majorVersion == 0 && v.minorVersion == 0) {
      return fullVersion == v.fullVersion;
    } else {
      return (majorVersion == v.majorVersion) &&
             (minorVersion == v.minorVersion);
    }
  }
  inline bool operator>(const SwVersion& v) const {
    return v < *this;
  }
  inline bool operator>=(const SwVersion& v) const {
    return !(*this < v);
  }
  inline bool operator <=(const SwVersion& v) const {
    return !(*this > v);
  }
  /** \} */

  /** The original version string (minus leading/trailing whitespace). */
  std::string fullVersion;
  /** The extracted version substring (see above). */
  std::string version;
  /**
    * The tokens from the extracted version substring (delimited by '_' / '-').
    */
  std::vector<std::string> tokens;

  /** The major version, or 0 if not parsed. */
  size_t majorVersion{0};
  /** The minor version, or 0 if not parsed. */
  size_t minorVersion{0};
};

/** Firmware version information. */
class FwVersion {
 public:
  /** Empty constructor. */
  FwVersion() {}

  /**
    * Construct FwVersion by parsing a firmware version string.
    *
    * Major/minor versions are parsed when version string body is of form:
    *    "M.M.M.m"
    *
    * For example, given "10.6.0.1", this parses [major="10.6.0", minor=1].
    *
    * Any other input formats will result in [major={input string}, minor=0].
    */
  explicit FwVersion(const std::string& ver);

  /** The original version string. */
  std::string fullVersion;

  /** The major version. */
  std::string majorVersion{""};
  /** The minor version, or 0 if not parsed. */
  size_t minorVersion{0};
};

} // namespace terragraph
} // namespace facebook
