/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigMetadata.h"

#include <algorithm>
#include <folly/Conv.h>
#include <folly/json_pointer.h>
#include <folly/String.h>
#include <regex>

#include "JsonUtils.h"

using std::string;
using apache::thrift::detail::TEnumMapFactory;

DEFINE_string(
    node_config_metadata_file,
    "/etc/e2e_config/config_metadata.json",
    "Node config metadata file");

DEFINE_string(
    controller_config_metadata_file,
    "/etc/e2e_config/controller_config_metadata.json",
    "Controller config metadata file");

DEFINE_string(
    aggregator_config_metadata_file,
    "/etc/stats_config/aggregator_config_metadata.json",
    "Aggregator config metadata file");

namespace {
  // "copy-block" preprocessor macro key
  const string kPreprocessorCopyBlockMarker{"__copy_block__"};
}

namespace facebook {
namespace terragraph {

ConfigMetadata::ConfigMetadata(
    const std::string& metadataFile,
    bool keepFullMetadata,
    bool hasFlags,
    bool validateCfgAction)
    : keepFullMetadata_(keepFullMetadata),
      hasFlags_(hasFlags),
      validateCfgAction_(validateCfgAction) {
  // Read file
  folly::dynamic configMeta = folly::dynamic::object;
  try {
    configMeta = JsonUtils::readJsonFile2DynamicObject(metadataFile);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Unable to read config metadata file " << metadataFile
               << ": " << folly::exceptionStr(ex);
    throw std::invalid_argument("Unable to read config metadata file");
  }

  loadConfigMetadata(configMeta);
}

ConfigMetadata::ConfigMetadata(
    const folly::dynamic& configMeta,
    bool keepFullMetadata,
    bool hasFlags,
    bool validateCfgAction)
    : keepFullMetadata_(keepFullMetadata),
      hasFlags_(hasFlags),
      validateCfgAction_(validateCfgAction) {
  loadConfigMetadata(configMeta);
}

void
ConfigMetadata::loadConfigMetadata(const folly::dynamic& configMeta) {
  // Preprocess the config metadata JSON (recursively)
  folly::dynamic preprocessedConfigMeta = configMeta;
  try {
    std::vector<string> keys;
    std::vector<string> copyBlockExpansions;
    preprocessConfigMetadata(
        configMeta, preprocessedConfigMeta, keys, copyBlockExpansions);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Preprocessing config metadata failed: "
               << folly::exceptionStr(ex);
    throw std::invalid_argument(
        "Preprocessing config metadata failed: " +
        folly::exceptionStr(ex).toStdString());
  }

  // Inject flags defaults
  if (hasFlags_) {
    setGFlagsDefaultValues(preprocessedConfigMeta);
  }

  // Parse JSON to CfgParamMetadata structs (recursively)
  try {
    parseConfigMetadata(preprocessedConfigMeta, configMetaIndexMap_);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Parsing config metadata failed: " << folly::exceptionStr(ex);
    throw std::invalid_argument(
        "Parsing config metadata failed: " +
        folly::exceptionStr(ex).toStdString());
  }

  if (keepFullMetadata_) {
    configMeta_ = std::move(preprocessedConfigMeta);
  }
}

void
ConfigMetadata::preprocessConfigMetadata(
    const folly::dynamic& configMeta,
    folly::dynamic& preprocessedObj,
    std::vector<string>& keys,
    std::vector<string>& copyBlockExpansions) {
  for (auto& kv : preprocessedObj.items()) {
    string key = kv.first.asString();
    folly::dynamic& val = kv.second;

    keys.push_back(key);
    if (val.isObject()) {
      // Expand copy-block macros repeatedly
      // This records macros expanded in copyBlockExpansions, to avoid infinite
      // looping on circular references
      int initialSize = copyBlockExpansions.size();
      while (preprocessCopyBlock(configMeta, val, keys, copyBlockExpansions));

      // Look one level deeper in the value JSON (recursively)
      preprocessConfigMetadata(configMeta, val, keys, copyBlockExpansions);

      // Remove all expanded copy-block keys we added to copyBlockExpansions,
      // since they can legitimately appear elsewhere in the JSON
      copyBlockExpansions.resize(initialSize);
    }
    keys.pop_back();
  }
}

bool
ConfigMetadata::preprocessCopyBlock(
    const folly::dynamic& configMeta,
    folly::dynamic& obj,
    const std::vector<string>& keys,
    std::vector<string>& copyBlockExpansions) {
  // Find copy-block marker
  auto copyBlock = obj.find(kPreprocessorCopyBlockMarker);
  if (copyBlock == obj.items().end()) {
    return false;
  }

  // Basic checks
  string fullKey = toFullKey(keys);
  if (!copyBlock->second.isString()) {
    throw std::invalid_argument(
        "Bad copy-block macro value for config metadata key " + fullKey +
        " (expected string type but found " +
        string(copyBlock->second.typeName()) + ")");
  }

  // Circular reference check
  string macro = copyBlock->second.getString();
  auto iter =
      std::find(copyBlockExpansions.begin(), copyBlockExpansions.end(), macro);
  if (iter != copyBlockExpansions.end()) {
    throw std::invalid_argument(
        "Found circular copy-block macro '" + macro +
        "' for config metadata key " + fullKey);
  }

  // Convert value to JSON pointer (used to index into full config metadata)
  auto copyKeyPtr = folly::json_pointer::try_parse(toJsonPointer(macro));
  if (copyKeyPtr.hasError()) {  // shouldn't happen...
    throw std::invalid_argument(
        "Malformed copy-block source '" + macro + "' for config metadata key " +
        fullKey);
  }

  // Copy value at copy-block key
  const folly::dynamic *configMetaPtr;
  try {
    configMetaPtr = configMeta.get_ptr(copyKeyPtr.value());
  } catch (const std::exception& ex) {
    // get_ptr: "Throws if pointer does not match the shape of the document,
    // e.g. uses string to index in array."
    throw std::invalid_argument(
        "Invalid copy-block source '" + macro + "' for config metadata key " +
        fullKey);
  }
  if (configMetaPtr == nullptr) {
    // get_ptr: "Returns nullptr if element could not be located."
    throw std::invalid_argument(
        "Unknown copy-block source '" + macro + "' for config metadata key " +
        fullKey);
  }
  folly::dynamic result = *configMetaPtr;

  // Add any "override" properties from original block into copied block
  obj.erase(kPreprocessorCopyBlockMarker);
  if (!obj.empty()) {
    for (const auto& kv : obj.items()) {
      result[kv.first.asString()] = kv.second;
    }
  }

  obj = result;
  copyBlockExpansions.push_back(macro);

  return true;
}

void
ConfigMetadata::parseConfigMetadata(
    const folly::dynamic& obj, folly::dynamic& indexMap) {
  for (const auto& kv : obj.items()) {
    string key = kv.first.asString();
    folly::dynamic val = kv.second;

    // If we hit a non-object value here, the original object was malformed
    if (!val.isObject()) {
      throw std::invalid_argument(
        "Bad value (non-object) for config metadata key " + key);
    }

    // We identify a CfgParamMetadata block by its required fields
    if (val.count("desc") && val.count("type") && val.count("action")) {
      // Construct CfgParamMetadata struct (recursively)
      CfgParamMetadata paramMeta(val, validateCfgAction_);

      // Store references to the struct
      configMetaArray_.push_back(std::move(paramMeta));
      indexMap[key] = configMetaArray_.size() - 1;
    } else {
      // Look one level deeper in the value JSON (recursively)
      folly::dynamic& map = indexMap.setDefault(key, folly::dynamic::object);
      parseConfigMetadata(val, map);
    }
  }
}

folly::dynamic
ConfigMetadata::get() {
  return keepFullMetadata_ ? configMeta_ : folly::dynamic::object;
}

std::unordered_map<thrift::CfgAction, std::vector<std::string>>
ConfigMetadata::getActions(const folly::dynamic& config) {
  // Recursively find actions in the config
  std::unordered_map<thrift::CfgAction, std::vector<std::string>> actions;
  std::vector<string> keys;
  getActions(config, actions, keys, configMetaIndexMap_);
  actions.erase(thrift::CfgAction::NO_ACTION);  // remove null action
  return actions;
}

void
ConfigMetadata::getActions(
    const folly::dynamic& config,
    std::unordered_map<thrift::CfgAction, std::vector<std::string>>& actions,
    std::vector<std::string>& keys,
    folly::dynamic& metaMap) {
  for (const auto& kv : config.items()) {
    string key = kv.first.asString();
    folly::dynamic val = kv.second;

    keys.push_back(key);

    auto iter = metaMap.find(key);
    if (iter == metaMap.items().end()) {
      // No metadata here or further down this branch
      keys.pop_back();
      continue;
    }

    // Check if we're at the entry or need to keep recursing
    if (iter->second.isInt()) {
      // Found an entry, so record the action
      auto idx = iter->second.asInt();
      CfgParamMetadata& paramMeta = configMetaArray_.at(idx);
      actions[paramMeta.action].push_back(toFullKey(keys));
      getParamActions(val, paramMeta, actions, keys);
    } else {
      // No entry here, look one level deeper (recursively)
      if (val.isObject()) {
        getActions(val, actions, keys, metaMap.at(key));
      }
    }

    keys.pop_back();
  }
}

void
ConfigMetadata::getParamActions(
    const folly::dynamic& param,
    const CfgRecursiveParam& paramMeta,
    std::unordered_map<thrift::CfgAction, std::vector<std::string>>& actions,
    std::vector<string>& keys) {
  // Get actions based on type
  switch (paramMeta.type) {
    case thrift::CfgParamType::OBJECT: {
      if (param.isObject() && paramMeta.objVal) {
        // Check all properties recursively
        for (const auto& kv : param.items()) {
          string prop = kv.first.asString();
          auto iter = paramMeta.objVal->properties.find(prop);
          if (iter == paramMeta.objVal->properties.end()) {
            continue;  // shouldn't happen if validated first
          }
          keys.push_back(prop);
          if (iter->second->action) {
            // Record the action
            actions[*iter->second->action].push_back(toFullKey(keys));
          }
          getParamActions(kv.second, *iter->second, actions, keys);
          keys.pop_back();
        }
      }
      break;
    }
    case thrift::CfgParamType::MAP: {
      if (param.isObject() && paramMeta.mapVal) {
        // Check all values recursively
        for (const auto& kv : param.items()) {
          keys.push_back(kv.first.asString());
          getParamActions(kv.second, *paramMeta.mapVal, actions, keys);
          keys.pop_back();
        }
      }
      break;
    }
    default:
      break;
  }
}

ConfigMetadata::ValidationResult
ConfigMetadata::validate(const folly::dynamic& config) {
  // Recursively validate the config
  ValidationResult result;
  std::vector<string> keys;
  validate(config, keys, configMetaIndexMap_, result);
  return result;
}

void
ConfigMetadata::validate(
    const folly::dynamic& config,
    std::vector<std::string>& keys,
    folly::dynamic& metaMap,
    ValidationResult& result) {
  for (const auto& kv : config.items()) {
    string key = kv.first.asString();
    folly::dynamic val = kv.second;

    keys.push_back(key);

    auto iter = metaMap.find(key);
    if (iter == metaMap.items().end()) {
      // No metadata here or further down this branch
      result.unrecognizedKeys.push_back(toFullKey(keys));
      keys.pop_back();
      continue;
    }

    // Check if we're at the entry or need to keep recursing
    if (iter->second.isInt()) {
      // Found an entry, so validate the current value accordingly
      auto idx = iter->second.asInt();
      CfgParamMetadata& paramMeta = configMetaArray_.at(idx);
      if (paramMeta.readOnly) {
        result.readOnlyKeys.push_back(toFullKey(keys));
      }
      if (paramMeta.deprecated) {
        result.deprecatedKeys.push_back(toFullKey(keys));
      }
      if (!paramMeta.sync) {
        result.bstarUnsyncedKeys.push_back(toFullKey(keys));
      }
      validateParam(val, paramMeta, keys, result);
    } else {
      // No entry here, look one level deeper (recursively)
      if (val.isObject()) {
        validate(val, keys, metaMap.at(key), result);
      }
    }

    keys.pop_back();
  }
}

void
ConfigMetadata::validateParam(
    const folly::dynamic& param,
    const CfgRecursiveParam& paramMeta,
    std::vector<string>& keys,
    ValidationResult& result) {
  // Validate based on type
  switch (paramMeta.type) {
    case thrift::CfgParamType::INTEGER: {
      if (!param.isInt()) {
        throw std::invalid_argument(
            "Expected integer type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.intVal) {
        // Check against [min, max] and/or list of allowed values
        int64_t val = param.asInt();
        bool hasAllowed = !!paramMeta.intVal->allowedValues;
        bool hasRanges = !!paramMeta.intVal->allowedRanges;
        bool allowed = hasAllowed &&
            paramMeta.intVal->allowedValues->count(val);
        bool inRange = false;
        if (hasRanges) {
          for (const auto& range : *paramMeta.intVal->allowedRanges) {
            if (val >= range.min && val <= range.max) {
              inRange = true;
              break;
            }
          }
        }
        if ((hasAllowed || hasRanges) && (!allowed && !inRange)) {
          throw std::invalid_argument(
              "Invalid integer value '" + std::to_string(val) +
              "' for key: " + toFullKey(keys));
        }
      }
      break;
    }
    case thrift::CfgParamType::FLOAT: {
      if (!param.isNumber()) {  // not isDouble() since we accept int64 here
        throw std::invalid_argument(
            "Expected double type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.floatVal) {
        // Check against [min, max] and/or list of allowed values
        double val = param.asDouble();
        bool hasAllowed = !!paramMeta.floatVal->allowedValues;
        bool hasRanges = !!paramMeta.floatVal->allowedRanges;
        bool allowed = hasAllowed &&
            paramMeta.floatVal->allowedValues->count(val);
        bool inRange = false;
        if (hasRanges) {
          for (const auto& range : *paramMeta.floatVal->allowedRanges) {
            if (val >= range.min && val <= range.max) {
              inRange = true;
              break;
            }
          }
        }
        if ((hasAllowed || hasRanges) && (!allowed && !inRange)) {
          throw std::invalid_argument(
              "Invalid float value '" + std::to_string(val) +
              "' for key: " + toFullKey(keys));
        }
      }
      break;
    }
    case thrift::CfgParamType::STRING: {
      if (!param.isString()) {
        throw std::invalid_argument(
            "Expected string type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.strVal) {
        // Check against regex, [min, max], and/or list of allowed values
        string val = param.asString();
        bool hasAllowed = !!paramMeta.strVal->allowedValues;
        bool hasRegex = !!paramMeta.strVal->regexMatches;
        bool hasIntRanges = !!paramMeta.strVal->intRanges;
        bool hasFloatRanges = !!paramMeta.strVal->floatRanges;
        bool hasRanges = hasIntRanges || hasFloatRanges;
        bool allowed = hasAllowed &&
            paramMeta.strVal->allowedValues->count(val);
        bool regexMatch = hasRegex &&
            std::regex_match(val, std::regex(*paramMeta.strVal->regexMatches));
        bool inRange = false;
        if (hasIntRanges) {
          // Value must be an integer type
          auto valInt = folly::tryTo<int64_t>(val);
          if (!valInt.hasValue()) {
            throw std::invalid_argument(
                "String value '" + val + "' is not parseable as an integer "
                "type for key: " + toFullKey(keys));
          }
          for (const auto& range : *paramMeta.strVal->intRanges) {
            if (valInt.value() >= range.min && valInt.value() <= range.max) {
              inRange = true;
              break;
            }
          }
        } else if (hasFloatRanges) {
          // Value must be a float type
          auto valFloat = folly::tryTo<double>(val);
          if (!valFloat.hasValue()) {
            throw std::invalid_argument(
                "String value '" + val + "' is not parseable as a float type "
                "for key: " + toFullKey(keys));
          }
          for (const auto& range : *paramMeta.strVal->floatRanges) {
            if (valFloat.value() >= range.min &&
                valFloat.value() <= range.max) {
              inRange = true;
              break;
            }
          }
        }
        if ((hasAllowed || hasRegex || hasRanges) &&
            (!allowed && !regexMatch && !inRange)) {
          throw std::invalid_argument(
              "Invalid string value '" + val + "' for key: " +
              toFullKey(keys));
        }
      }
      break;
    }
    case thrift::CfgParamType::BOOLEAN: {
      if (!param.isBool()) {
        throw std::invalid_argument(
            "Expected boolean type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.boolVal) {
        // Nothing to check
      }
      break;
    }
    case thrift::CfgParamType::OBJECT: {
      if (!param.isObject()) {
        throw std::invalid_argument(
            "Expected object type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.objVal) {
        // Look for unrecognized properties
        for (const auto& kv : param.items()) {
          string prop = kv.first.asString();
          if (paramMeta.objVal->properties.find(prop) ==
              paramMeta.objVal->properties.end()) {
            keys.push_back(prop);
            result.unrecognizedKeys.push_back(toFullKey(keys));
            keys.pop_back();
          }
        }

        // Check all required properties recursively
        for (const auto& kv : paramMeta.objVal->properties) {
          string prop = kv.first;
          auto iter = param.find(prop);
          if (iter == param.items().end()) {
            if (!kv.second->required) {
              continue;
            }
            throw std::invalid_argument(
                "Required object property '" + prop + "' missing for key: " +
                toFullKey(keys));
          }
          keys.push_back(prop);
          if (kv.second->readOnly) {
            result.readOnlyKeys.push_back(toFullKey(keys));
          }
          if (kv.second->deprecated) {
            result.deprecatedKeys.push_back(toFullKey(keys));
          }
          if (!kv.second->sync) {
            result.bstarUnsyncedKeys.push_back(toFullKey(keys));
          }
          validateParam(iter->second, *kv.second, keys, result);
          keys.pop_back();
        }
      }
      break;
    }
    case thrift::CfgParamType::MAP: {
      if (!param.isObject()) {
        throw std::invalid_argument(
            "Expected map type but found " + string(param.typeName()) +
            " for key: " + toFullKey(keys));
      }
      if (paramMeta.mapVal) {
        // Check all values recursively
        for (const auto& kv : param.items()) {
          keys.push_back(kv.first.asString());
          validateParam(kv.second, *paramMeta.mapVal, keys, result);
          keys.pop_back();
        }
      }
      break;
    }
  }
}

void
ConfigMetadata::setGFlagsDefaultValues(folly::dynamic& configMeta) {
  auto flagsIter = configMeta.find("flags");
  if (flagsIter != configMeta.items().end()) {
    for (auto& flag : flagsIter->second.items()) {
      auto& key = flag.first;
      google::CommandLineFlagInfo flagInfo;
      if (google::GetCommandLineFlagInfo(key.c_str(), &flagInfo)) {
        // add default value to flags metadata
        flag.second["defaultValue"] = flagInfo.default_value;
      } else {
        LOG(WARNING) << "Unknown flag name '" << key.c_str()
                     << "' found in config metadata";
      }
    }
  }
}

string
ConfigMetadata::toFullKey(const std::vector<string>& keys) {
  return folly::join(".", keys);
}

std::vector<string>
ConfigMetadata::splitFullKey(const string& fullKey) {
  std::vector<string> v;
  folly::split(".", fullKey, v);
  return v;
}

string
ConfigMetadata::toJsonPointer(const string& key) {
  string s = "/" + JsonUtils::jsonPointerEscape(key);
  std::replace(s.begin(), s.end(), '.', '/');
  return s;
}

ConfigMetadata::CfgRecursiveParam::CfgRecursiveParam(
    const folly::dynamic& val, bool validateCfgAction) {
  // Type (required)
  this->type = TEnumMapFactory<thrift::CfgParamType>::makeNamesToValuesMap().at(
      val.at("type").asString().c_str());

  // Additional type structs (optional)
  switch (this->type) {
    case thrift::CfgParamType::INTEGER: {
      auto intVal = val.find("intVal");
      if (intVal != val.items().end()) {
        this->intVal = std::make_unique<CfgIntegerParam>(intVal->second);
      }
      break;
    }
    case thrift::CfgParamType::FLOAT: {
      auto floatVal = val.find("floatVal");
      if (floatVal != val.items().end()) {
        this->floatVal = std::make_unique<CfgFloatParam>(floatVal->second);
      }
      break;
    }
    case thrift::CfgParamType::STRING: {
      auto strVal = val.find("strVal");
      if (strVal != val.items().end()) {
        this->strVal = std::make_unique<CfgStringParam>(strVal->second);
      }
      break;
    }
    case thrift::CfgParamType::BOOLEAN: {
      auto boolVal = val.find("boolVal");
      if (boolVal != val.items().end()) {
        this->boolVal = std::make_unique<CfgBooleanParam>(boolVal->second);
      }
      break;
    }
    case thrift::CfgParamType::OBJECT: {
      auto objVal = val.find("objVal");
      if (objVal != val.items().end()) {
        this->objVal =
            std::make_unique<CfgObjectParam>(objVal->second, validateCfgAction);
      }
      break;
    }
    case thrift::CfgParamType::MAP: {
      auto mapVal = val.find("mapVal");
      if (mapVal != val.items().end()) {
        this->mapVal = std::make_unique<CfgMapParam>(mapVal->second);
      }
      break;
    }
  }
}

ConfigMetadata::CfgIntegerParam::CfgIntegerParam(const folly::dynamic& val) {
  // Allowed ranges list (optional)
  auto allowedRanges = val.find("allowedRanges");
  if (allowedRanges != val.items().end()) {
    this->allowedRanges = std::make_unique<std::vector<CfgIntegerRange>>();
    for (const auto& range : allowedRanges->second) {
      this->allowedRanges->push_back(CfgIntegerRange(range));
    }
  }

  // Allowed values list (optional)
  auto allowedValues = val.find("allowedValues");
  if (allowedValues != val.items().end()) {
    this->allowedValues = std::make_unique<std::unordered_set<int64_t>>();
    for (const auto& i : allowedValues->second) {
      this->allowedValues->insert(i.asInt());
    }
  }
}

ConfigMetadata::CfgFloatParam::CfgFloatParam(const folly::dynamic& val) {
  // Allowed ranges list (optional)
  auto allowedRanges = val.find("allowedRanges");
  if (allowedRanges != val.items().end()) {
    this->allowedRanges = std::make_unique<std::vector<CfgFloatRange>>();
    for (const auto& range : allowedRanges->second) {
      this->allowedRanges->push_back(CfgFloatRange(range));
    }
  }

  // Allowed values list (optional)
  auto allowedValues = val.find("allowedValues");
  if (allowedValues != val.items().end()) {
    this->allowedValues = std::make_unique<std::unordered_set<double>>();
    for (const auto& i : allowedValues->second) {
      this->allowedValues->insert(i.asDouble());
    }
  }
}

ConfigMetadata::CfgStringParam::CfgStringParam(const folly::dynamic& val) {
  // Regular expression string (optional)
  auto regexMatches = val.find("regexMatches");
  if (regexMatches != val.items().end()) {
    // Validate the regex (throws std::regex_error if malformed)
    std::regex re(regexMatches->second.asString());
    this->regexMatches =
        std::make_unique<string>(regexMatches->second.asString());
  }

  // Numeric ranges (optional) - only one allowed
  auto intRanges = val.find("intRanges");
  auto floatRanges = val.find("floatRanges");
  if (intRanges != val.items().end() && floatRanges != val.items().end()) {
    throw std::invalid_argument(
        "CfgStringParam cannot contain both intRanges and floatRanges");
  }
  if (intRanges != val.items().end()) {
    this->intRanges = std::make_unique<std::vector<CfgIntegerRange>>();
    for (const auto& range : intRanges->second) {
      this->intRanges->push_back(CfgIntegerRange(range));
    }
  }
  if (floatRanges != val.items().end()) {
    this->floatRanges = std::make_unique<std::vector<CfgFloatRange>>();
    for (const auto& range : floatRanges->second) {
      this->floatRanges->push_back(CfgFloatRange(range));
    }
  }

  // Allowed values list (optional)
  auto allowedValues = val.find("allowedValues");
  if (allowedValues != val.items().end()) {
    this->allowedValues = std::make_unique<std::unordered_set<string>>();
    for (const auto& s : allowedValues->second) {
      this->allowedValues->insert(s.asString());
    }
  }
}

ConfigMetadata::CfgBooleanParam::CfgBooleanParam(
    const folly::dynamic& /*val*/) {
  // Nothing here
}

ConfigMetadata::CfgMapParam::CfgMapParam(const folly::dynamic& val)
    : CfgRecursiveParam(val) {
}

ConfigMetadata::CfgObjectParam::CfgObjectParam(
    const folly::dynamic& val, bool validateCfgAction) {
  // Object properties (required)
  for (const auto& kv : val.at("properties").items()) {
    this->properties[kv.first.asString()] =
        std::make_unique<CfgObjectValMetadata>(kv.second, validateCfgAction);
  }
}

ConfigMetadata::CfgIntegerRange::CfgIntegerRange(const folly::dynamic& val) {
  // [min, max] array (required)
  this->min = val.at(0).asInt();
  this->max = val.at(1).asInt();
}

ConfigMetadata::CfgFloatRange::CfgFloatRange(const folly::dynamic& val) {
  // [min, max] array (required)
  this->min = val.at(0).asDouble();
  this->max = val.at(1).asDouble();
}

ConfigMetadata::CfgObjectValMetadata::CfgObjectValMetadata(
    const folly::dynamic& val, bool validateCfgAction)
    : CfgRecursiveParam(val) {
  // Description (required)
  this->desc = val.at("desc").asString();

  // Action (optional)
  auto action = val.find("action");
  if (action != val.items().end()) {
    auto cfgActionMap =
        TEnumMapFactory<thrift::CfgAction>::makeNamesToValuesMap();
    auto actionIter = cfgActionMap.find( action->second.asString().c_str());
    if (actionIter == cfgActionMap.end()) {
      if (validateCfgAction) {
        throw std::invalid_argument(
            "Found unknown CfgAction type '" + action->second.asString() + "'");
      }
      LOG(WARNING) << "Found unknown CfgAction type '"
                   << action->second.asString() << "', defaulting to NO_ACTION";
      this->action =
          std::make_unique<thrift::CfgAction>(thrift::CfgAction::NO_ACTION);
    } else {
      this->action = std::make_unique<thrift::CfgAction>(actionIter->second);
    }
  }

  // Required property (optional)
  auto required = val.find("required");
  this->required = (required != val.items().end()) ?
      required->second.asBool() : false;

  // Read-only (optional)
  auto readOnly = val.find("readOnly");
  this->readOnly = (readOnly != val.items().end()) ?
      readOnly->second.asBool() : false;

  // Deprecated (optional)
  auto deprecated = val.find("deprecated");
  this->deprecated = (deprecated != val.items().end()) ?
      deprecated->second.asBool() : false;

  // Sync (optional)
  auto sync = val.find("sync");
  this->sync = (sync != val.items().end()) ? sync->second.asBool() : false;

  // Tag (optional)
  auto tag = val.find("tag");
  if (tag != val.items().end()) {
    this->tag = std::make_unique<string>(tag->second.asString());
  }
}

ConfigMetadata::CfgParamMetadata::CfgParamMetadata(
    const folly::dynamic& val, bool validateCfgAction)
    : CfgRecursiveParam(val, validateCfgAction) {
  // Description (required)
  this->desc = val.at("desc").asString();

  // Action (required)
  auto cfgActionMap =
      TEnumMapFactory<thrift::CfgAction>::makeNamesToValuesMap();
  auto actionIter = cfgActionMap.find( val.at("action").asString().c_str());
  if (actionIter == cfgActionMap.end()) {
    if (validateCfgAction) {
      throw std::invalid_argument(
          "Found unknown CfgAction type '" + val.at("action").asString() + "'");
    }
    LOG(WARNING) << "Found unknown CfgAction type '"
                 << val.at("action").asString() << "', defaulting to NO_ACTION";
    this->action = thrift::CfgAction::NO_ACTION;
  } else {
    this->action = actionIter->second;
  }

  // Read-only (optional)
  auto readOnly = val.find("readOnly");
  this->readOnly = (readOnly != val.items().end()) ?
      readOnly->second.asBool() : false;

  // Deprecated (optional)
  auto deprecated = val.find("deprecated");
  this->deprecated = (deprecated != val.items().end()) ?
      deprecated->second.asBool() : false;

  // Sync (optional)
  auto sync = val.find("sync");
  this->sync = (sync != val.items().end()) ? sync->second.asBool() : false;

  // Tag (optional)
  auto tag = val.find("tag");
  if (tag != val.items().end()) {
    this->tag = std::make_unique<string>(tag->second.asString());
  }
}

} // namespace terragraph
} // namespace facebook
