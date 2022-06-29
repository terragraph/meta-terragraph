/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "NodeConfigWrapper.h"

#include <errno.h>
#include <stdio.h>
#include <sys/file.h>
#include <unistd.h>

#include <folly/ScopeGuard.h>
#include <folly/String.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "WatchdogUtils.h"
#include "JsonUtils.h"
#include "Md5Utils.h"

namespace facebook {
namespace terragraph {

namespace {
  const std::string kConfigLockFile{"/tmp/configlockfile"};
}

NodeConfigWrapper::NodeConfigWrapper() {
  nodeConfig_ = std::make_shared<thrift::NodeConfig>();
  initializePointers();
}

NodeConfigWrapper::NodeConfigWrapper(const std::string& nodeConfigFile)
    : nodeConfigFile_(nodeConfigFile) {
  nodeConfig_ = std::make_shared<thrift::NodeConfig>();
  readNodeConfigFile();
}

void
NodeConfigWrapper::setNodeConfigFile(const std::string& nodeConfigFile) {
  nodeConfigFile_ = nodeConfigFile;
  readNodeConfigFile();
}

void
NodeConfigWrapper::readNodeConfigFile() {
  // Read config file
  try {
    nodeConfigJson_ = JsonUtils::readJsonFile2String(nodeConfigFile_);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Could not read config file " << nodeConfigFile_ << " "
               << folly::exceptionStr(ex);
  }

  // Compute MD5
  computeConfigMd5();

  try {
    nodeConfigDynamic_ = folly::parseJson(nodeConfigJson_);
  } catch (const std::exception& ex) {
    LOG(ERROR)
        << "Could not parse config into dynamic object"
        << folly::exceptionStr(ex);
  }

  // Deserialize config JSON
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  *nodeConfig_ = thrift::NodeConfig();
  try {
    jsonSerializer.deserialize(folly::toJson(nodeConfigDynamic_), *nodeConfig_);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse config struct "
               << folly::exceptionStr(ex);
  }

  // Create shared pointers
  initializePointers();
}

bool
NodeConfigWrapper::setNodeConfig(const std::string& configJson) {
  // Try to deserialize config JSON, reject on failure
  thrift::NodeConfig config;
  apache::thrift::SimpleJSONSerializer jsonSerializer;
  try {
    jsonSerializer.deserialize(configJson, config);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "setNodeConfig: Could not deserialize json string "
               << folly::exceptionStr(ex);
    return false;
  }

  // Update node config reliably/atomically.
  //
  // Idea: 1. write new config to temp file, 2. sync 3. flock 4. rename
  // See: http://www.linux-mtd.infradead.org/faq/ubifs.html#L_atomic_change
  //
  // Note: The flock is not necessary for low level atomicity of file updates.
  //       Here, the flock ensures that other writers always see the new
  //       content before overwriting it.

  // 1. write new config to temp file
  const std::string tmpConfigFile = nodeConfigFile_ + ".tmpncw";
  try {
    JsonUtils::writeString2JsonFile(configJson, tmpConfigFile);
  } catch (const std::invalid_argument& ex) {
    LOG(ERROR) << "Can't write " << tmpConfigFile << " " <<
      folly::exceptionStr(ex);
    return false;
  }

  // 2. sync
  ::sync(); // always succeeds

  // 3. flock
  const int fd = open(
      kConfigLockFile.c_str(), O_CREAT|O_RDWR, S_IRUSR|S_IWUSR|S_IRGRP|S_IROTH);
  if (fd == -1) {
    LOG(ERROR) << "Can't open " << kConfigLockFile << ": "
               << folly::errnoStr(errno);
    return false;
  }
  SCOPE_EXIT {
    if (close(fd) != 0) {
      LOG(ERROR) << "Can't close " << kConfigLockFile << ": "
                 << folly::errnoStr(errno);
    }
  };
  if (flock(fd, LOCK_EX) != 0) {
    LOG(ERROR) << "Can't flock " << kConfigLockFile << ": "
               << folly::errnoStr(errno);
    return false;
  }
  SCOPE_EXIT {
    // Do explicit unlock for good logs and safety if close were to fail.
    if (flock(fd, LOCK_UN) != 0) {
      LOG(ERROR) << "Can't unlock " << kConfigLockFile << ": "
                 << folly::errnoStr(errno);
    }
  };

  // Designate the current, active config as the fallback config.
  bool wdogSetConfigFallbackSuccess =
      WatchdogUtils::setConfigFallbackFile(nodeConfigFile_);
  if (!wdogSetConfigFallbackSuccess) {
    LOG(ERROR) << "Failed to set config fallback file for watchdog";
  }

  // 4. rename
  if (::rename(tmpConfigFile.c_str(), nodeConfigFile_.c_str()) != 0) {
    LOG(ERROR) << "Can't finalize " << nodeConfigFile_ << ": "
               << folly::errnoStr(errno);
    return false;
  }

  // Re-initialize state
  readNodeConfigFile();

  // Start config fallback monitoring
  if (wdogSetConfigFallbackSuccess) {
    if (!WatchdogUtils::startConfigFallbackMonitor()) {
      LOG(ERROR) << "Failed to start config fallback monitor for watchdog";
    }
  }

  return true;
}

void
NodeConfigWrapper::computeConfigMd5() {
  prevConfigMd5_ = configMd5_;
  try {
    auto prettyJson = JsonUtils::toSortedPrettyJson(nodeConfigJson_);
    configMd5_ = Md5Utils::computeMd5(prettyJson);
  } catch (const std::exception& ex) {
    LOG(ERROR) << "computeConfigMd5: Could not parse json "
               << folly::exceptionStr(ex);
    configMd5_ = "";
  }
}

void
NodeConfigWrapper::initializePointers() {
  linkParamsBase_ = std::make_shared<thrift::LinkParams>(
      nodeConfig_->linkParamsBase);
  radioParamsBase_ = std::make_shared<thrift::RadioParams>(
      nodeConfig_->radioParamsBase);
  if (!nodeConfig_->kvstoreParams_ref()) {
    nodeConfig_->kvstoreParams_ref() = std::map<std::string, std::string>();
  }
  if (!nodeConfig_->openrParams_ref()) {
    nodeConfig_->openrParams_ref() = thrift::OpenrParams();
  }
  envParams_ = std::make_shared<thrift::NodeEnvParams>(nodeConfig_->envParams);
  statsAgentParams_ =
      std::make_shared<thrift::StatsAgentParams>(nodeConfig_->statsAgentParams);
  logTailParams_ =
      std::make_shared<thrift::LogTailParams>(nodeConfig_->logTailParams);
  kvstoreParams_ = std::make_shared<std::map<std::string, std::string>>(
      nodeConfig_->kvstoreParams_ref().value());
  openrParams_ =
      std::make_shared<thrift::OpenrParams>(nodeConfig_->
          openrParams_ref().value());
  dhcpParams_ = std::make_shared<thrift::DhcpParams>(
      nodeConfig_->dhcpParams);
  sysParams_ = std::make_shared<thrift::SystemParams>(nodeConfig_->sysParams);
  topologyInfo_ =
      std::make_shared<thrift::TopologyInfo>(nodeConfig_->topologyInfo);
  bgpParams_ = std::make_shared<thrift::BgpParams>(nodeConfig_->bgpParams);
  eapolParams_ =
      std::make_shared<thrift::EAPoLParams>(nodeConfig_->eapolParams);

}

std::string
NodeConfigWrapper::getNodeConfigJson() const {
  return nodeConfigJson_;
}

thrift::LinkParams
NodeConfigWrapper::getLinkParams(std::string responderMac) const {
  // Check if an override exists for responderMac
  try {
    auto linkParamsOverrideIt = nodeConfigDynamic_.find("linkParamsOverride");
    if (linkParamsOverrideIt != nodeConfigDynamic_.items().end()) {
      auto overrideIt = linkParamsOverrideIt->second.find(responderMac);
      if (overrideIt != linkParamsOverrideIt->second.items().end()) {
        auto linkParams = nodeConfigDynamic_["linkParamsBase"];

        // Merge linkParamsBase with per-link override
        JsonUtils::dynamicObjectMerge(linkParams, overrideIt->second);

        // Deserialize and return merged linkParams
        apache::thrift::SimpleJSONSerializer jsonSerializer;
        return jsonSerializer.deserialize<thrift::LinkParams>(
            folly::toJson(linkParams));
      }
    }
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse link specific LinkParams struct for "
               << responderMac << " : "
               << folly::exceptionStr(ex);
  }
  // return base config if no override exists
  return *linkParamsBase_;
}

thrift::RadioParams
NodeConfigWrapper::getRadioParams(std::string radioMac) const {
  // Check if an override exists for radioMac
  try {
    auto radioParamsOverrideIt = nodeConfigDynamic_.find("radioParamsOverride");
    if (radioParamsOverrideIt != nodeConfigDynamic_.items().end()) {
      auto overrideIt = radioParamsOverrideIt->second.find(radioMac);
      if (overrideIt != radioParamsOverrideIt->second.items().end()) {
        auto radioParams = nodeConfigDynamic_["radioParamsBase"];

        // Merge radioParamsBase with per-radio override
        JsonUtils::dynamicObjectMerge(radioParams, overrideIt->second);

        // Deserialize and return merged linkParams
        apache::thrift::SimpleJSONSerializer jsonSerializer;
        return jsonSerializer.deserialize<thrift::RadioParams>(
            folly::toJson(radioParams));
      }
    }
  } catch (const std::exception& ex) {
    LOG(ERROR) << "Could not parse radio specific RadioParams struct for "
               << radioMac << " : "
               << folly::exceptionStr(ex);
  }
  // return base config if no override exists
  return *radioParamsBase_;
}

thrift::NodeAirtime
NodeConfigWrapper::getNodeAirtime() const {
  thrift::NodeAirtime nodeAirtime;
  for (const auto& kv : nodeConfig_->linkParamsOverride) {
    if (!kv.second.airtimeConfig_ref().has_value()) {
      continue;  // no airtime defined
    }
    thrift::LinkAirtime linkAirtime;
    linkAirtime.macAddress = kv.first;
    linkAirtime.txIdeal = kv.second.airtimeConfig_ref()->txIdeal;
    linkAirtime.txMin = kv.second.airtimeConfig_ref()->txMin;
    linkAirtime.txMax = kv.second.airtimeConfig_ref()->txMax;
    linkAirtime.rxIdeal = kv.second.airtimeConfig_ref()->rxIdeal;
    linkAirtime.rxMin = kv.second.airtimeConfig_ref()->rxMin;
    linkAirtime.rxMax = kv.second.airtimeConfig_ref()->rxMax;
    nodeAirtime.linkAirtimes.push_back(linkAirtime);
  }
  return nodeAirtime;
}

void
NodeConfigWrapper::usePreviousConfigMd5(bool hasDelayedNodeAction) {
  hasDelayedNodeAction_ = hasDelayedNodeAction;
}

std::string
NodeConfigWrapper::getConfigMd5() const {
  return hasDelayedNodeAction_ && !prevConfigMd5_.empty() ? prevConfigMd5_
                                                          : configMd5_;
}

std::shared_ptr<const thrift::NodeConfig>
NodeConfigWrapper::getNodeConfig() const {
  return nodeConfig_;
}

std::shared_ptr<const thrift::NodeEnvParams>
NodeConfigWrapper::getEnvConfig() const {
  return envParams_;
}

std::shared_ptr<const thrift::StatsAgentParams>
NodeConfigWrapper::getStatsAgentParams() const {
  return statsAgentParams_;
}

std::shared_ptr<const thrift::LogTailParams>
NodeConfigWrapper::getLogTailParams() const {
  return logTailParams_;
}

std::shared_ptr<const std::map<std::string, std::string>>
NodeConfigWrapper::getKvstoreParams() const {
  return kvstoreParams_;
}

std::shared_ptr<const thrift::OpenrParams>
NodeConfigWrapper::getOpenrParams() const {
  return openrParams_;
}

std::shared_ptr<const thrift::DhcpParams>
NodeConfigWrapper::getDhcpParams() const {
  return dhcpParams_;
}

std::shared_ptr<const thrift::SystemParams>
NodeConfigWrapper::getSysParams() const {
  return sysParams_;
}

std::shared_ptr<const thrift::TopologyInfo>
NodeConfigWrapper::getTopologyInfo() const {
  return topologyInfo_;
}

std::shared_ptr<const thrift::BgpParams>
NodeConfigWrapper::getBgpParams() const {
  return bgpParams_;
}

std::shared_ptr<const thrift::EAPoLParams>
NodeConfigWrapper::getEAPoLParams() const {
  return eapolParams_;
}

} // namespace terragraph
} // namespace facebook
