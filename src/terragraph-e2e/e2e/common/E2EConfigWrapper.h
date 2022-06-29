/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/dynamic.h>
#include <folly/Expected.h>
#include <folly/json.h>
#include <regex>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "ConfigMetadata.h"
#include "JsonUtils.h"

namespace facebook {
namespace terragraph {

/**
 * Wrapper class for accessing a Terragraph E2E configuration file.
 *
 * This is a template used to implement ControllerConfig and AggregatorConfig.
 */
template <class ConfigType>
class E2EConfigWrapper {
 public:
  /**
   * Empty constructor. The caller is expected to invoke setE2EConfigFile()
   * before using any other methods.
   */
  E2EConfigWrapper() {
    config_ = std::make_shared<ConfigType>();
  }

  /** Construct E2EConfigWrapper by reading the given config file. */
  explicit E2EConfigWrapper(const std::string& configFile)
           : configFile_(configFile) {
    config_ = std::make_shared<ConfigType>();
    readConfigFile();
  }

  /** Set the E2E config file to read from and write to. */
  void
  setE2EConfigFile(const std::string& e2eConfigFile) {
    configFile_ = e2eConfigFile;
    readConfigFile();
  }

  /**
   * Initialize (or re-initialize) all internal state by reading the original
   * config file.
   */
  void
  readConfigFile() {
    // Read config file
    try {
      configJson_ = JsonUtils::readJsonFile2String(configFile_);
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << "Could not read config file " << configFile_ << ": "
                 << folly::exceptionStr(ex);
      return;
    }

    // Parse config JSON and sort + trim whitespace
    // Remove meta info from flag names (deprecated feature)
    try {
      folly::dynamic configObj = folly::parseJson(configJson_);
      parseFlags(configObj, false);
      folly::json::serialization_opts opts;
      opts.sort_keys = true;
      configJson_ = folly::json::serialize(configObj, opts);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Could not parse config JSON: " << folly::exceptionStr(ex);
      return;
    }

    // Deserialize config JSON
    apache::thrift::SimpleJSONSerializer jsonSerializer;
    *config_ = ConfigType();
    try {
      jsonSerializer.deserialize(configJson_, *config_);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Could not parse config struct: "
                 << folly::exceptionStr(ex);
    }
  }

  /**
   * Set the node config to the given JSON string and writes it to the original
   * config file. Returns the full object difference between the old and new
   * config if successful, or sets the error string otherwise.
   *
   * This function will perform full validation of the new config:
   * Thrift deserialization, JSON parsing, "flags" checking, and validation
   * using config metadata (optional).
   *
   * If 'fromBstarSync' is true (i.e. during BinaryStar data syncing), any new
   * keys marked as "unsynced" will be stripped and old keys marked as
   * "unsynced" will persist.
   */
  folly::Expected<folly::dynamic, std::string>
  setConfig(
      const std::string& configJson,
      ConfigMetadata* configMetadata = nullptr,
      bool fromBstarSync = false) {
    // Copy the old config
    folly::dynamic oldConfig = folly::dynamic::object;
    try {
      oldConfig = folly::parseJson(configJson_);
    } catch (const std::exception& ex) {/* shouldn't happen */}

    // Try to deserialize new config JSON, reject on failure
    ConfigType config;
    apache::thrift::SimpleJSONSerializer jsonSerializer;
    try {
      jsonSerializer.deserialize(configJson, config);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Could not deserialize json string: "
                 << folly::exceptionStr(ex);
      return folly::makeUnexpected(
          std::string("Could not deserialize json string"));
    }

    // Try to parse new config JSON, reject on failure
    folly::dynamic configObj = folly::dynamic::object;
    try {
      configObj = folly::parseJson(configJson);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Could not parse json string: " << folly::exceptionStr(ex);
      return folly::makeUnexpected(std::string("Could not parse json string"));
    }

    // Special handling for "flags" field
    try {
      parseFlags(configObj, true);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Could not parse flags: " << folly::exceptionStr(ex);
      return folly::makeUnexpected(std::string(ex.what()));
    }

    // Validate with metadata (if provided)
    ConfigMetadata::ValidationResult newResult;
    if (configMetadata) {
      try {
        newResult = configMetadata->validate(configObj);
      } catch (const std::exception& ex) {
        LOG(ERROR) << "Config validation failed: " << folly::exceptionStr(ex);
        return folly::makeUnexpected(std::string(ex.what()));
      }
    }

    // Additional actions for BinaryStar sync...
    if (fromBstarSync && configMetadata) {
      // Strip out unsynced keys from new config
      for (const std::string& fullKey : newResult.bstarUnsyncedKeys) {
        dynamicEraseKey(configObj, fullKey);
      }

      // Add unsynced keys from old config
      ConfigMetadata::ValidationResult oldResult;
      try {
        oldResult = configMetadata->validate(oldConfig);
      } catch (const std::exception& ex) {
        // TODO: Should we validate existing config when reading?
        LOG(ERROR) << "ERROR! Validation of existing config failed: "
                   << folly::exceptionStr(ex);
        return folly::makeUnexpected(std::string(ex.what()));
      }
      for (const std::string& fullKey : oldResult.bstarUnsyncedKeys) {
        if (auto val = dynamicGetValue(oldConfig, fullKey)) {
          dynamicInsert(configObj, fullKey, *val);
        }
      }
    }

    // Write to config file
    try {
      JsonUtils::writeString2JsonFile(
          JsonUtils::toSortedPrettyJson(configObj), configFile_);
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << "Unable to write config: " << folly::exceptionStr(ex);
      return folly::makeUnexpected(std::string("Unable to write config"));
    }

    // Re-initialize state
    readConfigFile();

    // Diff the old vs. new config
    folly::dynamic newConfig = folly::dynamic::object;
    try {
      newConfig = folly::parseJson(configJson_);
    } catch (const std::exception& ex) {/* shouldn't happen */}
    return JsonUtils::dynamicObjectFullDifference(oldConfig, newConfig);
  }

  /**
   * Set the node config to the given Thrift structure and writes it to the
   * original config file. Returns true upon success, or false otherwise.
   *
   * WARNING: This function DOES NOT perform any kind of validation!
   */
  bool
  setConfigFromThrift(const ConfigType& config) {
    // Serialize to JSON
    std::string configJson;
    apache::thrift::SimpleJSONSerializer jsonSerializer;
    try {
      jsonSerializer.serialize(config, &configJson);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Unable to serialize config: " << folly::exceptionStr(ex);
      return false;
    }

    // Write to config file
    try {
      JsonUtils::writeString2JsonFile(
          JsonUtils::toSortedPrettyJson(configJson), configFile_);
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << "Unable to write config: " << folly::exceptionStr(ex);
      return false;
    }

    // Re-initialize state
    readConfigFile();
    return true;
  }

  /** Returns the config file contents (as a JSON string). */
  std::string
  getConfigJson() const {
    return configJson_;
  }

  /** Returns the config struct. */
  std::shared_ptr<const ConfigType>
  getConfig() const {
    return config_;
  }

 private:
  /**
   * Parse the "flags" map in the given E2E config object.
   *
   * This will strip meta info from the flag names (keys), i.e. all characters
   * before the last '.', and throws std::invalid_argument if any flag name was
   * invalid (only allow alphanumerics and underscores) and 'throwOnError' is
   * true.
   */
  void
  parseFlags(folly::dynamic& configObj, bool throwOnError) {
    std::regex flagNameRegex("^([A-Za-z0-9_])+$");
    const std::string kFlagsKey = "flags";

    folly::dynamic flags = folly::dynamic::object;
    configObj.setDefault(kFlagsKey, folly::dynamic::object);
    for (const auto& kv : configObj[kFlagsKey].items()) {
      std::string key = kv.first.asString();
      std::string value = kv.second.asString();

      // Strip group meta info in flag name (anything before last '.')
      size_t idx = key.rfind('.');
      std::string flag = (idx != std::string::npos) ? key.substr(idx + 1) : key;

      // Validate flag name (only alphanumerics and underscores)
      if (!std::regex_match(flag, flagNameRegex)) {
        LOG(ERROR) << "Validation of new controller config failed: "
                      "Invalid flag name: " << key;
        if (throwOnError) {
          throw std::invalid_argument("Invalid flag name: " + key);
        }
      }

      flags[flag] = value;
    }
    configObj[kFlagsKey] = flags;
  }

  /**
   * Erase a config key from a config object.
   *
   * Returns true if successful.
   */
  bool
  dynamicEraseKey(folly::dynamic& obj, const std::string& fullKey) {
    std::vector<std::string> v = ConfigMetadata::splitFullKey(fullKey);
    folly::dynamic* o = &obj;
    for (size_t i = 0; i < v.size() - 1; i++) {
      auto iter = o->find(v[i]);
      if (iter == o->items().end()) {
        return false;
      }
      o = &iter->second;
    }
    return (o->erase(v.back()) > 0);
  }

  /**
   * Returns the value at the given config key, or std::nullopt if not found.
   */
  std::optional<folly::dynamic>
  dynamicGetValue(folly::dynamic& obj, const std::string& fullKey) {
    folly::dynamic* o = &obj;
    for (const std::string& key : ConfigMetadata::splitFullKey(fullKey)) {
      auto iter = o->find(key);
      if (iter == o->items().end()) {
        return std::nullopt;
      }
      o = &iter->second;
    }
    return *o;
  }

  /** Insert a key-value pair into a config object. */
  void
  dynamicInsert(
      folly::dynamic& obj,
      const std::string& fullKey,
      const folly::dynamic& val) {
    std::vector<std::string> v = ConfigMetadata::splitFullKey(fullKey);
    folly::dynamic* o = &obj;
    for (size_t i = 0; i < v.size() - 1; i++) {
      o = &o->setDefault(v[i], folly::dynamic::object);
    }
    (*o)[v.back()] = val;
  }

  /** The location of the config file. */
  std::string configFile_;

  /** The config file contents (as a JSON string, sorted and trimmed). */
  std::string configJson_;

  /** Shared pointer to the config struct. */
  std::shared_ptr<ConfigType> config_;
};

} // namespace terragraph
} // namespace facebook
