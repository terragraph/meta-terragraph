/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*
 * Time literals for units like milliseconds or nanoseconds.
 *
 * One can  do, for instance:
 *  auto delay = 100_ms;
 * And decltype(delay) will be std::chrono::milliseconds.
 *
 * The time literals are implicitly imported into namespace facebook and
 * facebook::si_units. Should you need only the time literals but none of the
 * other stuff in those namespaces, just import facebook::si_units::literals:
 *
 *  using namespace facebook::si_units::literals;
 *
 * It also creates a shortcut to std::chrono types in the facebook::si_units
 * namespace, using the proper International System of Units symbol so instead
 * of typing
 *
 *  std::chrono::milliseconds
 *
 * one can simply type
 *
 *  si_units::ms
 *
 * Why SI and not IS for International System of Units?
 * from http://en.wikipedia.org/wiki/International_System_of_Units:
 *  "abbreviated SI from French: Le Systeme international d'unites"
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */

#pragma once

#include <folly/String.h>

#include <chrono>
#include <ratio>
#include <stdexcept>

namespace facebook {

#define FB_CREATE_DURATION_LITERAL_OPERATOR(type, suffix)  \
  namespace si_units {                                     \
  namespace literals {                                     \
  inline constexpr std::chrono::type operator"" _##suffix( \
      unsigned long long n) {                              \
    return std::chrono::type{n};                           \
  }                                                        \
  }                                                        \
  typedef std::chrono::type suffix;                        \
  using literals::operator"" _##suffix;                    \
  }                                                        \
  using si_units::literals::operator"" _##suffix;

#define FB_CHRONO_PICOSECONDS duration<std::intmax_t, std::pico>
FB_CREATE_DURATION_LITERAL_OPERATOR(FB_CHRONO_PICOSECONDS, ps);
#undef FB_CHRONO_PICOSECONDS
FB_CREATE_DURATION_LITERAL_OPERATOR(nanoseconds, ns);
FB_CREATE_DURATION_LITERAL_OPERATOR(microseconds, us);
FB_CREATE_DURATION_LITERAL_OPERATOR(milliseconds, ms);
FB_CREATE_DURATION_LITERAL_OPERATOR(seconds, s);
FB_CREATE_DURATION_LITERAL_OPERATOR(minutes, min);
FB_CREATE_DURATION_LITERAL_OPERATOR(hours, h);

#undef FB_CREATE_DURATION_LITERAL_OPERATOR

namespace detail {

template <typename TRatio>
struct get_ratio_symbol_impl {
  static char const*
  get() {
    return nullptr;
  }
};

#define FB_GET_RATIO_SYMBOL_IMPL(p, s)   \
  template <>                            \
  struct get_ratio_symbol_impl<std::p> { \
    static char const*                   \
    get() {                              \
      return s;                          \
    }                                    \
  }

/* these should be defined in <ratio>, but it seems like they aren't
FB_GET_RATIO_SYMBOL_IMPL(yotta, "Y");
FB_GET_RATIO_SYMBOL_IMPL(zetta, "Z");
*/
FB_GET_RATIO_SYMBOL_IMPL(exa, "E");
FB_GET_RATIO_SYMBOL_IMPL(peta, "P");
FB_GET_RATIO_SYMBOL_IMPL(tera, "T");
FB_GET_RATIO_SYMBOL_IMPL(giga, "G");
FB_GET_RATIO_SYMBOL_IMPL(mega, "M");
FB_GET_RATIO_SYMBOL_IMPL(kilo, "k");
FB_GET_RATIO_SYMBOL_IMPL(hecto, "h");
FB_GET_RATIO_SYMBOL_IMPL(deca, "da");
FB_GET_RATIO_SYMBOL_IMPL(ratio<1>, "");
FB_GET_RATIO_SYMBOL_IMPL(deci, "d");
FB_GET_RATIO_SYMBOL_IMPL(centi, "c");
FB_GET_RATIO_SYMBOL_IMPL(milli, "m");
FB_GET_RATIO_SYMBOL_IMPL(micro, "\u00B5");
FB_GET_RATIO_SYMBOL_IMPL(nano, "n");
FB_GET_RATIO_SYMBOL_IMPL(pico, "p");
FB_GET_RATIO_SYMBOL_IMPL(femto, "f");
FB_GET_RATIO_SYMBOL_IMPL(atto, "a");
/* these should be defined in <ratio>, but it seems like they aren't
FB_GET_RATIO_SYMBOL_IMPL(zepto, "z");
FB_GET_RATIO_SYMBOL_IMPL(yocto, "y");
*/
#undef FB_GET_RATIO_SYMBOL_IMPL

template <typename TRatio>
struct get_time_suffix_impl {
  static char const*
  get() {
    return nullptr;
  }
};

#define FB_GET_TIME_SUFFIX_IMPL(p, s)   \
  template <>                           \
  struct get_time_suffix_impl<std::p> { \
    static char const*                  \
    get() {                             \
      return s;                         \
    }                                   \
  }

/* these should be defined in <ratio>, but it seems like they aren't
FB_GET_TIME_SUFFIX_IMPL(yotta, "Ys");
FB_GET_TIME_SUFFIX_IMPL(zetta, "Zs");
*/
FB_GET_TIME_SUFFIX_IMPL(exa, "Es");
FB_GET_TIME_SUFFIX_IMPL(peta, "Ps");
FB_GET_TIME_SUFFIX_IMPL(tera, "Ts");
FB_GET_TIME_SUFFIX_IMPL(giga, "Gs");
FB_GET_TIME_SUFFIX_IMPL(mega, "Ms");
FB_GET_TIME_SUFFIX_IMPL(chrono::hours::period, "h");
FB_GET_TIME_SUFFIX_IMPL(kilo, "ks");
FB_GET_TIME_SUFFIX_IMPL(hecto, "hs");
FB_GET_TIME_SUFFIX_IMPL(chrono::minutes::period, "min");
FB_GET_TIME_SUFFIX_IMPL(deca, "das");
FB_GET_TIME_SUFFIX_IMPL(chrono::seconds::period, "s");
FB_GET_TIME_SUFFIX_IMPL(deci, "ds");
FB_GET_TIME_SUFFIX_IMPL(centi, "cs");
FB_GET_TIME_SUFFIX_IMPL(milli, "ms");
FB_GET_TIME_SUFFIX_IMPL(micro, "\u00B5s");
FB_GET_TIME_SUFFIX_IMPL(nano, "ns");
FB_GET_TIME_SUFFIX_IMPL(pico, "ps");
FB_GET_TIME_SUFFIX_IMPL(femto, "fs");
FB_GET_TIME_SUFFIX_IMPL(atto, "as");
/* these should be defined in <ratio>, but it seems like they aren't
FB_GET_TIME_SUFFIX_IMPL(zepto, "zs");
FB_GET_TIME_SUFFIX_IMPL(yocto, "ys");
*/

#undef FB_GET_TIME_SUFFIX_IMPL

} // namespace detail {

/**
 * Returns the SI symbol for the given ratio, or nullptr if the ratio
 * is unknown.
 *
 * E.g.:
 *  'm' for a ratio of 1:1000
 *  'k' for a ratio of 1000:1
 *  'M' for a ratio of 1000000:1
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <typename TRatio>
inline char const*
get_ratio_symbol() {
  return detail::get_ratio_symbol_impl<TRatio>::get();
}

/**
 * Returns the SI suffix for the given duration, or nullptr if the duration
 * is unknown.
 *
 * E.g.:
 *  'ms' for std::chrono::milliseconds or equivalent (same ratio)
 *  's' for std::chrono::seconds or equivalent (same ratio)
 *  'min' for std::chrono::minutes or equivalent (same ratio)
 *  'h' for std::chrono::hours or equivalent (same ratio)
 *
 * Example:
 *
 *  cout << 30 << get_time_suffix<std::chrono::seconds>();
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <typename TDuration>
inline char const*
get_time_suffix() {
  return detail::get_time_suffix_impl<typename TDuration::period>::get();
}

/**
 * Returns the SI suffix for the given duration, or nullptr if the duration
 * is unknown. See get_time_suffix() above for details.
 *
 * Example:
 *
 *  std::chrono::seconds s(30);
 *  cout << s.count() << get_time_suffix(s);
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <typename TRep, typename TPeriod>
inline char const*
get_time_suffix(std::chrono::duration<TRep, TPeriod> const&) {
  return get_time_suffix<std::chrono::duration<TRep, TPeriod>>();
}

/**
 * Returns the SI suffix for the given time_point's duration, or nullptr
 * if the duration is unknown. See get_time_suffix() above for details.
 *
 * Example:
 *
 *  auto now = std::chrono::system_clock::now();
 *  cout << now.time_since_epoch().count() << get_time_suffix(now);
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <typename TClock, typename TDuration>
inline char const*
get_time_suffix(std::chrono::time_point<TClock, TDuration> const&) {
  return get_time_suffix<TDuration>();
}

/**
 * The result type for parse_time_unit.
 *
 * Using a struct with a regular enum instead of an enum class
 * so that we achieve the same namespace scoping, without losing the
 * implicit conversion to bool.
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
struct parse_time_unit_result {
  enum type {
    // explicitly guaranteeing it to be 0 to allow a sane conversion to bool
    success = 0,
    unknown_unit,
    precision_loss
  };
};

namespace safe_time {

enum conversion_precision { lossy, lossless };

} // namespace time {

/**
 * Parses the `unit` string and properly interprets the given
 * `value` as a std::chrono::duration of that unit.
 *
 * Sets `out` with such value, properly converted. `out` remains untouched
 * unless this function succeeds.
 *
 * Returns `parse_time_unit_result::type` which is an enum telling whether the
 * parsing was successful or not, or the error detected while parsing.
 * The result is implicitly convertible to bool where `false` means success.
 *
 * Precision loss will only be checked if `Precision` is `lossless`.
 *
 * Example:
 *  Say you have the string "1 ms" properly split in a vector v = {"1", "ms"}
 *  and you want this string parsed into std::chrono::nanoseconds:
 *
 *    DCHECK(v.size() == 2);
 *    auto value = folly::to<std::intmax_t>(v[0]);
 *    std::chrono::nanoseconds result;
 *    auto error = parse_time_unit<safe_time::lossless>(result, value, v[2]);
 *    if (error) {
 *      // handle error
 *    }
 *    // `value` was successfully parsed
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <
    safe_time::conversion_precision Precision,
    typename TDuration,
    typename TValue>
parse_time_unit_result::type
parse_time_unit(
    TDuration& out, TValue&& value, folly::StringPiece const& unit) {
#define RETURN_CONVERTED_TIME(Duration)                                        \
  do {                                                                         \
    typedef typename std::decay<TDuration>::type to_t;                         \
    typedef std::chrono::Duration from_t;                                      \
    if (Precision == safe_time::lossless &&                                    \
        !std::is_constructible<to_t, from_t>::value) {                         \
      return parse_time_unit_result::precision_loss;                           \
    }                                                                          \
    out =                                                                      \
        std::chrono::duration_cast<to_t>(from_t{std::forward<TValue>(value)}); \
    return parse_time_unit_result::success;                                    \
  } while (false)

#define PARSE_UNIT_LAST_CHAR(Duration, Char, Size)       \
  do {                                                   \
    if (unit.size() == Size && unit[Size - 1] == Char) { \
      RETURN_CONVERTED_TIME(Duration);                   \
    }                                                    \
    return parse_time_unit_result::unknown_unit;         \
  } while (false)

#define PARSE_FULL_UNIT(Duration, Size)          \
  do {                                           \
    if (unit.size() == Size) {                   \
      RETURN_CONVERTED_TIME(Duration);           \
    }                                            \
    return parse_time_unit_result::unknown_unit; \
  } while (false)

  if (!unit.empty()) {
    switch (unit[0]) {
      case 'n':
        PARSE_UNIT_LAST_CHAR(nanoseconds, 's', 2);
      case 'u':
        PARSE_UNIT_LAST_CHAR(microseconds, 's', 2);
      case 'm':
        if (unit.size() > 1) {
          switch (unit[1]) {
            case 's':
              PARSE_FULL_UNIT(milliseconds, 2);
            case 'i':
              PARSE_UNIT_LAST_CHAR(minutes, 'n', 3);
          }
        }
        break;
      case 's':
        PARSE_FULL_UNIT(seconds, 1);
      case 'h':
        PARSE_FULL_UNIT(hours, 1);
    }
  }

  return parse_time_unit_result::unknown_unit;

#undef PARSE_FULL_UNIT
#undef PARSE_UNIT_LAST_CHAR
#undef RETURN_CONVERTED_TIME
}

/**
 * Parses the `unit` string and properly interprets the given
 * `value` as a std::chrono::duration of that unit.
 *
 * Returns such value, properly converted to the requested `TDuration`.
 *
 * Throws std::invalid_argument if the given unit string is unknown.
 *
 * Precision loss will only be checked if `Precision` is `lossless`.
 *
 * Example:
 *  Say you have the string "1 ms" properly split in a vector v = {"1", "ms"}
 *  and you want this string parsed into std::chrono::nanoseconds:
 *
 *    using namespace std::chrono;
 *    DCHECK(v.size() == 2);
 *    auto result = parse_time_unit<safe_time::lossless, nanoseconds>(
 *      folly::to<std::intmax_t>(v[0]),
 *      v[1]
 *    );
 *
 * @author Marcelo Juchem (marcelo@fb.com)
 */
template <
    safe_time::conversion_precision Precision,
    typename TDuration,
    typename TValue>
TDuration
parse_time_unit(TValue&& value, folly::StringPiece const& unit) {
  TDuration out;
  auto result =
      parse_time_unit<Precision>(out, std::forward<TValue>(value), unit);

  switch (result) {
    case parse_time_unit_result::success:
      return out;

    case parse_time_unit_result::unknown_unit:
      throw std::invalid_argument(
          folly::to<std::string>("unknown unit: ", unit));

    case parse_time_unit_result::precision_loss:
      throw std::underflow_error("cannot convert without precision loss");
  }

  CHECK(!"this should be dead code");
  return out; // Pacify compiler.
}

} // namespace facebook {
