/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Type-safe time units for Google Flags.
 *
 * One can do:
 *
 *   DEFINE_time_ms(timeout, 300_ms, "timeout for Foo");
 *
 * and then:
 *
 *   int main(void) {
 *     auto server = std::make_shared<ThriftServer>();
 *     server->setTaskExpireTime(FLAGS_timeout_ms);
 *   }
 *
 * Flags are set from command line like this:
 *   $ my_service --timeout=250ms
 *
 * It prevents from losing precision e.g. you can't assign nanoseconds
 * to seconds. For default value it does that at compile time, for values
 * set from command line it does that at initialization.
 *
 *
 * IMPLEMENTATION DETAILS:
 *
 * It's just a simple wrapper around DEFINE_string that registers a validator
 * that checks if the parameter is a valid duration value.
 * This approach has two quirks:
 *
 *   * help message reports "string" as the type of the parameter
 *   * global variable is named FLAGS_name_suffix e.g. FLAGS_timeout_ms
 *     instead of the usual FLAGS_name.
 *     FLAGS_name is still available and it's just the unparsed string.
 *
 * @author Bartosz Nitka (bnitka@fb.com)
 */

#pragma once

#include <chrono>
#include <gflags/gflags.h>
#include <string>

#include <folly/Conv.h>

#include "time/TimeLiterals.h"

#define DEFINE_time_impl(name, def, suffix, type, help)                        \
  DEFINE_string(name, #def, help);                                             \
                                                                               \
  namespace chrono_flags_secret {                                              \
  using namespace ::facebook::si_units::literals;                              \
  ::std::chrono::type FLAGS_##name##_##suffix(def);                            \
  }                                                                            \
  using chrono_flags_secret::FLAGS_##name##_##suffix;                          \
                                                                               \
  namespace {                                                                  \
  typedef ::std::integral_constant<char, '_'> time_unit_delimiter;             \
  static bool validate_##name##_##suffix(                                      \
      const char* /*flagname*/, const ::std::string& v) {                      \
    auto delimiter = v.find_first_not_of("0123456789");                        \
    if (delimiter == ::std::string::npos) {                                    \
      return false;                                                            \
    }                                                                          \
    auto suffixStart = v.data() + delimiter;                                   \
    if (v[delimiter] == time_unit_delimiter::value) {                          \
      if (delimiter + 1 == v.size()) {                                         \
        return false;                                                          \
      }                                                                        \
      ++suffixStart;                                                           \
    }                                                                          \
                                                                               \
    try {                                                                      \
      FLAGS_##name##_##suffix = ::facebook::parse_time_unit<                   \
          ::facebook::safe_time::lossless,                                     \
          ::std::chrono::type>(                                                \
          ::folly::to<::std::intmax_t>(                                        \
              ::folly::StringPiece(v.data(), v.data() + delimiter)),           \
          ::folly::StringPiece(suffixStart, v.data() + v.size()));             \
      return true;                                                             \
    } catch (::std::exception const&) {                                        \
      return false;                                                            \
    }                                                                          \
  }                                                                            \
                                                                               \
  DEFINE_validator(name, validate_##name##_##suffix);                          \
  }

#define DEFINE_time_ns(name, def, help) \
  DEFINE_time_impl(name, def, ns, nanoseconds, help);

#define DEFINE_time_us(name, def, help) \
  DEFINE_time_impl(name, def, us, microseconds, help);

#define DEFINE_time_ms(name, def, help) \
  DEFINE_time_impl(name, def, ms, milliseconds, help);

#define DEFINE_time_s(name, def, help) \
  DEFINE_time_impl(name, def, s, seconds, help);

#define DEFINE_time_min(name, def, help) \
  DEFINE_time_impl(name, def, min, minutes, help);

#define DEFINE_time_h(name, def, help) \
  DEFINE_time_impl(name, def, h, hours, help);

#define DECLARE_time_impl(name, suffix, type)       \
  namespace chrono_flags_secret {                   \
  extern std::chrono::type FLAGS_##name##_##suffix; \
  }                                                 \
  using chrono_flags_secret::FLAGS_##name##_##suffix;

#define DECLARE_time_ns(name) DECLARE_time_impl(name, ns, nanoseconds);

#define DECLARE_time_us(name) DECLARE_time_impl(name, us, microseconds);

#define DECLARE_time_ms(name) DECLARE_time_impl(name, ms, milliseconds);

#define DECLARE_time_s(name) DECLARE_time_impl(name, s, seconds);

#define DECLARE_time_min(name) DECLARE_time_impl(name, min, minutes);

#define DECLARE_time_h(name) DECLARE_time_impl(name, h, hours);
