/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "JsonUtils.h"

#include <folly/json_pointer.h>

namespace facebook {
namespace terragraph {

std::string
JsonUtils::toSortedPrettyJson(const std::string& jsonString) {
  std::string formattedJson;
  folly::json::serialization_opts opts;
  opts.sort_keys = true;
  opts.pretty_formatting = true;

  try {
    formattedJson =
        folly::json::serialize(folly::parseJson(jsonString), opts);
  } catch (const std::exception& ex) {
    throw std::invalid_argument("Could not parse json string");
  }

  return formattedJson;
}

std::string
JsonUtils::toSortedPrettyJson(const folly::dynamic& object) {
  std::string formattedJson;
  folly::json::serialization_opts opts;
  opts.sort_keys = true;
  opts.pretty_formatting = true;

  try {
    formattedJson = folly::json::serialize(object, opts);
  } catch (const std::exception& ex) {
    throw std::invalid_argument("Could not parse dynamic object");
  }

  return formattedJson;
}

std::string
JsonUtils::readJsonFile2String(const std::string& fileName) {
  std::string contents;
  if (!folly::readFile(fileName.c_str(), contents)) {
    throw std::invalid_argument(
        folly::sformat("Could not read file {}", fileName));
  }
  return contents;
}

folly::dynamic
JsonUtils::readJsonFile2DynamicObject(const std::string& fileName) {
  folly::dynamic object;
  auto contents = readJsonFile2String(fileName);
  try {
    object = folly::parseJson(contents);
  } catch (const std::exception& ex) {
    throw std::invalid_argument("Could not parse file struct");
  }
  return object;
}

void
JsonUtils::writeString2JsonFile(
    const std::string& jsonStr, const std::string& fileName) {
  if (!folly::writeFile(toSortedPrettyJson(jsonStr), fileName.c_str())) {
    throw std::invalid_argument(
        folly::sformat("Could not write to file {}", fileName));
  }
}

void
JsonUtils::writeDynamicObject2JsonFile(
    const folly::dynamic& object, const std::string& fileName) {
  if (!folly::writeFile(toSortedPrettyJson(object), fileName.c_str())) {
    throw std::invalid_argument(
        folly::sformat("Could not write to file {}", fileName));
  }
}

void
JsonUtils::dynamicObjectMerge(folly::dynamic& a, const folly::dynamic& b) {
  if (!a.isObject() || !b.isObject()) {
    return;
  }
  for (const auto& bPair : b.items()) {
    auto aPair = a.find(bPair.first);
    if (aPair != a.items().end() && aPair->second.isObject()) {
      dynamicObjectMerge(aPair->second, bPair.second);
    } else {
      a[bPair.first] = bPair.second;
    }
  }
}

void
JsonUtils::dynamicObjectMergeAppend(
    folly::dynamic& a, const folly::dynamic& b) {
  if (!a.isObject() || !b.isObject()) {
    return;
  }
  for (const auto& bPair : b.items()) {
    auto aPair = a.find(bPair.first);
    if (aPair != a.items().end()) {
      if (aPair->second.isObject()) {
        dynamicObjectMergeAppend(aPair->second, bPair.second);
      }
    } else {
      a[bPair.first] = bPair.second;
    }
  }
}

folly::dynamic
JsonUtils::dynamicObjectDifference(
    const folly::dynamic& a, const folly::dynamic& b) {
  folly::dynamic result = folly::dynamic::object;
  if (!a.isObject() || !b.isObject()) {
    return result;
  }
  for (const auto& bPair : b.items()) {
    std::string key = bPair.first.asString();
    auto aPair = a.find(key);
    if (aPair == a.items().end()) {
      result[key] = bPair.second;
    } else if (aPair->second.isObject()) {
      folly::dynamic obj = dynamicObjectDifference(aPair->second, bPair.second);
      if (!obj.empty()) {
        result[key] = obj;
      }
    } else if (aPair->second != bPair.second) {
      result[key] = bPair.second;
    }
  }
  return result;
}

folly::dynamic
JsonUtils::dynamicObjectFullDifference(
    const folly::dynamic& a, const folly::dynamic& b) {
  folly::dynamic aDiff = dynamicObjectDifference(a, b);
  folly::dynamic bDiff = dynamicObjectDifference(b, a);
  dynamicObjectMerge(aDiff, bDiff);
  return aDiff;
}

folly::dynamic
JsonUtils::dynamicObjectClean(const folly::dynamic& dirtyObj) {
  folly::dynamic cleanObj = folly::dynamic::object;
  if (dirtyObj.empty()) {
    return cleanObj;
  }

  for (const auto& pair : dirtyObj.items()) {
    std::string key = pair.first.asString();
    folly::dynamic val = pair.second;
    if (val.isObject()) {
      auto cleanValue = dynamicObjectClean(val);
      if (!cleanValue.empty()) {
        cleanObj[key] = cleanValue;
      }
    } else {
      cleanObj[key] = val;
    }
  }

  return cleanObj;
}

std::string
JsonUtils::jsonPointerEscape(const std::string& s) {
  std::string escaped;
  for (const char& c : s) {
    switch (c) {
      case '~':
        escaped += "~0";
        break;
      case '/':
        escaped += "~1";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

std::string
JsonUtils::jsonPointerUnescape(const std::string& s) {
  std::string unescaped = s;
  replace(unescaped, "~1", "/");
  replace(unescaped, "~0", "~");
  return unescaped;
}

void
JsonUtils::replace(
    std::string& s, const std::string& find, const std::string& replace) {
  size_t pos = 0;
  while ((pos = s.find(find, pos)) != std::string::npos) {
    s.replace(pos, find.length(), replace);
    pos += replace.length();
  }
}

bool
JsonUtils::objectContains(
    const folly::dynamic& obj, const std::string& jsonPtr) {
  try {
    return (obj.get_ptr(folly::json_pointer::parse(jsonPtr)) != nullptr);
  } catch (const std::exception& ex) {
    return false;
  }
}

} // namespace terragraph
} // namespace facebook
