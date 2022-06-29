/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "ConfigHelper.h"

#include <ctime>

#include <boost/filesystem.hpp>
#include <folly/Conv.h>
#include <folly/Format.h>
#include <folly/gen/Base.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "SharedObjects.h"
#include "algorithms/ChannelHelper.h"
#include "algorithms/ControlSuperframeHelper.h"
#include "algorithms/GolayHelper.h"
#include "algorithms/LinkGroupHelper.h"
#include "algorithms/OccSolver.h"
#include "algorithms/PolarityHelper.h"
#include "e2e/common/JsonUtils.h"
#include "e2e/common/MacUtils.h"
#include "e2e/common/Md5Utils.h"
#include "e2e/common/OpenrUtils.h"
#include "e2e/common/SysUtils.h"

using apache::thrift::detail::TEnumMapFactory;

namespace {
// Number of colors used to partition graph for distributed ignition. Must
// be even, as half of this number will be used to color radios with even
// polarity and half will be used to color radios with odd polarity.
constexpr static int kNumColors{4};
static_assert(kNumColors % 2 == 0, "kNumColors must be even");
}

namespace facebook {
namespace terragraph {

void
ConfigHelper::setConfigFiles(
    const std::string& baseConfigDir,
    const std::string& firmwareBaseConfigDir,
    const std::string& hardwareBaseConfigDir,
    const std::string& hardwareTypesFile,
    const std::string& nodeConfigOverridesFile,
    const std::string& autoNodeConfigOverridesFile,
    const std::string& networkConfigOverridesFile,
    const std::string& nodeConfigMetadataFile,
    const std::string& configBackupDir,
    const std::unordered_set<std::string>& topologyNodeNames) {
  baseConfigDir_ = baseConfigDir;
  firmwareBaseConfigDir_ = firmwareBaseConfigDir;
  hardwareBaseConfigDir_ = hardwareBaseConfigDir;
  hardwareTypesFile_ = hardwareTypesFile;
  nodeConfigOverridesFile_ = nodeConfigOverridesFile;
  autoNodeConfigOverridesFile_ = autoNodeConfigOverridesFile;
  networkConfigOverridesFile_ = networkConfigOverridesFile;
  configBackupDir_ = configBackupDir;

  // Load config metadata file
  // On errors, default initialize to prevent segfaults
  try {
    nodeConfigMeta_ = std::make_unique<ConfigMetadata>(
        nodeConfigMetadataFile,
        true /* keepFullMetadata */,
        false /* hasFlags */);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Error loading node config metadata: "
               << folly::exceptionStr(ex);
    folly::dynamic obj = folly::dynamic::object;
    nodeConfigMeta_ = std::make_unique<ConfigMetadata>(
        obj, true /* keepFullMetadata */, false /* hasFlags */);
  }

  // Load config files from disk
  readLocalConfigStore(topologyNodeNames);

  configState_.clear();
}

bool
ConfigHelper::createDirectory(const std::string& filepath) {
  boost::filesystem::path path(filepath);
  path.remove_filename();
  boost::system::error_code errorCode;
  return boost::filesystem::create_directories(path, errorCode);
}

void
ConfigHelper::readLocalConfigStore(
    const std::unordered_set<std::string>& topologyNodeNames) {
  // Validate directory paths (expect trailing '/')
  if (baseConfigDir_.empty() || baseConfigDir_.back() != '/') {
    baseConfigDir_ += '/';
  }
  if (firmwareBaseConfigDir_.empty() || firmwareBaseConfigDir_.back() != '/') {
    firmwareBaseConfigDir_ += '/';
  }
  if (hardwareBaseConfigDir_.empty() || hardwareBaseConfigDir_.back() != '/') {
    hardwareBaseConfigDir_ += '/';
  }
  if (configBackupDir_.empty() || configBackupDir_.back() != '/') {
    configBackupDir_ += '/';
  }

  // Read base configs
  readBaseConfigs();

  // Read firmware base configs
  readFirmwareBaseConfigs();

  // Read hardware types file
  readHardwareTypesFile();

  // Read hardware base configs
  readHardwareBaseConfigs();

  // Read the node specific overrides file (or create the parent directory tree)
  if (createDirectory(autoNodeConfigOverridesFile_)) {
    LOG(INFO) << "Created directories for automatic node overrides file: "
              << autoNodeConfigOverridesFile_;
  } else {
    readAutoNodeConfigOverrides(topologyNodeNames);
  }

  // Read the network-wide overrides file (or create the parent directory tree)
  if (createDirectory(networkConfigOverridesFile_)) {
    LOG(INFO) << "Created directories for network overrides file: "
              << networkConfigOverridesFile_;
  } else {
    readNetworkConfigOverrides();
  }

  // Read the node specific overrides file (or create the parent directory tree)
  if (createDirectory(nodeConfigOverridesFile_)) {
    LOG(INFO) << "Created directories for node overrides file: "
              << nodeConfigOverridesFile_;
  } else {
    readNodeConfigOverrides(topologyNodeNames);
  }

  // Create directory for config backups
  if (createDirectory(configBackupDir_)) {
    LOG(INFO) << "Created config backup directory: "
              << configBackupDir_;
  }
}

std::pair<folly::dynamic, std::string>
ConfigHelper::readBaseConfigFiles(const std::string& directory) const {
  folly::dynamic obj = folly::dynamic::object;
  SwVersion latestSwVer;
  for (const auto& pair : SysUtils::findFilesInDirectory(
           directory, E2EConsts::kJsonFileExtension)) {

    // Check software version length
    std::string filename = pair.second.substr(
        0, pair.second.size() - E2EConsts::kJsonFileExtension.size());
    if (filename.empty()) {
      continue;
    }
    auto swVer = SwVersion(filename);

    // Only record the software version - load the file when needed
    obj[swVer.version] = pair.first;
    if (latestSwVer < swVer) {
      latestSwVer = swVer;
    }
  }

  return std::make_pair(obj, obj.empty() ? "" : latestSwVer.version);
}

void
ConfigHelper::readBaseConfigs() {
  LOG(INFO) << "Reading base configs from " << baseConfigDir_ << "...";
  auto result = readBaseConfigFiles(baseConfigDir_);
  baseConfigObjects_ = result.first;
  latestBaseVersion_ = result.second;
}

void
ConfigHelper::readFirmwareBaseConfigs() {
  LOG(INFO) << "Reading firmware base configs from " << firmwareBaseConfigDir_
            << "...";

  folly::dynamic obj = folly::dynamic::object;
  for (const auto& pair : SysUtils::findFilesInDirectory(
           firmwareBaseConfigDir_, E2EConsts::kJsonFileExtension)) {
    // Check firmware version length
    std::string filename = pair.second.substr(
        0, pair.second.size() - E2EConsts::kJsonFileExtension.size());
    if (filename.empty()) {
      continue;
    }

    // Only record the firmware version - load the file when needed
    obj[filename] = pair.first;
  }

  fwConfigObjects_ = std::move(obj);
}

void
ConfigHelper::readHardwareBaseConfigs() {
  for (const auto& pathAndName :
       SysUtils::findSubdirectoriesInDirectory(hardwareBaseConfigDir_)) {
    const std::string& hwType = pathAndName.second;
    std::string dir = hardwareBaseConfigDir_ + hwType + '/';
    LOG(INFO) << "Reading hardware base configs from " << dir << "...";
    auto result = readBaseConfigFiles(dir);
    hwConfigObjects_[hwType] = result.first;
    latestHwBaseVersions_[hwType] = result.second;
  }
}

void
ConfigHelper::readHardwareTypesFile() {
  folly::dynamic obj = folly::dynamic::object;
  try {
    obj = JsonUtils::readJsonFile2DynamicObject(hardwareTypesFile_);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to read hardware config types file "
               << hardwareTypesFile_ << ": " << folly::exceptionStr(ex);
    return;
  }
  if (!obj.isObject()) {
    LOG(ERROR) << "Hardware config types file is not a JSON object";
    return;
  }

  // Fill out reverse map
  for (const auto& pair : obj.items()) {
    if (pair.first.isString() && pair.second.isArray()) {
      for (const auto& hwBoardId : pair.second) {
        if (hwBoardId.isString()) {
          hardwareConfigTypeMap_[hwBoardId.asString()] = pair.first.asString();
        }
      }
    }
  }
}

void
ConfigHelper::readNetworkConfigOverrides() {
  // Read file
  folly::dynamic cfg = folly::dynamic::object;
  try {
    cfg = JsonUtils::readJsonFile2DynamicObject(networkConfigOverridesFile_);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to read network config overrides file "
               << networkConfigOverridesFile_ << ": "
               << folly::exceptionStr(ex);
    return;
  }
  if (!cfg.isObject()) {
    LOG(ERROR) << "Network config overrides file is not a JSON object";
    return;
  }

  // Create a fake node config and try to deserialize it
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  folly::dynamic fullConfigObj;
  try {
    fullConfigObj = buildNodeConfig(
        std::nullopt,
        "",
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::make_optional(cfg),
        std::nullopt);
    thrift::NodeConfig config;
    jsonSerializer.deserialize(folly::toJson(fullConfigObj), config);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not serialize network config overrides: "
               << folly::exceptionStr(ex);
    return;
  }

  // Validate network config overrides
  try {
    nodeConfigMeta_->validate(fullConfigObj);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Validation of network config overrides failed: "
               << folly::exceptionStr(ex);
    return;
  }

  // Store network config overrides
  LOG(INFO) << "Loaded network config overrides";
  networkOverrides_ = std::move(cfg);
}

void
ConfigHelper::readNodeConfigOverrides(
    const std::unordered_set<std::string>& topologyNodeNames) {
  readPerNodeConfigOverrides(
      topologyNodeNames,
      nodeConfigOverridesFile_,
      nodesOverrides_,
      false /* isAutoLayer */);
}

void
ConfigHelper::readAutoNodeConfigOverrides(
    const std::unordered_set<std::string>& topologyNodeNames) {
  readPerNodeConfigOverrides(
      topologyNodeNames,
      autoNodeConfigOverridesFile_,
      autoNodesOverrides_,
      true /* isAutoLayer */);
}

void
ConfigHelper::readPerNodeConfigOverrides(
    const std::unordered_set<std::string>& topologyNodeNames,
    const std::string& nodeConfigOverridesFile,
    folly::dynamic& nodeOverrides,
    bool isAutoLayer) {
  // Read file
  folly::dynamic cfg = folly::dynamic::object;
  try {
    cfg = JsonUtils::readJsonFile2DynamicObject(nodeConfigOverridesFile);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to read node config overrides file "
               << nodeConfigOverridesFile << ": "
               << folly::exceptionStr(ex);
    return;
  }
  if (!cfg.isObject()) {
    LOG(ERROR) << "Node config overrides file is not a JSON object";
    return;
  }

  // Parse all node overrides individually
  for (const auto& pair : cfg.items()) {
    std::string nodeName = pair.first.asString();
    folly::dynamic object = pair.second;

    if (!object.isObject()) {
      LOG(ERROR) << "Node config overrides for node " << nodeName
                 << " is not a JSON object";
      continue;
    }
    if (object.empty()) {
      continue;
    }

    if (!topologyNodeNames.count(nodeName)) {
      VLOG(3) << "WARNING: Node " << nodeName << " has node config overrides "
                   "but is missing from the topology file. This config will "
                   "still be loaded, but please delete it if unneeded.";
    }

    // Clean empty objects
    object = JsonUtils::dynamicObjectClean(object);

    // Create a node config and try to deserialize it
    folly::dynamic newNodesOverrides = folly::dynamic::object(nodeName, object);
    apache::thrift::SimpleJSONSerializer jsonSerializer;
    folly::dynamic fullConfigObj;
    try {
      if (isAutoLayer) {
        fullConfigObj = buildNodeConfig(
            nodeName,
            "",
            std::nullopt,
            std::nullopt,
            std::make_optional(newNodesOverrides),
            std::nullopt,
            std::nullopt);
      } else {
        fullConfigObj = buildNodeConfig(
            nodeName,
            "",
            std::nullopt,
            std::nullopt,
            std::make_optional(autoNodesOverrides_),
            std::make_optional(networkOverrides_),
            std::make_optional(newNodesOverrides));
      }
      thrift::NodeConfig config;
      jsonSerializer.deserialize(folly::toJson(fullConfigObj), config);
    } catch (std::exception& ex) {
      LOG(ERROR) << "Could not serialize node config overrides for node "
                 << nodeName << ": " << folly::exceptionStr(ex);
      continue;
    }

    // Validate node config overrides
    try {
      nodeConfigMeta_->validate(fullConfigObj);
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Validation of node config overrides for node " << nodeName
                 << " failed: " << folly::exceptionStr(ex);
      continue;
    }

    // Store node config overrides
    VLOG(4) << "Loaded node config overrides for node " << nodeName;
    nodeOverrides[nodeName] = object;
  }
}

folly::dynamic
ConfigHelper::buildNodeConfig(
    const std::optional<std::string> nodeName,
    const std::string& swVersion,
    const std::optional<std::string> fwVersion,
    const std::optional<std::string> hwBoardId,
    const std::optional<folly::dynamic> autoNodeOverrides,
    const std::optional<folly::dynamic> networkOverrides,
    const std::optional<folly::dynamic> nodeOverrides) {
  SwVersion version(swVersion);

  // Get best base config match
  folly::dynamic config = getBaseConfig(
      version, baseConfigObjects_, latestBaseVersion_, false);

  // Merge with firmware base config (if fwVersion is provided)
  if (fwVersion) {
    folly::dynamic fwConfig = getFirmwareConfig(
        fwVersion.value(), fwConfigObjects_);
    JsonUtils::dynamicObjectMerge(config, fwConfig);
  }

  // Merge with hardware base config (if hwBoardId is provided)
  if (hwBoardId) {
    // Look up in hardware types map
    auto hwConfigTypeIter = hardwareConfigTypeMap_.find(hwBoardId.value());
    if (hwConfigTypeIter != hardwareConfigTypeMap_.end()) {
      // Look up in hardware configs map
      auto hwConfigIter = hwConfigObjects_.find(hwConfigTypeIter->second);
      if (hwConfigIter != hwConfigObjects_.items().end()) {
        auto hwVerIter = latestHwBaseVersions_.find(hwConfigTypeIter->second);
        std::string hwDefaultVer = (hwVerIter != latestHwBaseVersions_.end())
            ? hwVerIter->second : "";
        folly::dynamic hwConfig = getBaseConfig(
            version, hwConfigIter->second, hwDefaultVer, true);
        JsonUtils::dynamicObjectMerge(config, hwConfig);
      }
    }
  }

  // Merge with override layers
  JsonUtils::dynamicObjectMerge(
      config,
      getConfigOverridesForNode(
          nodeName, autoNodeOverrides, networkOverrides, nodeOverrides));
  return config;
}

folly::dynamic
ConfigHelper::getConfigOverridesForNode(
    const std::optional<std::string> nodeName,
    const std::optional<folly::dynamic> autoNodeOverrides,
    const std::optional<folly::dynamic> networkOverrides,
    const std::optional<folly::dynamic> nodeOverrides) const {
  folly::dynamic config = folly::dynamic::object;

  // Merge with automatic node overrides (if nodeName is provided)
  if (nodeName) {
    if (!autoNodeOverrides) {
      if (autoNodesOverrides_.isObject()) {
        auto iter = autoNodesOverrides_.find(*nodeName);
        if (iter != autoNodesOverrides_.items().end()) {
          config = iter->second;
        }
      }
    } else {
      if (autoNodeOverrides->isObject()) {
        auto iter = autoNodeOverrides->find(*nodeName);
        if (iter != autoNodeOverrides->items().end()) {
          config = iter->second;
        }
      }
    }
  }

  // Merge with network overrides
  if (!networkOverrides) {
    JsonUtils::dynamicObjectMerge(config, networkOverrides_);
  } else {
    JsonUtils::dynamicObjectMerge(config, networkOverrides.value());
  }

  // Merge with user node overrides (if nodeName is provided)
  if (nodeName) {
    if (!nodeOverrides) {
      if (nodesOverrides_.isObject()) {
        auto iter = nodesOverrides_.find(*nodeName);
        if (iter != nodesOverrides_.items().end()) {
          JsonUtils::dynamicObjectMerge(config, iter->second);
        }
      }
    } else {
      if (nodeOverrides->isObject()) {
        auto iter = nodeOverrides->find(*nodeName);
        if (iter != nodeOverrides->items().end()) {
          JsonUtils::dynamicObjectMerge(config, iter->second);
        }
      }
    }
  }
  return config;
}

std::optional<std::string>
ConfigHelper::matchBaseVersion(
    const SwVersion& swVer,
    const std::optional<folly::dynamic> baseConfigObjects) const {
  folly::dynamic baseConfigs =
      baseConfigObjects ? baseConfigObjects.value() : baseConfigObjects_;

  // Match exact version (e.g. for unknown hardware)
  if (baseConfigs.count(swVer.fullVersion)) {
    return swVer.fullVersion;
  }

  // If swVer has a valid major version, use that to match
  if (swVer.majorVersion > 0) {
    SwVersion bestBaseVer;
    for (const auto& pair : baseConfigs.items()) {
      SwVersion baseSwVer(pair.first.asString());
      if (swVer == baseSwVer) {
        return baseSwVer.fullVersion;
      }
      if (swVer > baseSwVer && baseSwVer > bestBaseVer) {
        bestBaseVer = baseSwVer;
      }
    }
    return bestBaseVer.fullVersion;
  }

  // If no valid major version, try to match as many tokens as possible
  for (int i = swVer.tokens.size(); i > 0; i--) {
    std::string ver = folly::join(
        E2EConsts::kSwVersionDelimiter,
        swVer.tokens.begin(),
        swVer.tokens.begin() + i);
    if (baseConfigs.count(ver)) {
      return ver;
    }
  }

  // No match
  return std::nullopt;
}

std::optional<std::string>
ConfigHelper::matchFirmwareVersion(
    const std::string& fwVer,
    const std::optional<folly::dynamic> fwConfigObjects) const {
  folly::dynamic fwConfigs =
      fwConfigObjects ? fwConfigObjects.value() : fwConfigObjects_;

  // Find best match based on major/minor versions
  FwVersion ver(fwVer);
  FwVersion bestFwVer;
  for (const auto& pair : fwConfigs.items()) {
    FwVersion baseFwVer(pair.first.asString());
    if (baseFwVer.majorVersion == ver.majorVersion) {
      if (baseFwVer.minorVersion == ver.minorVersion) {
        return baseFwVer.fullVersion;  // exact match, return now
      } else if (baseFwVer.minorVersion < ver.minorVersion &&
                 baseFwVer.minorVersion >= bestFwVer.minorVersion) {
        bestFwVer = baseFwVer;  // majors match, find closest minor
      }
    }
  }

  if (!bestFwVer.fullVersion.empty()) {
    return bestFwVer.fullVersion;
  } else {
    return std::nullopt;  // no match
  }
}

folly::dynamic
ConfigHelper::getBaseConfig(
    const SwVersion& swVer,
    folly::dynamic& baseConfigObjects,
    const std::string& defaultVersion,
    bool isHwBaseConfig) {
  // Find the best match for software version
  std::string ver;
  auto bestMatch = matchBaseVersion(swVer, baseConfigObjects);
  if (bestMatch) {
    VLOG(3) << "Best match for software version " << swVer.fullVersion
            << " is: " << bestMatch.value();
    ver = bestMatch.value();
  } else {
    if (!swVer.fullVersion.empty()) {
      VLOG(3) << "No match for software version " << swVer.fullVersion
              << ", using latest base config instead: " << defaultVersion;
    }
    ver = defaultVersion;
  }

  // Return the associated base config
  auto iter = baseConfigObjects.find(ver);
  if (iter == baseConfigObjects.items().end()) {
    // No base config exists
    return folly::dynamic::object;
  } else if (iter->second.isObject()) {
    // Config file is already loaded
    return iter->second;
  } else {
    // Need to load config file from disk
    try {
      if (!isHwBaseConfig) {
        iter->second = loadConfigFile(iter->second.asString());
      } else {
        iter->second = loadConfigFile(iter->second.asString(), ver);
      }
    } catch (const std::exception& ex) {
      // Failed - set value to an empty object so we don't try again
      iter->second = folly::dynamic::object;
    }
    return iter->second;
  }
}

folly::dynamic
ConfigHelper::getFirmwareConfig(
    const std::string& fwVer, folly::dynamic& fwConfigObjects) {
  // Find the best match for firmware version
  auto bestMatch = matchFirmwareVersion(fwVer, fwConfigObjects);
  if (bestMatch) {
    VLOG(3) << "Best match for firmware version " << fwVer << " is: "
            << bestMatch.value();
  } else {
    if (!fwVer.empty()) {
      VLOG(3) << "No match for firmware version " << fwVer;
    }
    return folly::dynamic::object;
  }

  // Return the associated firmware config
  auto iter = fwConfigObjects.find(bestMatch.value());
  if (iter == fwConfigObjects.items().end()) {
    // No firmware config exists (shouldn't happen)
    return folly::dynamic::object;
  } else if (iter->second.isObject()) {
    // Config file is already loaded
    return iter->second;
  } else {
    // Need to load config file from disk
    try {
      iter->second = loadConfigFile(iter->second.asString());
    } catch (const std::exception& ex) {
      // Failed - set value to an empty object so we don't try again
      iter->second = folly::dynamic::object;
    }
    return iter->second;
  }
}

void
ConfigHelper::filterValidationResults(
    ConfigMetadata::ValidationResult& result,
    const folly::dynamic& overrides) const {
  result.unrecognizedKeys.erase(
      std::remove_if(
          result.unrecognizedKeys.begin(),
          result.unrecognizedKeys.end(),
          [&](const std::string& key) -> bool {
            return !JsonUtils::objectContains(
                overrides, ConfigMetadata::toJsonPointer(key));
          }),
      result.unrecognizedKeys.end());
  result.readOnlyKeys.erase(
      std::remove_if(
          result.readOnlyKeys.begin(),
          result.readOnlyKeys.end(),
          [&](const std::string& key) -> bool {
            return !JsonUtils::objectContains(
                overrides, ConfigMetadata::toJsonPointer(key));
          }),
      result.readOnlyKeys.end());
  result.deprecatedKeys.erase(
      std::remove_if(
          result.deprecatedKeys.begin(),
          result.deprecatedKeys.end(),
          [&](const std::string& key) -> bool {
            return !JsonUtils::objectContains(
                overrides, ConfigMetadata::toJsonPointer(key));
          }),
      result.deprecatedKeys.end());
}

void
ConfigHelper::updateTunnelConfigAutoLayerForNodeOverride(
    const std::string& nodeName,
    const folly::dynamic& newTunnelConfig,
    std::string& errorMsg) {
  // make a copy of the node tunnelConfig in the auto node overrides layer
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  folly::dynamic& autoNodesTunnelConfig =
      getDynamicObjectAtPath(newAutoNodesOverrides, {nodeName, "tunnelConfig"});
  bool changed = false;
  // copy of the tunnel config names to iterate over
  auto tunnelConfigKeys = autoNodesTunnelConfig.keys();
  for (const auto& tunnelName : tunnelConfigKeys) {
    if (!newTunnelConfig.count(tunnelName)) {
      VLOG(4) << "Removing auto layer tunnel \"" << tunnelName
              << "\" for node \"" << nodeName << "\".";
      changed = true;
      autoNodesTunnelConfig.erase(tunnelName);
    }
  }
  // remove tunnelConfig object if empty
  if (changed && autoNodesTunnelConfig.empty()) {
    folly::dynamic& nodeOverrides =
        getDynamicObjectAtPath(newAutoNodesOverrides, {nodeName});
    nodeOverrides.erase("tunnelConfig");
    changed = true;
  }
  // update auto nodes override layer
  if (changed) {
    setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
  }
}

bool
ConfigHelper::validateAndSetNewNodeOverrides(
    const std::string& overrides,
    const std::unordered_set<std::string>& topologyNodeNames,
    folly::dynamic& newNodesOverrides,
    bool overwrite,
    std::string& errorMsg) {

  if (!validateNewNodeOverrides(
          overrides, topologyNodeNames, newNodesOverrides, errorMsg)) {
    return false;
  }

  // Build a copy of nodesOverrides_ with the new overrides
  // (Don't overwrite nodesOverrides_ until we've successfully written the file)
  folly::dynamic fullNodeOverrides = nodesOverrides_;
  for (const auto& pair : newNodesOverrides.items()) {
    std::string nodeName = pair.first.asString();
    folly::dynamic updatedNodeConfig;
    if (!overwrite) {
      // If overwrite flag is false, merge new overrides with existing
      // overrides.
      updatedNodeConfig = fullNodeOverrides.getDefault(nodeName);
      JsonUtils::dynamicObjectMerge(updatedNodeConfig, pair.second);
    } else {
      updatedNodeConfig = pair.second;
    }
    fullNodeOverrides[nodeName] = updatedNodeConfig;
  }

  // Write to node overrides file
  return setNewNodeOverrides(fullNodeOverrides, errorMsg);
}

bool
ConfigHelper::validateNewNodeOverrides(
    const std::string& overrides,
    const std::unordered_set<std::string>& topologyNodeNames,
    folly::dynamic& newNodesOverrides,
    std::string& errorMsg) {
  // Convert JSON to dynamic object
  folly::dynamic requestOverrides = folly::dynamic::object;
  try {
    requestOverrides = folly::parseJson(overrides);
  } catch (const std::exception& ex) {
    errorMsg = "Could not parse config overrides";
    LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
    return false;
  }

  // Validate new node override by creating a node config (using latest base)
  // and try to deserialize it
  std::vector<std::string> unrecognizedConfigs;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  for (const auto& pair : requestOverrides.items()) {
    std::string nodeName = pair.first.asString();
    newNodesOverrides[nodeName] = pair.second;

    // Check if node exists in topology
    if (!topologyNodeNames.count(nodeName)) {
      LOG(ERROR) << "Rejecting new node overrides for unknown node "
                 << nodeName << " (not in topology)";
      errorMsg = "Node " + nodeName + " does not exist in the topology";
      return false;
    }

    // Try to deserialize config
    folly::dynamic fullConfigObj;
    try {
      fullConfigObj = buildNodeConfig(
          nodeName,
          "",
          std::nullopt,
          std::nullopt,
          std::make_optional(autoNodesOverrides_),
          std::make_optional(networkOverrides_),
          std::make_optional(newNodesOverrides));
      thrift::NodeConfig config;
      jsonSerializer.deserialize(folly::toJson(fullConfigObj), config);
    } catch (std::exception& ex) {
      errorMsg = "Could not parse node overrides for " + nodeName;
      LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
      return false;
    }

    // Validate with metadata
    try {
      auto result = nodeConfigMeta_->validate(fullConfigObj);
      folly::dynamic oldNodeOverrides = folly::dynamic::object;
      auto iter = nodesOverrides_.find(nodeName);
      if (iter != nodesOverrides_.items().end()) {
        oldNodeOverrides = iter->second;
      }
      filterValidationResults(
          result,
          JsonUtils::dynamicObjectDifference(oldNodeOverrides, pair.second));

      // Don't allow setting read-only keys
      if (!result.readOnlyKeys.empty()) {
        throw std::invalid_argument(
            "Setting read-only keys: " +
            folly::join(", ", result.readOnlyKeys));
      }

      // Don't allow setting deprecated keys
      if (!result.deprecatedKeys.empty()) {
        throw std::invalid_argument(
            "Setting deprecated keys: " +
            folly::join(", ", result.deprecatedKeys));
      }

      // Allow unrecognized keys, but record them
      for (const std::string& key : result.unrecognizedKeys) {
        unrecognizedConfigs.push_back(nodeName + ":" + key);
      }
    } catch (const std::exception& ex) {
      LOG(ERROR) << "Validation of new node overrides for " << nodeName
                 << " failed: " << folly::exceptionStr(ex);
      errorMsg = nodeName + ": " + ex.what();
      return false;
    }
  }

  // Return an error if no new overrides were given
  if (newNodesOverrides.empty()) {
    errorMsg = "No new node overrides found";
    return false;
  }

  if (!unrecognizedConfigs.empty()) {
    errorMsg = "Unrecognized configs: " +
                 folly::join(", ", unrecognizedConfigs);
    LOG(ERROR) << errorMsg;
  }
  return true;
}

bool
ConfigHelper::setNewNodeOverrides(
    const folly::dynamic& overrides,
    std::string& errorMsg) {
  return setNewPerNodeOverrides(
      overrides, nodeConfigOverridesFile_, nodesOverrides_, errorMsg);
}

bool
ConfigHelper::setNewNodeOverrides(
    const std::string& overrides,
    std::string& errorMsg) {
  folly::dynamic obj = folly::parseJson(overrides);
  return setNewPerNodeOverrides(
      obj, nodeConfigOverridesFile_, nodesOverrides_, errorMsg);
}

bool
ConfigHelper::setNewAutoNodeOverrides(
    const folly::dynamic& overrides,
    std::string& errorMsg) {
  return setNewPerNodeOverrides(
      overrides, autoNodeConfigOverridesFile_, autoNodesOverrides_, errorMsg);
}

bool
ConfigHelper::setNewAutoNodeOverrides(
    const std::string& overrides,
    std::string& errorMsg) {
  folly::dynamic obj = folly::parseJson(overrides);
  return setNewPerNodeOverrides(
      obj, autoNodeConfigOverridesFile_, autoNodesOverrides_, errorMsg);
}

bool
ConfigHelper::setNewPerNodeOverrides(
    const folly::dynamic& overrides,
    const std::string& nodeConfigOverridesFile,
    folly::dynamic& nodeOverrides,
    std::string& errorMsg) {
  // Write backup of per-node overrides file
  writeBackupConfigFile(nodeConfigOverridesFile, nodeOverrides);

  // Write to per-node overrides file
  try {
    JsonUtils::writeDynamicObject2JsonFile(overrides, nodeConfigOverridesFile);
    nodeOverrides =
        JsonUtils::readJsonFile2DynamicObject(nodeConfigOverridesFile);
  } catch (const std::invalid_argument& ex) {
    errorMsg = "Unable to write per-node config overrides to " +
        nodeConfigOverridesFile;
    LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
    return false;
  }
  configState_.clear();
  return true;
}

bool
ConfigHelper::validateAndSetNewNetworkOverrides(
    const std::string& overrides,
    folly::dynamic& newNetworkOverrides,
    bool overwrite,
    std::string& errorMsg) {

  if (!validateNewNetworkOverrides(
          overrides, newNetworkOverrides, errorMsg)) {
    return false;
  }

  std::string networkOverrides;
  if (!overwrite) {
    // Merge requested overrides into a copy of the existing network overrides.
    folly::dynamic currentNetworkOverrides = networkOverrides_;
    JsonUtils::dynamicObjectMerge(currentNetworkOverrides, newNetworkOverrides);
    networkOverrides = folly::toJson(currentNetworkOverrides);
  } else {
    // Otherwise, replace existing overrides with provided overrides.
    networkOverrides = overrides;
  }

  return setNewNetworkOverrides(networkOverrides, errorMsg);
}

bool
ConfigHelper::validateNewNetworkOverrides(
    const std::string& overrides,
    folly::dynamic& newNetworkOverrides,
    std::string& errorMsg) {
  // Convert JSON to dynamic object
  try {
    newNetworkOverrides = folly::parseJson(overrides);
  } catch (const std::exception& ex) {
    errorMsg = "Could not parse network config overrides";
    LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
    return false;
  }

  // Validate new network override
  // Create a fake node config and try to deserialize it
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  folly::dynamic fullConfigObj;
  try {
    fullConfigObj = buildNodeConfig(
        std::nullopt,
        "",
        std::nullopt,
        std::nullopt,
        std::make_optional(autoNodesOverrides_),
        std::make_optional(newNetworkOverrides),
        std::make_optional(nodesOverrides_));
    thrift::NodeConfig config;
    jsonSerializer.deserialize(folly::toJson(fullConfigObj), config);
  } catch (const std::exception& ex) {
    errorMsg = "Could not serialize network config overrides";
    LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
    return false;
  }

  // Validate with metadata
  try {
    auto result = nodeConfigMeta_->validate(fullConfigObj);
    filterValidationResults(
        result,
        JsonUtils::dynamicObjectDifference(
            networkOverrides_, newNetworkOverrides));

    // Don't allow setting read-only keys
    if (!result.readOnlyKeys.empty()) {
      throw std::invalid_argument(
          "Setting read-only keys: " +
          folly::join(", ", result.readOnlyKeys));
    }

    // Don't allow setting deprecated keys
    if (!result.deprecatedKeys.empty()) {
      throw std::invalid_argument(
          "Setting deprecated keys: " +
          folly::join(", ", result.deprecatedKeys));
    }

    // Allow unrecognized keys, but record them
    if (!result.unrecognizedKeys.empty()) {
      errorMsg = "Unrecognized configs: " +
                   folly::join(", ", result.unrecognizedKeys);
      LOG(ERROR) << errorMsg;
    }
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Validation of new network overrides failed: "
               << folly::exceptionStr(ex);
    errorMsg = ex.what();
    return false;
  }
  return true;
}

void
ConfigHelper::writeBackupConfigFile(
    const std::string& configPath, const folly::dynamic& config) const {
  // Write backup of config file
  try {
    // Take filename of file and the current timestamp
    boost::filesystem::path path(configPath);
    std::string filename = path.filename().string();

    // Create backup file name and write to it
    std::string timestamp = std::to_string(std::time(nullptr));
    std::string backupConfigPath =
        folly::sformat("{}.{}", configBackupDir_ + filename, timestamp);
    JsonUtils::writeDynamicObject2JsonFile(
        config,
        backupConfigPath);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Unable to write config backup: " << folly::exceptionStr(ex);
  }
}

bool
ConfigHelper::setNewNetworkOverrides(
    const std::string& overrides,
    std::string& errorMsg) {
  // Write backup of network overrides file
  writeBackupConfigFile(networkConfigOverridesFile_, networkOverrides_);

  // Write to network overrides file
  try {
    JsonUtils::writeString2JsonFile(
        overrides, networkConfigOverridesFile_);
    networkOverrides_ = JsonUtils::readJsonFile2DynamicObject(
        networkConfigOverridesFile_);
  } catch (const std::invalid_argument& ex) {
    errorMsg = "Unable to write network config overrides";
    LOG(ERROR) << errorMsg << ": " << folly::exceptionStr(ex);
    return false;
  }
  configState_.clear();
  return true;
}

std::optional<std::string>
ConfigHelper::getNetworkOverridesJson() const{
  try {
    return folly::toJson(networkOverrides_);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse network overrides "
               << folly::exceptionStr(ex);
  }
  return std::nullopt;
}

std::optional<std::string>
ConfigHelper::getNodeOverridesJson(
    const std::vector<std::string>& nodeNames) const {
  return getPerNodeOverridesJson(nodeNames, nodesOverrides_);
}

std::optional<std::string>
ConfigHelper::getAutoNodeOverridesJson(
    const std::vector<std::string>& nodeNames) const {
  return getPerNodeOverridesJson(nodeNames, autoNodesOverrides_);
}

std::optional<std::string>
ConfigHelper::getPerNodeOverridesJson(
    const std::vector<std::string>& nodeNames,
    const folly::dynamic& nodeOverrides) const {
  try {
    if (nodeNames.empty()) {
      return folly::toJson(nodeOverrides);
    } else {
      folly::dynamic obj = folly::dynamic::object;
      for (auto const& nodeName : nodeNames) {
        obj[nodeName] = "";
        if (nodeOverrides.isObject()) {
          auto iter = nodeOverrides.find(nodeName);
          if (iter != nodeOverrides.items().end()) {
            obj[nodeName] = nodeOverrides[nodeName];
          }
        }
      }
      return folly::toJson(obj);
    }
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse node overrides " << folly::exceptionStr(ex);
  }
  return std::nullopt;
}

std::string
ConfigHelper::getBaseConfigJson(const std::vector<std::string>& swVersions) {
  // Get requested software versions
  std::vector<std::string> requestedSwVersions(swVersions);
  if (swVersions.empty()) {  // get all available software versions
    for (const auto& pair : baseConfigObjects_.items()) {
      requestedSwVersions.push_back(pair.first.asString());
    }
  }

  // Add software versions
  folly::dynamic obj = folly::dynamic::object;
  for (const auto& ver : requestedSwVersions) {
    obj[ver] = getBaseConfig(
        SwVersion(ver), baseConfigObjects_, latestBaseVersion_, false);
  }
  return folly::toJson(obj);
}

std::string
ConfigHelper::getFirmwareBaseConfigJson(
    const std::vector<std::string>& fwVersions) {
  // Get requested firmware versions
  std::vector<std::string> requestedFwVersions(fwVersions);
  if (fwVersions.empty()) {  // get all available firmware versions
    for (const auto& pair : fwConfigObjects_.items()) {
      requestedFwVersions.push_back(pair.first.asString());
    }
  }

  // Add firmware versions
  folly::dynamic obj = folly::dynamic::object;
  for (const auto& ver : requestedFwVersions) {
    obj[ver] = getFirmwareConfig(ver, fwConfigObjects_);
  }
  return folly::toJson(obj);
}

std::string
ConfigHelper::getHardwareBaseConfigJson(
    const std::vector<std::string>& hwBoardIds,
    const std::vector<std::string>& swVersions) {
  // Get requested hardware board IDs
  std::vector<std::string> requestedHwBoardIds(hwBoardIds);
  if (hwBoardIds.empty()) {  // get all available hardware board IDs
    for (const auto& kv : hardwareConfigTypeMap_) {
      requestedHwBoardIds.push_back(kv.first);
    }
  }

  // Add hardware board IDs
  folly::dynamic obj = folly::dynamic::object;
  for (const std::string& hwBoardId : requestedHwBoardIds) {
    folly::dynamic& hwObj = obj.setDefault(hwBoardId, folly::dynamic::object);

    auto hwConfigTypeIter = hardwareConfigTypeMap_.find(hwBoardId);
    if (hwConfigTypeIter == hardwareConfigTypeMap_.end()) {
      continue;  // given hardware board ID doesn't exist
    }
    auto hwConfigIter = hwConfigObjects_.find(hwConfigTypeIter->second);
    if (hwConfigIter == hwConfigObjects_.items().end()) {
      continue;  // given hardware config type doesn't exist
    }

    // Get requested software versions
    std::vector<std::string> requestedSwVersions(swVersions);
    if (swVersions.empty()) {  // get all available software versions
      for (const auto& pair : hwConfigIter->second.items()) {
        requestedSwVersions.push_back(pair.first.asString());
      }
    }

    // Add software versions
    auto hwVerIter = latestHwBaseVersions_.find(hwConfigTypeIter->second);
    std::string hwDefaultVer = (hwVerIter != latestHwBaseVersions_.end())
        ? hwVerIter->second : "";
    for (const auto& ver : requestedSwVersions) {
      hwObj[ver] = getBaseConfig(
          SwVersion(ver), hwConfigIter->second, hwDefaultVer, true);
    }
  }
  return folly::toJson(obj);
}

std::optional<ConfigHelper::NodeConfigState>
ConfigHelper::getConfigState(const std::string& nodeName) const {
  auto it = configState_.find(nodeName);
  if (it == configState_.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::string
ConfigHelper::getLatestBaseVersion() const {
  return latestBaseVersion_;
}

std::unordered_map<std::string, ConfigHelper::NodeConfigState>
ConfigHelper::getAllConfigStates() const {
  return configState_;
}

folly::dynamic
ConfigHelper::loadConfigFile(
    const std::string& path, const std::optional<std::string> swVersion) {
  // Load config file
  folly::dynamic cfg = folly::dynamic::object;
  try {
    cfg = JsonUtils::readJsonFile2DynamicObject(path);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to read base config file: " << path;
    throw;
  }
  if (!cfg.isObject()) {
    LOG(ERROR) << "Base config file is not a JSON object: " << path;
    throw std::invalid_argument("Not a JSON object");
  }

  // Try to deserialize the base config
  folly::dynamic fullConfigObj;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    if (swVersion) {
      fullConfigObj = buildNodeConfig(
          std::nullopt,
          swVersion.value(),
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt,
          std::nullopt);
    } else {
      fullConfigObj = cfg;
    }

    thrift::NodeConfig config;
    jsonSerializer.deserialize(folly::toJson(fullConfigObj), config);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not serialize base config " << path << ": "
               << folly::exceptionStr(ex);
    throw;
  }

  // Validate the base config
  try {
    nodeConfigMeta_->validate(fullConfigObj);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Validation of base config " << path << " failed: "
               << folly::exceptionStr(ex);
    throw;
  }

  LOG(INFO) << "Loaded base config: " << path;
  return cfg;
}

std::optional<ConfigHelper::NodeConfigState>
ConfigHelper::initConfigState(
    const std::string& nodeName,
    const std::string& swVersion,
    const std::string& fwVersion,
    const std::string& hwBoardId) {
  auto& nodeState = configState_[nodeName];
  try {
    auto nodeConfig = buildNodeConfig(
        nodeName,
        swVersion,
        fwVersion,
        hwBoardId,
        std::make_optional(autoNodesOverrides_),
        std::make_optional(networkOverrides_),
        std::make_optional(nodesOverrides_));

    nodeState.swVersion = swVersion;
    nodeState.fwVersion = fwVersion;
    nodeState.hwBoardId = hwBoardId;
    nodeState.configObj = nodeConfig;
    nodeState.configJson = JsonUtils::toSortedPrettyJson(nodeConfig);
    nodeState.isManaged = isManaged(nodeConfig);
    nodeState.md5 = Md5Utils::computeMd5(nodeState.configJson);

    // Check if this hardware type and version are recognized
    nodeState.isUnknownHardware = isUnknownHardware(hwBoardId, swVersion);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Could not parse node config for " << nodeName << ": "
               << folly::exceptionStr(ex);
    configState_.erase(nodeName);
    return std::nullopt;
  }
  return nodeState;
}

bool
ConfigHelper::isUnknownHardware(
    const std::string& hwBoardId, const std::string& swVersion) const {
  // Look up in hardware types map
  auto hwConfigTypeIter = hardwareConfigTypeMap_.find(hwBoardId);
  if (hwConfigTypeIter != hardwareConfigTypeMap_.end()) {
    // Look up in hardware configs map
    auto hwConfigIter = hwConfigObjects_.find(hwConfigTypeIter->second);
    if (hwConfigIter != hwConfigObjects_.items().end()) {
      // Check if software version matches anything
      SwVersion version(swVersion);
      if (matchBaseVersion(version, hwConfigIter->second)) {
        return false;
      }
    }
  }
  return true;
}

void
ConfigHelper::updateNodeIsUnknownHardware(const std::string& nodeName) {
  auto it = configState_.find(nodeName);
  if (it != configState_.end()) {
    auto& configState = it->second;
    bool prevVal = configState.isUnknownHardware;
    bool newVal =
        isUnknownHardware(configState.hwBoardId, configState.swVersion);
    if (!newVal && prevVal) {
      // Re-initialize node config (e.g. we have received new hardware config)
      VLOG(3) << "Re-initializing config state for " << nodeName
              << " (no longer unknown hardware)";
      initConfigState(
          nodeName,
          configState.swVersion,
          configState.fwVersion,
          configState.hwBoardId);
    } else {
      it->second.isUnknownHardware = newVal;
    }
  }
}

void
ConfigHelper::setNodeStatusTime(
    const std::string& nodeName, const std::int64_t statusTime) {
  auto it = configState_.find(nodeName);
  if (it != configState_.end()) {
    it->second.statusTime = statusTime;
  }
}

void
ConfigHelper::setNodeConfigTime(
    const std::string& nodeName, const std::int64_t configTime) {
  auto it = configState_.find(nodeName);
  if (it != configState_.end()) {
    it->second.configTime = configTime;
  }
}

void
ConfigHelper::setNodeBaseConfigRequestedTime(
    const std::string& nodeName, const std::int64_t baseConfigRequestedTime) {
  auto it = configState_.find(nodeName);
  if (it != configState_.end()) {
    it->second.baseConfigRequestedTime = baseConfigRequestedTime;
  }
}

bool
ConfigHelper::isManaged(const folly::dynamic& config) const {
  if (config.isObject()) {
    auto sysParamsIter = config.find("sysParams");
    if (sysParamsIter != config.items().end() &&
        sysParamsIter->second.isObject()) {
      auto managedConfigIter = sysParamsIter->second.find("managedConfig");
      if (managedConfigIter != sysParamsIter->second.items().end()) {
        return managedConfigIter->second.asBool();
      }
    }
  }
  return false;
}

bool
ConfigHelper::migrateNodeOverrides(
    const std::string& oldNodeName, const std::string& newNodeName) {
  // Migrate user node overrides
  auto success = migratePerNodeOverrides(
      oldNodeName, newNodeName, nodesOverrides_, nodeConfigOverridesFile_);
  if (success) {
    LOG(INFO) << "Migrated node overrides config from " << oldNodeName << " to "
              << newNodeName;
  } else {
    return false;  // migration failed, don't migrate other files
  }

  // Migrate automatic node overrides
  success = migratePerNodeOverrides(
      oldNodeName,
      newNodeName,
      autoNodesOverrides_,
      autoNodeConfigOverridesFile_);
  if (success) {
    LOG(INFO) << "Migrated automatic node overrides config from " << oldNodeName
              << " to " << newNodeName;
  }

  return success;
}

bool
ConfigHelper::migratePerNodeOverrides(
    const std::string& oldNodeName,
    const std::string& newNodeName,
    folly::dynamic& nodeOverrides,
    const std::string& nodeConfigOverridesFile) {
  if (!nodeOverrides.count(oldNodeName)) {
    return true;  // no previous config, nothing to migrate
  }

  // Build a copy of nodeOverrides with the new overrides
  // (Don't overwrite nodeOverrides until we've successfully written the file)
  folly::dynamic fullNodeOverrides = nodeOverrides;
  auto configIter = fullNodeOverrides.find(oldNodeName);
  fullNodeOverrides[newNodeName] = configIter->second;
  fullNodeOverrides.erase(configIter);

  // Write to node overrides file
  try {
    JsonUtils::writeDynamicObject2JsonFile(
        fullNodeOverrides, nodeConfigOverridesFile);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Unable to write node config overrides: "
               << folly::exceptionStr(ex);
    return false;
  }

  nodeOverrides = fullNodeOverrides;
  LOG(INFO) << "Migrated config from " << oldNodeName << " to " << newNodeName;
  configState_.erase(oldNodeName);
  return true;
}

std::optional<std::string>
ConfigHelper::getConfigMetadata() const {
  try {
    return folly::toJson(nodeConfigMeta_->get());
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse config metadata: "
               << folly::exceptionStr(ex);
  }
  return std::nullopt;
}

bool
ConfigHelper::setAutoNodeSpecificNetworkPrefixes(
    const std::string& nodeName,
    const std::optional<std::string> specificNetworkPrefixes,
    std::string& errorMsg) {
  if (specificNetworkPrefixes) {
    VLOG(4) << folly::format(
        "Setting `specificNetworkPrefixes` automatic node override for "
        "node `{}` to `{}`",
        nodeName,
        specificNetworkPrefixes.value());
  } else {
    VLOG(4) << folly::format(
        "Clearing `specificNetworkPrefixes` automatic node override for "
        "node `{}`",
        nodeName);
  }

  auto currentSpecificNetworkPrefixes = getAutoNodeSpecificNetworkPrefixes(
      nodeName);
  // Exit early if the new specificNetworkPrefixes matches what is currently
  // stored in config
  if (specificNetworkPrefixes == currentSpecificNetworkPrefixes) {
    return true;
  }

  // Otherwise, need to update the stored specificNetworkPrefixes
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  folly::dynamic& bgpParams = getDynamicObjectAtPath(
      newAutoNodesOverrides, {nodeName, "bgpParams"});
  if (!specificNetworkPrefixes) {
    // Erase specificNetworkPrefixes if passed std::nullopt
    bgpParams.erase("specificNetworkPrefixes");
  } else {
    // Write new specificNetworkPrefixes
    bgpParams["specificNetworkPrefixes"] = specificNetworkPrefixes.value();
  }
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

bool
ConfigHelper::setAutoNodeSpecificTunnelConfigIp(
    const std::string& nodeName,
    const std::unordered_map<std::string, std::optional<std::string>>&
        tunnelConfigUpdates,
    std::string& errorMsg) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  for (const auto& tunnelConfig : tunnelConfigUpdates) {
    auto& tunnelName = tunnelConfig.first;
    auto& dstIp = tunnelConfig.second;
    folly::dynamic& autoTunnelConfig = getDynamicObjectAtPath(
        newAutoNodesOverrides, {nodeName, "tunnelConfig", tunnelName});
    if (dstIp) {
      autoTunnelConfig["dstIp"] = *dstIp;
      VLOG(4) << "Updated dstIp to \"" << *dstIp << "\" on node \"" << nodeName
              << "\" for tunnel \"" << tunnelName << "\"";
    } else {
      autoTunnelConfig.erase("dstIp");
      VLOG(4) << "Removed dstIp on node \"" << nodeName << "\" for tunnel \""
              << tunnelName << "\"";
    }
  }
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

void
ConfigHelper::onIpAddrChange(
    const thrift::Node& node, const std::string& ipv6Address) {
  // skip empty IP address changes
  if (ipv6Address.empty()) {
    return;
  }
  std::string errorMsg;
  // iterate over all node configs looking for tunnelConfig params
  for (const auto& nodeConfigOverrides : nodesOverrides_.items()) {
    const std::string nodeName = nodeConfigOverrides.first.asString();
    std::unordered_map<std::string, std::optional<std::string>>
      tunnelConfigUpdates;
    const folly::dynamic& tunnelConfig =
        getDynamicObjectAtPath(nodesOverrides_, {nodeName, "tunnelConfig"});
    for (const auto& tunnel : tunnelConfig.items()) {
      auto dstNodeNameIt = tunnel.second.find("dstNodeName");
      if (dstNodeNameIt != tunnel.second.items().end() &&
          dstNodeNameIt->second == node.name) {
        // set 'dstIp' to the new IP address
        tunnelConfigUpdates[tunnel.first.asString()] = ipv6Address;
      }
    }
    if (!tunnelConfigUpdates.empty()) {
      setAutoNodeSpecificTunnelConfigIp(
          nodeName, tunnelConfigUpdates, errorMsg);
    }
  }
}

std::optional<std::string>
ConfigHelper::getAutoNodeSpecificNetworkPrefixes(const std::string& nodeName) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  folly::dynamic& bgpParams = getDynamicObjectAtPath(
      newAutoNodesOverrides, {nodeName, "bgpParams"});

  auto iter = bgpParams.find("specificNetworkPrefixes");
  if (iter == bgpParams.items().end()) {
    return std::nullopt;
  }

  return iter->second.getString();
}

bool
ConfigHelper::setAutoNodeOpenrStaticPrefixAlloc(
    const std::string& nodeName,
    const bool openrStaticPrefixAllocEnabled,
    std::string& errorMsg) {
  VLOG(4) << folly::format(
      "Setting `OPENR_STATIC_PREFIX_ALLOC` automatic node override for node "
      "`{}` to `{}`",
      nodeName,
      openrStaticPrefixAllocEnabled);

  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  folly::dynamic& envParams = getDynamicObjectAtPath(
      newAutoNodesOverrides, {nodeName, "envParams"});
  envParams["OPENR_STATIC_PREFIX_ALLOC"] =
      openrStaticPrefixAllocEnabled ? "true" : "false";
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

bool
ConfigHelper::setNodeTopologyInfo(
    const std::vector<std::string>& nodeNames,
    const TopologyWrapper& topologyW,
    std::string& errorMsg) {
  folly::dynamic newAutoNodeOverrides = autoNodesOverrides_;

  // Get each radio's color assignment used for distributed ignition.
  auto colors = getColorAssignments(topologyW);

  // Create a map of POP nodes to Node IDs of nodes in the same zone.
  auto zones = topologyW.getPrefixAllocZones();
  std::unordered_map<std::string, std::string> zoneNodes;

  // Only populate zone nodes if DPA is currently enabled.
  if (topologyW.getControllerPrefixAllocScheme() ==
      ControllerPrefixAllocScheme::DETERMINISTIC_PREFIX_ALLOCATION
      && zones) {
    for (const auto& zone : *zones) {
      std::set<std::string> popNodes;
      std::set<std::string> zoneNodeIDs;

      for (const auto& nodeName : zone.second.node_names) {
        auto node = topologyW.getNode(nodeName);
        if (!node) {
          LOG(ERROR) << "Unable to find node info for node name: " << nodeName;
          continue;
        }
        if (node->pop_node) {
          popNodes.insert(nodeName);
        }
        // Node expects Open/R names.
        zoneNodeIDs.insert(
          OpenrUtils::toOpenrNodeName(node->mac_addr));
      }
      std::string zoneNodesStr = folly::join(",", zoneNodeIDs);
      // Mapping for all POP nodes in the zone.
      for (const auto& popNode : popNodes) {
        zoneNodes[popNode] = zoneNodesStr;
      }
    }
  }

  for (const auto& nodeName : nodeNames) {
    // Get a reference to topologyInfo. As this object is updated,
    // node config will be written to.
    folly::dynamic& topologyInfoRef = getDynamicObjectAtPath(
        newAutoNodeOverrides, {nodeName, "topologyInfo"});

    // Convert topology info folly::dynamic object into thrift
    // TopologyInfo struct.
    std::string json = folly::toJson(topologyInfoRef);
    auto topologyInfo =
        JsonUtils::deserializeFromJson<thrift::TopologyInfo>(json);
    if (!topologyInfo.has_value()) {
      topologyInfo = thrift::TopologyInfo();
    }

    // Fill in parameters from topology.
    topologyInfo->nodeName = nodeName;
    topologyInfo->topologyName = topologyW.getTopologyName();
    auto node = topologyW.getNode(nodeName);
    if (!node) {
      LOG(ERROR) << "Unable to find node info for node name: " << nodeName;
      continue;
    }
    topologyInfo->nodeType_ref() = node->node_type;
    // Only DNs have colors because only DNs will begin ignition.
    auto iter = colors.find(node->name);
    if (iter != colors.end()) {
      auto& radioColors = iter->second;
      std::unordered_map<std::string, thrift::RadioInfo> radiosInfo;
      for (const auto& kv : radioColors) {
        std::string radioMac = kv.first;
        int color = kv.second;

        thrift::RadioInfo radioInfo;
        radioInfo.distributedIgnitionColor_ref() = color;
        radiosInfo[radioMac] = radioInfo;
      }
      topologyInfo->radioInfo_ref() = radiosInfo;
      topologyInfo->distributedIgnitionNumColors_ref() = kNumColors;
    }
    auto site = topologyW.getSite(node->site_name);
    if (!site) {
      LOG(ERROR) << "Unable to find site info for node name: " << nodeName;
      continue;
    }
    topologyInfo->site = site.value();
    auto neighborInfo = getNeighborInfo(topologyW, node.value());
    if (neighborInfo) {
      topologyInfo->neighborInfo_ref() = neighborInfo.value();
    } else {
      topologyInfo->neighborInfo_ref().reset();
    }

    // Fill in nodes in the POP's zone
    auto zoneNodesIter = zoneNodes.find(nodeName);
    if (node->pop_node && zoneNodesIter != zoneNodes.end()) {
      topologyInfo->zoneNodes_ref() = zoneNodesIter->second;
    } else {
      topologyInfo->zoneNodes_ref() = "";
    }

    // Convert back to folly::dynamic
    std::string serializedTopologyInfo =
        JsonUtils::serializeToJson<thrift::TopologyInfo>(topologyInfo.value());
    topologyInfoRef = folly::parseJson(serializedTopologyInfo);
  }
  return setNewAutoNodeOverrides(newAutoNodeOverrides, errorMsg);
}

std::optional<std::unordered_map<std::string, thrift::NeighborInfo>>
ConfigHelper::getNeighborInfo(
    const TopologyWrapper& topologyW, thrift::Node& node) {
  // Ignore any node which is not a DN (CNs cannot ignite links).
  if (node.node_type != thrift::NodeType::DN) {
    return std::nullopt;
  }

  std::unordered_map<std::string /* responderMac */, thrift::NeighborInfo>
      neighborInfoMap;

  // Build up information about neighbor responder nodes for storage in
  // this node's topology info.
  auto links = topologyW.getLinksByNodeName(node.name);
  for (const auto& link : links) {
    if (link.link_type != thrift::LinkType::WIRELESS) {
      // Skip links that are not wireless (non-wireless links don't
      // undergo ignition).
      continue;
    }
    if (link.is_backup_cn_link_ref().value_or(false)) {
      // Don't save information about backup links in config.
      continue;
    }

    // Get information about link responder node.
    std::string neighborNodeName;
    std::string neighborMac;
    std::string initiatorMac;
    if (node.name == link.a_node_name) {
      neighborNodeName = link.z_node_name;
      neighborMac = link.z_node_mac;
      initiatorMac = link.a_node_mac;
    } else {
      neighborNodeName = link.a_node_name;
      neighborMac = link.a_node_mac;
      initiatorMac = link.z_node_mac;
    }
    auto neighborNode = topologyW.getNode(neighborNodeName);
    if (!neighborNode) {
      continue;
    }
    auto initiatorLinkConfig = getLinkTopologyConfig(node.name, neighborMac);
    auto responderPolarity =
        getRadioPolarity(neighborNodeName, neighborMac, false);

    thrift::NeighborInfo neighborInfo;
    neighborInfo.initiatorMac = initiatorMac;
    if (initiatorLinkConfig.controlSuperframe.has_value()) {
      neighborInfo.controlSuperframe_ref() =
          initiatorLinkConfig.controlSuperframe.value();
    }
    neighborInfo.nodeType_ref() = neighborNode->node_type;
    if (initiatorLinkConfig.golayIdx.has_value()) {
      neighborInfo.golayIdx_ref() = initiatorLinkConfig.golayIdx.value();
    }
    if (responderPolarity.has_value()) {
      neighborInfo.nodePolarity_ref() = responderPolarity.value();
    }

    neighborInfoMap[neighborMac] = neighborInfo;
  }

  // Return info about neighbors if any neighbors exist.
  if (!neighborInfoMap.empty()) {
    return neighborInfoMap;
  }
  return std::nullopt;
}

std::unordered_map<std::string /* nodeName */,
    std::unordered_map<std::string /* radioMac */, int /* color */>>
ConfigHelper::getColorAssignments(const TopologyWrapper& topologyW) {
  std::unordered_map<std::string,
      std::unordered_map<std::string, int>> colors;

  // Determine the number of colors to use per polarity type (there are two
  // polarities, EVEN (including HYBRID_EVEN) and ODD (including HYBRID_ODD).
  int colorsPerPolarity = kNumColors / 2;

  // For each site, assign colorsPerPolarity different colors to the radios.
  for (const auto& site : topologyW.getAllSites()) {
    auto nodes = topologyW.getNodesBySiteName(site.name);
    if (nodes.empty()) {
      continue;
    }

    // Calculate angle of each node at the site.
    std::unordered_map<std::string /* nodeMac */, double /* angle */>
        nodeAngles;
    for (const auto& node : nodes) {
      auto links = topologyW.getLinksByNodeName(node.name);

      // Get location of a site this node has a link to.
      for (const auto& link : links) {
        auto neighbor = topologyW.getNbrNode(node.name, link.name);
        if (!neighbor) {
          continue;
        }
        // Ignore the link if it doesn't go to another site.
        if (link.link_type != thrift::LinkType::WIRELESS) {
          continue;
        }
        auto neighborSite = topologyW.getSite(neighbor->site_name);
        if (!neighborSite) {
          continue;
        }
        if (neighborSite) {
          nodeAngles[node.name] =
              computeAngle(site.location, neighborSite->location);
          break;
        }
      }
    }

    // Sort list of nodes at this site based on angle.
    std::sort(nodes.begin(), nodes.end(),
        [&nodeAngles](thrift::Node& n1, thrift::Node& n2) {
      if (!nodeAngles.count(n1.name)) {
        // If n1 has no recorded angle, n2 should come first.
        return false;
      }
      if (!nodeAngles.count(n2.name)) {
        // If n2 has no recorded angle, n1 should come first.
        return true;
      }
      // Order by node angle, ascending.
      return nodeAngles[n1.name] < nodeAngles[n2.name];
    });

    // Iterate through nodes at site in ascending order of angle,
    // assigning colors. Ideally this will space out color assignments
    // and limit interference between nodes at the same site with the
    // same color.
    int offset = 0;
    for (const auto& node : nodes) {
      if (node.node_type != thrift::NodeType::DN) {
        continue;
      }
      colors[node.name] = {};
      for (const auto& radioMac : node.wlan_mac_addrs) {
        auto polarity = getRadioPolarity(node.name, radioMac, false);
        if (!polarity) {
          continue;
        }
        // Assume two different polarity types (EVEN and ODD). Assign
        // start index based on polarity of node and number of colors. For
        // example, if kNumColors is 4, assign each polarity two colors, with
        // EVEN getting colors 0 and 2, and ODD getting colors 1 and 3.
        int color = (PolarityHelper::isOddPolarity(polarity.value()) ? 1 : 0)
            + offset * colorsPerPolarity;
        colors[node.name][radioMac] = color;

        offset = (offset + 1) % colorsPerPolarity;
      }
    }
  }
  return colors;
}

bool
ConfigHelper::setNodePolarity(
    const std::string& nodeName,
    const std::optional<std::string> radioMac,
    const std::optional<thrift::PolarityType> polarity,
    bool forUserConfig,
    std::string& errorMsg) {
  VLOG(4) << folly::format(
      "Setting polarity in {} node override for radioMac `{}` on node "
      "`{}` to {}",
      forUserConfig ? "user" : "auto",
      nodeName,
      radioMac ? *radioMac : "",
      polarity ? std::to_string(static_cast<int64_t>(*polarity)) : "None");

  folly::dynamic newNodesOverrides =
      forUserConfig ? nodesOverrides_ : autoNodesOverrides_;

  std::deque<std::string> path;
  if (radioMac) {
    path = {nodeName, "radioParamsOverride", getStandardizedMac(*radioMac),
        "fwParams"};
  } else {
    path = {nodeName, "radioParamsBase", "fwParams"};
  }

  folly::dynamic& fwParams = getDynamicObjectAtPath(
      newNodesOverrides, path);
  if (polarity) {
    fwParams["polarity"] = static_cast<int64_t>(*polarity);
  } else {
    fwParams.erase("polarity");
  }

  if (forUserConfig) {
    return setNewNodeOverrides(newNodesOverrides, errorMsg);
  } else {
    return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
  }
}

bool
ConfigHelper::setNodePolarities(
    std::unordered_map<std::string, std::optional<thrift::PolarityType>>
        mac2Polarity,
    std::map<std::string, std::string> mac2NodeName,
    bool forUserConfig,
    std::string& errorMsg) {

  // Change user-configured overrides
  folly::dynamic newNodesOverrides;
  bool success = true;
  if (!mac2Polarity.empty()) {
    newNodesOverrides = forUserConfig ? nodesOverrides_ : autoNodesOverrides_;
    for (const auto& polarityIt : mac2Polarity) {
      auto nodeNameIt = mac2NodeName.find(polarityIt.first);
      if (nodeNameIt == mac2NodeName.end()) {
        continue;
      }

      folly::dynamic& fwParams = getDynamicObjectAtPath(
          newNodesOverrides,
          {nodeNameIt->second, "radioParamsOverride",
           getStandardizedMac(polarityIt.first), "fwParams"});
      if (polarityIt.second.has_value()) {
        fwParams["polarity"] = static_cast<int64_t>(polarityIt.second.value());
      } else {
        fwParams.erase("polarity");
      }
    }

    if (forUserConfig) {
      success = setNewNodeOverrides(newNodesOverrides, errorMsg);
    } else {
      success = setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
    }
  }

  return success;
}

bool
ConfigHelper::setLinkControlSuperframe(
    const thrift::Link& link,
    const std::optional<int64_t> controlSuperframe,
    bool forUserConfig,
    std::string& errorMsg) {
  VLOG(4) << folly::format(
      "Setting controlSuperframe {} node override for nodes "
      "`{}` with mac `{}` and `{}` with mac `{}` to {}",
      forUserConfig ? "user" : "auto",
      link.a_node_name,
      link.a_node_mac,
      link.z_node_name,
      link.z_node_mac,
      controlSuperframe ? std::to_string(*controlSuperframe) : "None");

  folly::dynamic newNodesOverrides =
      forUserConfig ? nodesOverrides_ : autoNodesOverrides_;
  if (!link.z_node_mac.empty()) {
    setLinkControlSuperframeForNode(
        newNodesOverrides,
        link.a_node_name,
        link.z_node_mac,
        controlSuperframe);
  }

  if (!link.a_node_mac.empty()) {
    setLinkControlSuperframeForNode(
        newNodesOverrides,
        link.z_node_name,
        link.a_node_mac,
        controlSuperframe);
  }

  if (forUserConfig) {
    return setNewNodeOverrides(newNodesOverrides, errorMsg);
  } else {
    return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
  }
}

void
ConfigHelper::setLinkControlSuperframeForNode(
    folly::dynamic& nodesOverrides,
    const std::string& nodeName,
    const std::string& responderMac,
    const std::optional<int64_t> controlSuperframe) {
  folly::dynamic& fwParams = getDynamicObjectAtPath(
      nodesOverrides,
      {nodeName, "linkParamsOverride", getStandardizedMac(responderMac),
          "fwParams"});
  if (controlSuperframe) {
    fwParams["controlSuperframe"] = *controlSuperframe;
  } else {
    fwParams.erase("controlSuperframe");
  }
}

bool
ConfigHelper::setLinkGolay(
    const thrift::Link& link,
    const std::optional<thrift::GolayIdx> golayIdx,
    bool forUserConfig,
    std::string& errorMsg) {
  VLOG(4) << folly::format(
      "Setting golay {} node override for nodes "
      "`{}` with mac `{}` and `{}` with mac `{}` to {}",
      forUserConfig ? "user" : "auto",
      link.a_node_name,
      link.a_node_mac,
      link.z_node_name,
      link.z_node_mac,
      golayIdx ? std::to_string(golayIdx->txGolayIdx) : "None");

  folly::dynamic newNodesOverrides =
      forUserConfig ? nodesOverrides_ : autoNodesOverrides_;
  if (!link.z_node_mac.empty()) {
    setLinkGolayForNode(
        newNodesOverrides, link.a_node_name, link.z_node_mac, golayIdx);
  }

  if (!link.a_node_mac.empty()) {
    setLinkGolayForNode(
        newNodesOverrides, link.z_node_name, link.a_node_mac, golayIdx);
  }

  if (forUserConfig) {
    return setNewNodeOverrides(newNodesOverrides, errorMsg);
  } else {
    return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
  }
}

void
ConfigHelper::setLinkGolayForNode(
    folly::dynamic& nodesOverrides,
    const std::string& nodeName,
    const std::string& responderMac,
    const std::optional<thrift::GolayIdx> golayIdx) {
  folly::dynamic& fwParams = getDynamicObjectAtPath(
      nodesOverrides,
      {nodeName, "linkParamsOverride", getStandardizedMac(responderMac),
          "fwParams"});

  if (!golayIdx) {
    fwParams.erase("txGolayIdx");
    fwParams.erase("rxGolayIdx");
    return;
  }

  fwParams["txGolayIdx"] = golayIdx->txGolayIdx;
  fwParams["rxGolayIdx"] = golayIdx->rxGolayIdx;
}

bool
ConfigHelper::setLinkChannel(
    const thrift::Link& link,
    const std::optional<int8_t> channel,
    bool forUserConfig,
    std::string& errorMsg) {
  LOG(INFO) << folly::format(
      "Setting channel {} node override for nodes "
      "`{}` with mac `{}` and `{}` with mac `{}` to {}",
      forUserConfig ? "user" : "auto",
      link.a_node_name,
      link.a_node_mac,
      link.z_node_name,
      link.z_node_mac,
      channel ? std::to_string(channel.value()) : "None");

  folly::dynamic newNodesOverrides =
      forUserConfig ? nodesOverrides_ : autoNodesOverrides_;
  if (!link.z_node_mac.empty()) {
    setLinkChannelForRadio(
        newNodesOverrides, link.a_node_name, link.a_node_mac, channel);
  }

  if (!link.a_node_mac.empty()) {
    setLinkChannelForRadio(
        newNodesOverrides, link.z_node_name, link.z_node_mac, channel);
  }

  if (forUserConfig) {
    return setNewNodeOverrides(newNodesOverrides, errorMsg);
  } else {
    return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
  }
}

void
ConfigHelper::setLinkChannelForRadio(
    folly::dynamic& nodesOverrides,
    const std::string& nodeName,
    const std::string& radioMac,
    const std::optional<int8_t> channel) {
  folly::dynamic& fwParams = getDynamicObjectAtPath(
      nodesOverrides,
      {nodeName, "radioParamsOverride", getStandardizedMac(radioMac),
          "fwParams"});
  if (!channel) {
    fwParams.erase("channel");
    return;
  }
  fwParams["channel"] = channel.value();
}

bool
ConfigHelper::setLinkMaxMcs(
    const std::string& nodeName,
    const std::string& radioMac,
    const std::string& responderMac,
    const int laMaxMcs,
    std::string& errorMsg) {
  folly::dynamic newNodesOverrides = autoNodesOverrides_;

  // Set link override for the link
  folly::dynamic& fwParams = getDynamicObjectAtPath(
      newNodesOverrides,
      {nodeName,
       "linkParamsOverride",
       getStandardizedMac(responderMac),
       "fwParams"});
  fwParams["laMaxMcs"] = laMaxMcs;

  // Update laMaxMcs in radioParamsOverride to min value across
  // linkParamsOverride since radioParams are used at responder after
  // re-ignition
  folly::dynamic& linkParamsMap = getDynamicObjectAtPath(
      newNodesOverrides, {nodeName, "linkParamsOverride"});
  int minVal = laMaxMcs;
  for (auto& it : linkParamsMap.items()) {
    folly::dynamic& linkOverride =
        getDynamicObjectAtPath(it.second, {"fwParams"});
    auto linkVal = maybeReadInt(linkOverride, "laMaxMcs");
    if (linkVal && linkVal.value() < minVal) {
      minVal = linkVal.value();
    }
  }
  folly::dynamic& radioFwParams = getDynamicObjectAtPath(
      newNodesOverrides,
      {nodeName,
       "radioParamsOverride",
       getStandardizedMac(radioMac),
       "fwParams"});
  radioFwParams["laMaxMcs"] = minVal;

  return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
}

ConfigHelper::TopologyLinkParams
ConfigHelper::getLinkTopologyConfig(
    const std::string& nodeName,
    const std::string& responderMac) const {
  VLOG(5) << folly::format(
      "Getting link topology config for node `{}` and responderMac `{}`",
      nodeName,
      responderMac);

  TopologyLinkParams topologyLinkParams;
  folly::dynamic nodeOverrideConfigDynamic = getConfigOverridesForNode(
      nodeName, std::nullopt, std::nullopt, std::nullopt);

  // Get config params from linkParamsBase
  folly::dynamic fwParams = getDynamicObjectAtPath(
      nodeOverrideConfigDynamic, {"linkParamsBase", "fwParams"});
  auto txGolay = maybeReadInt(fwParams, "txGolayIdx");
  auto rxGolay = maybeReadInt(fwParams, "rxGolayIdx");
  auto controlSuperframe = maybeReadInt(fwParams, "controlSuperframe");

  // Get config params from linkParamsOverride
  fwParams = getDynamicObjectAtPath(
      nodeOverrideConfigDynamic,
      {"linkParamsOverride", getStandardizedMac(responderMac), "fwParams"});
  auto txGolayOverride = maybeReadInt(fwParams, "txGolayIdx");
  auto rxGolayOverride = maybeReadInt(fwParams, "rxGolayIdx");
  auto controlSuperframeOverride = maybeReadInt(fwParams, "controlSuperframe");

  // Overwrite params with the respective override if they exist
  if (txGolayOverride && rxGolayOverride) {
    txGolay = txGolayOverride;
    rxGolay = rxGolayOverride;
  }
  if (controlSuperframeOverride) {
    controlSuperframe = controlSuperframeOverride;
  }

  // Set params in the return struct if they exist
  if (txGolay && rxGolay) {
    thrift::GolayIdx golayIdx;
    golayIdx.txGolayIdx = txGolay.value();
    golayIdx.rxGolayIdx = rxGolay.value();
    topologyLinkParams.golayIdx = golayIdx;
  }
  if (controlSuperframe) {
    topologyLinkParams.controlSuperframe = controlSuperframe.value();
  }
  return topologyLinkParams;
}

std::optional<int64_t>
ConfigHelper::maybeReadInt(
      const folly::dynamic& obj, const std::string& key) const {
  auto iter = obj.find(key);
  if (iter == obj.items().end() || !iter->second.isInt()) {
    return std::nullopt;
  }
  return iter->second.getInt();
}

bool
ConfigHelper::updateAutoNodeTopologyConfigMac(
    const std::string& nodeName,
    const std::string& oldMac,
    const std::string& newMac,
    std::string& errorMsg) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  folly::dynamic& radioParamsMap = getDynamicObjectAtPath(
      newAutoNodesOverrides, {nodeName, "radioParamsOverride"});
  auto radioParamsIter = radioParamsMap.find(getStandardizedMac(oldMac));
  if (radioParamsIter == radioParamsMap.items().end()) {
    return true;
  }
  folly::dynamic radioParamsCopy = radioParamsIter->second;
  radioParamsMap[getStandardizedMac(newMac)] = radioParamsCopy;
  radioParamsMap.erase(radioParamsIter);
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

bool
ConfigHelper::updateAutoLinkTopologyConfigMac(
    const thrift::Link& link,
    const std::string& oldMac,
    const std::string& newMac,
    std::string& errorMsg) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;

  // ResponderMac is used for per-link overrides. So, the updated mac should be
  // reflected in the config for the node on the opposite end of the link.
  // (check for newMac because topology already changed)
  const std::string& nodeName = link.a_node_mac == newMac ?
      link.z_node_name : link.a_node_name;
  updateAutoLinkTopologyConfigMacforNode(
      newAutoNodesOverrides, nodeName, oldMac, newMac);
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

void
ConfigHelper::updateAutoLinkTopologyConfigMacforNode(
    folly::dynamic& autoNodeOverrides,
    const std::string& nodeName,
    const std::string& oldMac,
    const std::string& newMac) {
  folly::dynamic& linkParamsMap = getDynamicObjectAtPath(
      autoNodeOverrides, {nodeName, "linkParamsOverride"});
  auto iter = linkParamsMap.find(getStandardizedMac(oldMac));
  if (iter == linkParamsMap.items().end()) {
    return;
  }
  folly::dynamic linkParamsCopy = iter->second;
  linkParamsMap[getStandardizedMac(newMac)] = linkParamsCopy;
  linkParamsMap.erase(iter);
}

bool
ConfigHelper::deleteAllNodeOverrides(
    const std::string& nodeName, std::string& errorMsg) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  newAutoNodesOverrides.erase(nodeName);
  folly::dynamic newNodesOverrides = nodesOverrides_;
  newNodesOverrides.erase(nodeName);
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg) &&
      setNewNodeOverrides(newNodesOverrides, errorMsg);
}

bool
ConfigHelper::deleteAutoLinkOverrides(
    const thrift::Link& link, std::string& errorMsg) {
  folly::dynamic newAutoNodesOverrides = autoNodesOverrides_;
  if (!link.z_node_mac.empty()) {
    deleteAutoLinkOverridesForNode(
        newAutoNodesOverrides, link.a_node_name, link.z_node_mac);
  }

  if (!link.a_node_mac.empty()) {
    deleteAutoLinkOverridesForNode(
        newAutoNodesOverrides, link.z_node_name, link.a_node_mac);
  }
  return setNewAutoNodeOverrides(newAutoNodesOverrides, errorMsg);
}

void
ConfigHelper::deleteAutoLinkOverridesForNode(
    folly::dynamic& autoNodeOverrides,
    const std::string& nodeName,
    const std::string& responderMac) {
  folly::dynamic& linkParamsMap = getDynamicObjectAtPath(
      autoNodeOverrides, {nodeName, "linkParamsOverride"});
  linkParamsMap.erase(getStandardizedMac(responderMac));
}

folly::dynamic&
ConfigHelper::getDynamicObjectAtPath(
    folly::dynamic& obj, std::deque<std::string> path) const {
  if (path.empty()) {
    return obj;
  }

  // Extract and erase first element
  std::string key = path.front();
  path.pop_front();

  // Find next object
  folly::dynamic& newObj = obj.setDefault(key, folly::dynamic::object);
  return getDynamicObjectAtPath(newObj, path);
}

std::string
ConfigHelper::getStandardizedMac(const std::string& mac) const {
  if (!mac.empty()) {
    try {
      return MacUtils::standardizeMac(mac);
    } catch (const std::invalid_argument& ex) {
      LOG(ERROR) << folly::format(
          "Error standardizing mac {}: {}", mac, ex.what());
    }
  }
  return mac;
}

std::unordered_map<std::string, thrift::PolarityType>
ConfigHelper::getPolarities(
    const std::vector<thrift::Node>& nodes, bool userConfiguredOnly) const {
  std::unordered_map<std::string, thrift::PolarityType> polarities;

  for (const auto& node : nodes) {
    folly::dynamic nodeOverrides;
    if (userConfiguredOnly) {
      // Get user overrides for node
      folly::dynamic empty = folly::dynamic::object;
      nodeOverrides = getConfigOverridesForNode(
          node.name, std::make_optional(empty), std::nullopt, std::nullopt);
    } else {
      // Get auto+user overrides for node
      nodeOverrides = getConfigOverridesForNode(
          node.name, std::nullopt, std::nullopt, std::nullopt);
    }

    // Get base overrides
    folly::dynamic& fwParams =
        getDynamicObjectAtPath(nodeOverrides, {"radioParamsBase", "fwParams"});
    auto basePolarity = maybeReadInt(fwParams, "polarity");

    for (const auto& macAddr : node.wlan_mac_addrs) {
      if (basePolarity) {
        polarities[macAddr] =
            static_cast<thrift::PolarityType>(basePolarity.value());
      }

      // Get per radio override
      folly::dynamic& fwParams = getDynamicObjectAtPath(
          nodeOverrides, {"radioParamsOverride", macAddr, "fwParams"});
      auto overridePolarity = maybeReadInt(fwParams, "polarity");
      if (overridePolarity) {
        polarities[macAddr] =
            static_cast<thrift::PolarityType>(overridePolarity.value());
      }
    }
  }

  return polarities;
}

bool
ConfigHelper::isForceGpsDisable(const thrift::Node& node) const {
  folly::dynamic nodeOverrides = getConfigOverridesForNode(
      node.name, std::nullopt, std::nullopt, std::nullopt);

  // Get base overrides
  folly::dynamic& fwParamsBase =
      getDynamicObjectAtPath(nodeOverrides, {"radioParamsBase", "fwParams"});
  auto baseVal = maybeReadInt(fwParamsBase, "forceGpsDisable");
  if (baseVal && baseVal.value() > 0) {
    return true;
  }

  // Get per radio override
  for (const auto& macAddr : node.wlan_mac_addrs) {
    folly::dynamic& fwParamsOverride = getDynamicObjectAtPath(
        nodeOverrides, {"radioParamsOverride", macAddr, "fwParams"});
    auto overrideVal = maybeReadInt(fwParamsOverride, "forceGpsDisable");
    if (overrideVal && overrideVal.value() > 0) {
      return true;
    }
  }

  return false;
}

std::optional<thrift::PolarityType>
ConfigHelper::getRadioPolarity(
    const std::string& nodeName,
    const std::string& macAddr,
    bool userConfiguredOnly) const {
  std::optional<thrift::PolarityType> polarity = std::nullopt;

  folly::dynamic nodeOverrides;
  if (userConfiguredOnly) {
    // Get user overrides for node
    folly::dynamic empty = folly::dynamic::object;
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::make_optional(empty), std::nullopt, std::nullopt);
  } else {
    // Get auto+user overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::nullopt, std::nullopt, std::nullopt);

  }

  // Get base overrides
  folly::dynamic& fwParams =
      getDynamicObjectAtPath(nodeOverrides, {"radioParamsBase", "fwParams"});
  auto basePolarity = maybeReadInt(fwParams, "polarity");
  if (basePolarity) {
    polarity = static_cast<thrift::PolarityType>(basePolarity.value());
  }

  // Get per radio override
  fwParams = getDynamicObjectAtPath(
      nodeOverrides, {"radioParamsOverride", macAddr, "fwParams"});
  auto overridePolarity = maybeReadInt(fwParams, "polarity");
  if (overridePolarity) {
    polarity = static_cast<thrift::PolarityType>(overridePolarity.value());
  }

  if (!polarity || !PolarityHelper::isValidPolarityType(polarity.value())) {
    return std::nullopt;
  }

  return polarity;
}

std::optional<int8_t>
ConfigHelper::getRadioChannel(
    const std::string& nodeName,
    const std::string& macAddr,
    bool userConfiguredOnly,
    bool autoConfiguredOnly) const {
  std::optional<int8_t> radioChannel = std::nullopt;
  folly::dynamic nodeOverrides;
  folly::dynamic empty = folly::dynamic::object;
  if (userConfiguredOnly) {
    // Get user overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::make_optional(empty), std::nullopt, std::nullopt);
  } else if (autoConfiguredOnly) {
    // Get auto overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName,
        std::nullopt,
        std::make_optional(empty),
        std::make_optional(empty));
  } else {
    // Get auto+user overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::nullopt, std::nullopt, std::nullopt);
  }

  // Get base overrides
  folly::dynamic& fwParams =
      getDynamicObjectAtPath(nodeOverrides, {"radioParamsBase", "fwParams"});
  auto baseChannel = maybeReadInt(fwParams, "channel");
  if (baseChannel) {
    radioChannel = (int8_t)(baseChannel.value());
  }

  // Get per radio override
  fwParams = getDynamicObjectAtPath(
      nodeOverrides, {"radioParamsOverride", macAddr, "fwParams"});
  auto overrideChannel = maybeReadInt(fwParams, "channel");
  if (overrideChannel) {
    radioChannel = (int8_t)(overrideChannel.value());
  }

  if (!radioChannel || !ChannelHelper::isValidChannel(radioChannel.value())) {
    return std::nullopt;
  }
  return radioChannel;
}

std::optional<int8_t>
ConfigHelper::getLinkChannel (
    const thrift::Link& link,
    bool userConfiguredOnly,
    bool autoConfiguredOnly) const {
  if (!link.a_node_mac.empty() && !link.z_node_mac.empty()) {
    auto aRadioChannel = getRadioChannel(
        link.a_node_name,
        link.a_node_mac,
        userConfiguredOnly,
        autoConfiguredOnly);
    auto zRadioChannel = getRadioChannel(
        link.z_node_name,
        link.z_node_mac,
        userConfiguredOnly,
        autoConfiguredOnly);

    if (aRadioChannel == zRadioChannel && aRadioChannel) {
      return aRadioChannel;
    }
  }
  return std::nullopt;
}

std::optional<folly::dynamic>
ConfigHelper::getConfigPathWithWildcards(
    folly::dynamic& configObj, std::deque<std::string> configPathTokens) {
  if (configPathTokens.empty()) {
    return std::make_optional(configObj);
  }

  std::string key = configPathTokens.front();
  configPathTokens.pop_front();
  if (key == E2EConsts::kConfigPathWildcard) {
    // Traverse all paths at current level if a wildcard exists
    if (configPathTokens.empty()) {
      // If path ends with a wildcard, return full object
      return std::make_optional(configObj);
    } else if (configObj.isObject()) {
      folly::dynamic retObj = folly::dynamic::object;
      for (const auto& pair : configObj.items()) {
        // Recursively move down the config path
        std::optional<folly::dynamic> obj =
            getConfigPathWithWildcards(configObj[pair.first], configPathTokens);
        if (obj) {
          retObj[pair.first] = obj.value();
        }
      }

      if (!retObj.empty()) {
        return std::make_optional(retObj);
      }
    }
  } else {
    if (configObj.isObject()) {
      auto iter = configObj.find(key);
      if (iter != configObj.items().end()) {
        // Recursively move down the config path
        std::optional<folly::dynamic> obj =
            getConfigPathWithWildcards(configObj[key], configPathTokens);
        if (obj) {
          folly::dynamic retObj = folly::dynamic::object;
          retObj[key] = obj.value();
          return std::make_optional(retObj);
        }
      }
    }
  }

  return std::nullopt;
}

std::optional<folly::dynamic>
ConfigHelper::getNodeConfigPaths(
    const std::string& nodeName, const std::vector<std::string>& configPaths) {
  // Get config state of node. Construct one if missing
  auto configState = getConfigState(nodeName);
  if (!configState) {
    configState = initConfigState(nodeName, "", "", "");
    if (!configState) {
      LOG(INFO) << "configState error: " << nodeName;
      return std::nullopt;
    }
  }

  std::optional<folly::dynamic> nodeConfigObj;
  for (const auto& path : configPaths) {
    // tokenize config path
    std::deque<std::string> configPathTokens;
    folly::splitTo<std::string>(
        E2EConsts::kConfigPathDelimiter,
        path,
        std::inserter(configPathTokens, configPathTokens.begin()));

    std::optional<folly::dynamic> config =
        getConfigPathWithWildcards(configState->configObj, configPathTokens);
    if (config) {
      if (nodeConfigObj == std::nullopt) {
        nodeConfigObj = config;
      } else {
        // Merge all requested config paths
        JsonUtils::dynamicObjectMerge(nodeConfigObj.value(), config.value());
      }
    }
  }

  return nodeConfigObj;
}

void
ConfigHelper::onAddLink(
    const TopologyWrapper& topologyW, const thrift::Link& newLink) {
  if (!newLink.a_node_mac.empty() && !newLink.z_node_mac.empty()) {
    PolarityHelper::assignLinkPolarity(topologyW, *this, newLink);
    GolayHelper::assignLinkGolay(topologyW, *this, newLink);
    ChannelHelper::assignLinkChannel(topologyW, *this, newLink);
    ControlSuperframeHelper::assignLinkControlSuperframe(
        topologyW, *this, newLink);
  }
}

void
ConfigHelper::onChangeRadioMac(
    const TopologyWrapper& topologyW,
    const std::string& nodeName,
    const std::string& oldMacAddr,
    const std::string& newMacAddr) {
  // Changing mac to an empty one is not supported
  if (newMacAddr.empty()) {
    return;
  }

  std::string oldMac = getStandardizedMac(oldMacAddr);
  std::string newMac = getStandardizedMac(newMacAddr);

  std::unordered_set<std::string> affectedNodes;
  affectedNodes.insert(nodeName);

  // Find all affected links
  for (auto const& link : topologyW.getAllLinks()) {
    // Topology already updated, so check for newMac
    if (link.a_node_mac == newMac || link.z_node_mac == newMac) {
      if (oldMac.empty()) {
        // Assign config for the first time
        PolarityHelper::assignLinkPolarity(topologyW, *this, link);
        GolayHelper::assignLinkGolay(topologyW, *this, link);
        ControlSuperframeHelper::assignLinkControlSuperframe(
            topologyW, *this, link);
      } else {
        // Update existing link config
        std::string errorMsg;
        updateAutoLinkTopologyConfigMac(link, oldMac, newMac, errorMsg);
      }
      affectedNodes.insert(link.a_node_name);
      affectedNodes.insert(link.z_node_name);
    }
  }

  // Update existing node config
  if (!oldMac.empty()) {
    std::string errorMsg;
    updateAutoNodeTopologyConfigMac(nodeName, oldMac, newMac, errorMsg);
  }

  // Update node info for all affected nodes
  std::vector<std::string> nodes(affectedNodes.begin(), affectedNodes.end());
  std::string errorMsg;
  setNodeTopologyInfo(nodes, topologyW, errorMsg);
}

thrift::GolayIdx
ConfigHelper::getGolayIdx(
    folly::dynamic& nodeOverrides, const std::string& responderMac) {
  thrift::GolayIdx nodeGolayIdx;

  // Get base golay for all links
  folly::dynamic& fwParams =
      getDynamicObjectAtPath(nodeOverrides, {"linkParamsBase", "fwParams"});
  auto baseRxGolayIdx = maybeReadInt(fwParams, "rxGolayIdx");
  auto baseTxGolayIdx = maybeReadInt(fwParams, "txGolayIdx");

  if (baseRxGolayIdx && baseTxGolayIdx) {
    nodeGolayIdx.rxGolayIdx = baseRxGolayIdx.value();
    nodeGolayIdx.txGolayIdx = baseTxGolayIdx.value();
  }

  // Get perLink overrides for responder MAC address
  fwParams = getDynamicObjectAtPath(
      nodeOverrides, {"linkParamsOverride", responderMac, "fwParams"});
  auto overrideRxGolay = maybeReadInt(fwParams, "rxGolayIdx");
  auto overrideTxGolay = maybeReadInt(fwParams, "txGolayIdx");
  if (overrideRxGolay && overrideTxGolay) {
    nodeGolayIdx.rxGolayIdx = overrideRxGolay.value();
    nodeGolayIdx.txGolayIdx = overrideTxGolay.value();
  }

  return nodeGolayIdx;
}

std::optional<thrift::GolayIdx>
ConfigHelper::getLinkGolayIdx(
    const thrift::Link& link, bool userConfiguredOnly) {
  if (!link.a_node_mac.empty() && !link.z_node_mac.empty()) {
    auto aNodeGolayIdx =
        getLinkGolayIdx(link.a_node_name, link.z_node_mac, userConfiguredOnly);
    auto zNodeGolayIdx =
        getLinkGolayIdx(link.z_node_name, link.a_node_mac, userConfiguredOnly);
    if (aNodeGolayIdx == zNodeGolayIdx && aNodeGolayIdx != thrift::GolayIdx()) {
      return aNodeGolayIdx;
    }
  }

  return std::nullopt;
}

std::optional<thrift::GolayIdx>
ConfigHelper::getLinkGolayIdx(
    const std::string& nodeName,
    const std::string& responderMac,
    bool userConfiguredOnly) {
  folly::dynamic nodeOverrides;
  if (userConfiguredOnly) {
    // Get user overrides for node
    folly::dynamic cfg = folly::dynamic::object;
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::make_optional(cfg), std::nullopt, std::nullopt);
  } else {
    // Get auto+user overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::nullopt, std::nullopt, std::nullopt);
  }

  auto nodeGolayIdx = getGolayIdx(nodeOverrides, responderMac);
  if (nodeGolayIdx != thrift::GolayIdx()) {
    return nodeGolayIdx;
  }

  return std::nullopt;
}

std::optional<int64_t>
ConfigHelper::getLinkControlSuperframe(
    const std::string& nodeName,
    const std::string& responderMac,
    bool userConfiguredOnly) const {
  folly::dynamic nodeOverrides;
  if (userConfiguredOnly) {
    // Get user overrides for node
    folly::dynamic cfg = folly::dynamic::object;
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::make_optional(cfg), std::nullopt, std::nullopt);
  } else {
    // Get auto+user overrides for node
    nodeOverrides = getConfigOverridesForNode(
        nodeName, std::nullopt, std::nullopt, std::nullopt);
  }

  // Get base superframe for all links
  folly::dynamic& fwParams =
      getDynamicObjectAtPath(nodeOverrides, {"linkParamsBase", "fwParams"});
  auto controlSuperframe = maybeReadInt(fwParams, "controlSuperframe");

  // Get perLink overrides for responder MAC address
  fwParams = getDynamicObjectAtPath(
      nodeOverrides, {"linkParamsOverride", responderMac, "fwParams"});
  auto controlSuperframeOverride = maybeReadInt(fwParams, "controlSuperframe");

  if (controlSuperframeOverride) {
    controlSuperframe = controlSuperframeOverride;
  }

  return controlSuperframe;
}

std::set<int8_t>
ConfigHelper::getEnabledChannels() const {
  std::set<int8_t> enabledChannels;
  std::string enabledChannelsConfig = SharedObjects::getE2EConfigWrapper()
                                        ->rlock()
                                        ->getConfig()
                                        ->topologyParams.enabledChannels;
  if (enabledChannelsConfig.empty()) {
    // return only default channel
    return {kDefaultChannel};
  }

  std::vector<std::string> splitChannels;
  folly::split(",", enabledChannelsConfig, splitChannels);
  for (size_t i = 0; i < splitChannels.size(); i++) {
    auto channel = folly::tryTo<int>(splitChannels[i]);
    if (channel.hasValue() && ChannelHelper::isValidChannel(channel.value())) {
      enabledChannels.insert(channel.value());
    } else {
      LOG(WARNING) << "Invalid element in enabledChannels: "
                   << splitChannels[i];
    }
  }

  if (enabledChannels.empty()) {
    LOG(INFO) << "No valid channels found in enabledChannels config. Treating"
              << " only default channel "
              << std::to_string(kDefaultChannel) << " as enabled.";
    enabledChannels.insert(kDefaultChannel);
  }

  return enabledChannels;
}

std::set<int8_t>
ConfigHelper::getUsedChannels(const TopologyWrapper& topologyW) const {
  std::set<int8_t> usedChannels;
  LinkGroupHelper::GroupNameToLinkNames group2Links =
    LinkGroupHelper::getLinkGroups(topologyW);
  for (const auto& groupIt : group2Links) {
    const auto& linkName = *(groupIt.second.begin());
    auto link = topologyW.getLink(linkName);
    if (!link) {
      continue;
    }
    // Get auto configured channel
    auto channelIdx = getLinkChannel(link.value(), false, true);

    if (channelIdx) {
      usedChannels.insert(channelIdx.value());
    }
  }

  return usedChannels;
}

bool
ConfigHelper::clearAutoNodeOverrides(
    std::vector<std::string> nodeNames,
    std::vector<std::string> configPaths,
    std::string& errorMsg) {
  folly::dynamic newNodesOverrides = autoNodesOverrides_;
  for (const std::string& configPath : configPaths) {
    // tokenize config path
    std::deque<std::string> configPathTokens;
    folly::splitTo<std::string>(
        E2EConsts::kConfigPathDelimiter,
        configPath,
        std::inserter(configPathTokens, configPathTokens.begin()));

    configPathTokens.push_front("");
    for (const std::string& nodeName : nodeNames) {
      // Add nodeName as first key in path
      configPathTokens[0] = nodeName;
      std::optional<folly::dynamic> configToBeCleared =
          getConfigPathWithWildcards(newNodesOverrides, configPathTokens);

      if (configToBeCleared) {
        // Remove configToBeCleared from newNodeOverrides
        newNodesOverrides = JsonUtils::dynamicObjectDifference(
            configToBeCleared.value(), newNodesOverrides);
      }
    }
  }
  newNodesOverrides = JsonUtils::dynamicObjectClean(newNodesOverrides);
  return setNewAutoNodeOverrides(newNodesOverrides, errorMsg);
}

bool
ConfigHelper::validateLinkConfig(
    const TopologyWrapper& topologyW,
    const thrift::Link& link,
    EventClient& eventClient) {

  // Skip links with empty MAC addresses
  // Responder MAC address is used to specify link configuration
  if (link.a_node_mac.empty() || link.z_node_mac.empty()) {
    return false;
  }

  // Validate polarity
  auto aPolarityOld =
      getRadioPolarity(link.a_node_name, link.a_node_mac, false);
  auto zPolarityOld =
      getRadioPolarity(link.z_node_name, link.z_node_mac, false);
  if (!PolarityHelper::isValidLinkPolarity(aPolarityOld, zPolarityOld)) {
    // Invalid polarity assignment. Attempt to repair.
    if (!PolarityHelper::assignLinkPolarity(topologyW, *this, link)) {
      // Unable to repair polarity allocation
      auto polarityTypeMap =
          TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap();
      auto errMsg = folly::sformat(
          "Invalid polarities assigned across `{}`. Current polarities are "
          "{}/{}. Attempt to repair allocation failed.",
          link.name,
          folly::get_default(
              polarityTypeMap,
              aPolarityOld.value(),
              "UNKNOWN"),
          folly::get_default(
              polarityTypeMap,
              zPolarityOld.value(),
              "UNKNOWN"));
      LOG(ERROR) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_POLARITY_INFO,
          thrift::EventLevel::ERROR,
          errMsg,
          "",
          std::make_optional(link.name));
      return false;
    } else {
      auto aPolarityNew = getRadioPolarity(
          link.a_node_name, link.a_node_mac, false);
      auto zPolarityNew = getRadioPolarity(
          link.z_node_name, link.z_node_mac, false);
      auto errMsg = folly::sformat(
          "Changing polarity assignment across `{}` to {}/{}",
          link.name,
          folly::get_default(
              TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap(),
              aPolarityNew.value(),
              "UNKNOWN"),
          folly::get_default(
              TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap(),
              zPolarityNew.value(),
              "UNKNOWN"));
      LOG(WARNING) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_POLARITY_INFO,
          thrift::EventLevel::WARNING,
          errMsg,
          "",
          std::make_optional(link.name));
    }
  }

  // Validate channel assignment
  auto aChannelOld =
      getRadioChannel(link.a_node_name, link.a_node_mac, false, false);
  auto zChannelOld =
      getRadioChannel(link.z_node_name, link.z_node_mac, false, false);
  if (!ChannelHelper::isValidLinkChannel(aChannelOld, zChannelOld)) {
    // Invalid channel assignment. Attempt to repair.
    if (!ChannelHelper::assignLinkChannel(topologyW, *this, link)) {
      // Unable to repair channel assignment
      eventClient.logEventDynamic(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_CHANNEL_INFO,
          thrift::EventLevel::ERROR,
          "Channel assignment failed",
          folly::dynamic::object("name", link.name)
              ("old_channelA",
                aChannelOld ? std::to_string(aChannelOld.value()) : "None")
              ("old_channelZ",
                zChannelOld ? std::to_string(zChannelOld.value()) : "None"),
          std::make_optional(link.name));
      return false;
    } else {
      auto aChannelNew = getRadioChannel(
          link.a_node_name, link.a_node_mac, false, false);
      auto zChannelNew = getRadioChannel(
          link.z_node_name, link.z_node_mac, false, false);
      eventClient.logEventDynamic(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_CHANNEL_INFO,
          thrift::EventLevel::WARNING,
          "Changing channel assignment",
          folly::dynamic::object("name", link.name)
              ("channelA",
                aChannelNew ? std::to_string(aChannelNew.value()) : "None")
              ("channelZ",
                zChannelNew ? std::to_string(zChannelNew.value()) : "None"),
          std::make_optional(link.name));
    }
  }

  // Validate Golay
  auto aGolayIdx = getLinkGolayIdx(link.a_node_name, link.z_node_mac, false);
  auto zGolayIdx = getLinkGolayIdx(link.z_node_name, link.a_node_mac, false);
  if (!GolayHelper::isValidLinkGolay(aGolayIdx, zGolayIdx)) {
    // Invalid golay assignment. Attempt to repair.
    if (!GolayHelper::assignLinkGolay(topologyW, *this, link)) {
      // Unable to repair golay allocation
      auto errMsg = folly::sformat(
          "Invalid golayIdx assigned across `{}`. Attempt to repair assignment "
          "failed.",
          link.name);
      LOG(ERROR) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_GOLAY_INFO,
          thrift::EventLevel::ERROR,
          errMsg,
          "",
          std::make_optional(link.name));
      return false;
    } else {
      auto aGolayIdxNew =
          getLinkGolayIdx(link.a_node_name, link.z_node_mac, false);
      auto errMsg = folly::sformat(
          "Changing golayIdx assigned across `{}` to {}/{}",
          link.name,
          aGolayIdxNew->txGolayIdx,
          aGolayIdxNew->rxGolayIdx);
      LOG(WARNING) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_GOLAY_INFO,
          thrift::EventLevel::WARNING,
          errMsg,
          "",
          std::make_optional(link.name));
    }
  }

  // Validate control superframe
  auto aSframe =
      getLinkControlSuperframe(link.a_node_name, link.z_node_mac, false);
  auto zSframe =
      getLinkControlSuperframe(link.z_node_name, link.a_node_mac, false);
  if (!ControlSuperframeHelper::isValidLinkControlSuperframe(
          topologyW, *this, link, aSframe, zSframe)) {
    // Invalid control superframe assignment. Attempt to repair.
    if (!ControlSuperframeHelper::assignLinkControlSuperframe(
            topologyW, *this, link)) {
      auto errMsg = folly::sformat(
          "Invalid control superframe assigned across `{}`. Attempt to repair "
          "assignment "
          "failed.",
          link.name);
      LOG(ERROR) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_CONTROL_SUPERFRAME_INFO,
          thrift::EventLevel::ERROR,
          errMsg,
          "",
          std::make_optional(link.name));
      return false;
    } else {
      auto aSframeNew =
          getLinkControlSuperframe(link.a_node_name, link.z_node_mac, false);
      auto errMsg = folly::sformat(
          "Changing control superframe assigned across `{}` to {}",
          link.name,
          aSframeNew.value());
      LOG(WARNING) << errMsg;
      eventClient.logEvent(
          thrift::EventCategory::CONFIG,
          thrift::EventId::CONFIG_CONTROL_SUPERFRAME_INFO,
          thrift::EventLevel::WARNING,
          errMsg,
          "",
          std::make_optional(link.name));
    }
  }

  return true;
}

bool
ConfigHelper::addBaseConfig(
    const std::string& ver, const std::string& configJson) {
  // Convert JSON to dynamic object
  folly::dynamic obj = folly::dynamic::object;
  try {
    obj = folly::parseJson(configJson);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse base config JSON: "
               << folly::exceptionStr(ex);
    return false;
  }

  // Look up existing entries
  if (baseConfigObjects_.count(ver)) {
    LOG(WARNING) << "Not adding base config for version='" << ver
                 << "' (found existing entry)";
    return false;
  }

  LOG(INFO) << "Adding new base config for version='" << ver << "'";
  baseConfigObjects_[ver] = obj;
  return true;
}

bool
ConfigHelper::addHardwareBaseConfig(
    const std::string& hwType,
    const std::string& ver,
    const std::string& configJson) {
  // Convert JSON to dynamic object
  folly::dynamic obj = folly::dynamic::object;
  try {
    obj = folly::parseJson(configJson);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse hardware config JSON: "
               << folly::exceptionStr(ex);
    return false;
  }

  // Look up existing entries
  auto hwConfigIter = hwConfigObjects_.find(hwType);
  if (hwConfigIter == hwConfigObjects_.items().end()) {
    hwConfigObjects_[hwType] = folly::dynamic::object;
  } else {
    if (hwConfigIter->second.count(ver)) {
      LOG(WARNING) << "Not adding hardware base config for type='" << hwType
                   << "', version='" << ver << "' (found existing entry)";
      return false;
    }
  }

  LOG(INFO) << "Adding new hardware base config for type='" << hwType
            << "', version='" << ver << "'";
  hwConfigObjects_[hwType][ver] = obj;
  return true;
}

bool
ConfigHelper::addHardwareTypeMapping(
    const std::string& hwBoardId, const std::string& hwType) {
  // Look up existing entries
  if (getHardwareTypeMapping(hwBoardId)) {
    LOG(WARNING) << "Not adding hardware type mapping for type='" << hwType
                  << "', hwBoardId='" << hwBoardId
                  << "' (found existing entry)";
    return false;
  }

  LOG(INFO) << "Adding new hardware type mapping from type='" << hwType
            << "' to hwBoardId='" << hwBoardId << "'";
  hardwareConfigTypeMap_[hwBoardId] = hwType;
  return true;
}

std::optional<std::string>
ConfigHelper::getHardwareTypeMapping(const std::string& hwBoardId) const {
  auto iter = hardwareConfigTypeMap_.find(hwBoardId);
  if (iter == hardwareConfigTypeMap_.end()) {
    return std::nullopt;
  } else {
    return iter->second;
  }
}

bool
ConfigHelper::mergeConfigMetadata(
    const std::string& configMeta, bool validateCfgAction) {
  // Convert JSON to dynamic object
  if (configMeta.empty()) {
    LOG(WARNING) << "Cannot merge config metadata with an empty string";
    return false;
  }
  folly::dynamic newMetadata = folly::dynamic::object;
  try {
    newMetadata = folly::parseJson(configMeta);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse config metadata: "
               << folly::exceptionStr(ex);
    return false;
  }
  if (newMetadata.empty()) {
    LOG(WARNING) << "Cannot merge config metadata with an empty object";
    return false;
  }

  // Create new metadata object
  folly::dynamic existingMetadata = nodeConfigMeta_->get();
  JsonUtils::dynamicObjectMergeAppend(existingMetadata, newMetadata);
  try {
    auto mergedMetadata = std::make_unique<ConfigMetadata>(
        existingMetadata,
        true /* keepFullMetadata */,
        false /* hasFlags */,
        validateCfgAction);

    nodeConfigMeta_.swap(mergedMetadata);
    mergedMetadata.reset();
    return true;
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Failed to parse merged config metadata: "
               << folly::exceptionStr(ex);
    return false;
  }
}

} // namespace terragraph
} // namespace facebook
