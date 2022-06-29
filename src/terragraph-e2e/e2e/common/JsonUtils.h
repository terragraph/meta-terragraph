/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/FileUtil.h>
#include <folly/dynamic.h>
#include <folly/json.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

namespace facebook {
namespace terragraph {

/**
 * JSON-related utilities.
 */
class JsonUtils {
 public:
  /**
   * Sort and pretty-print a JSON string.
   *
   * Throws std::invalid_argument if unable to parse the input string.
   */
  static std::string toSortedPrettyJson(const std::string& jsonString);

  /**
   * Sort and pretty-print a folly::dynamic object.
   *
   * Throws std::invalid_argument if unable to parse the input object.
   */
  static std::string toSortedPrettyJson(const folly::dynamic& object);

  /**
   * Read a JSON file and return its contents as a string.
   *
   * Throws std::invalid_argument if unable to read the file.
   */
  static std::string readJsonFile2String(const std::string& fileName);

  /**
   * Read a JSON file and parse its content into a folly::dynamic object.
   *
   * Throws std::invalid_argument if unable to read the file or parse its
   * content.
   */
  static folly::dynamic readJsonFile2DynamicObject(const std::string& fileName);

  /**
   * Write a JSON string to a file.
   *
   * Throws std::invalid_argument if unable to write to the file or parse the
   * input string.
   */
  static void writeString2JsonFile(
      const std::string& jsonStr, const std::string& fileName);

  /**
   * Write a folly::dynamic object to a file as JSON.
   *
   * Throws std::invalid_argument if unable to write to the file or parse the
   * object.
   */
  static void writeDynamicObject2JsonFile(
      const folly::dynamic& object, const std::string& fileName);

  /**
   * Write a Thrift object to a file as JSON.
   *
   * Throws std::invalid_argument if unable to write to the file or serialize
   * the object.
   */
  template <class T>
  static void
  writeObject2JsonFile(const T& object, const std::string& fileName) {
    std::string contents;
    apache::thrift::SimpleJSONSerializer jsonSerializer;
    try {
      jsonSerializer.serialize(object, &contents);
    } catch (const std::exception& ex) {
      throw std::invalid_argument("Could not serialize object");
    }

    if (!folly::writeFile(toSortedPrettyJson(contents), fileName.c_str())) {
      LOG(ERROR) << "Could not write to file " << fileName;
      throw std::invalid_argument(
          folly::sformat("Could not write to file {}", fileName));
    }
  }

  /** Merge items from a folly::dynamic object "b" into "a". */
  static void dynamicObjectMerge(folly::dynamic& a, const folly::dynamic& b);

  /**
   * Merge items from a folly::dynamic object "b" into "a" without overwriting
   * any keys in "a".
   */
  static void dynamicObjectMergeAppend(
      folly::dynamic& a, const folly::dynamic& b);

  /** Clean items with empty objects from an input folly::dynamic object. */
  static folly::dynamic dynamicObjectClean(const folly::dynamic& dirtyObj);

  /**
   * Returns the difference between folly::dynamic objects "a" and "b".
   *
   * This only iterates through b's keys, and returns b's values.
   */
  static folly::dynamic dynamicObjectDifference(
      const folly::dynamic& a, const folly::dynamic& b);

  /**
   * Returns the full difference between folly::dynamic objects "a" and "b".
   *
   * This returns a's values for conflicting keys.
   */
  static folly::dynamic dynamicObjectFullDifference(
      const folly::dynamic& a, const folly::dynamic& b);

  /** Returns an escaped string according to RFC 6901 ("JSON Pointer"). */
  static std::string jsonPointerEscape(const std::string& s);

  /** Returns an unescaped string according to RFC 6901 ("JSON Pointer"). */
  static std::string jsonPointerUnescape(const std::string& s);

  /**
   * Returns whether the given folly::dynamic object contains a value at a
   * JSON Pointer address.
   */
  static bool objectContains(
      const folly::dynamic& obj, const std::string& jsonPtr);

  /** Serialize the given Thrift structure to a JSON string. */
  template <class T>
  static std::string serializeToJson(const T& obj) {
    return apache::thrift::SimpleJSONSerializer::serialize<std::string>(obj);
  }

  /** Deserialize the given Thrift structure from a JSON string. */
  template <class T>
  static std::optional<T> deserializeFromJson(const std::string& s) {
    try {
      return apache::thrift::SimpleJSONSerializer::deserialize<T>(s);
    } catch (const std::exception& ex) {
      return std::nullopt;
    }
  }

 private:
  /** Perform an in-place find-and-replace within a string. */
  static void replace(
      std::string& s, const std::string& find, const std::string& replace);
};

} // namespace terragraph
} // namespace facebook
