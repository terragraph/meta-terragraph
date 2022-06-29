/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/dynamic.h>

#include "e2e/if/gen-cpp2/BWAllocation_types.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"

namespace facebook {
namespace terragraph {

/**
 * Wrapper class for accessing a Terragraph node configuration file.
 */
class NodeConfigWrapper {
 public:
  /**
   * Empty constructor. The caller is expected to invoke setNodeConfigFile()
   * before using any other methods.
   */
  NodeConfigWrapper();

  /** Construct NodeConfigWrapper by reading the given config file. */
  explicit NodeConfigWrapper(const std::string& nodeConfigFile);

  /** Set the node config file to read from and write to. */
  void setNodeConfigFile(const std::string& nodeConfigFile);

  /**
   * Initialize (or re-initialize) all internal state by reading the original
   * config file.
   */
  void readNodeConfigFile();

  /**
   * Set the node config to the given JSON string and writes it to the original
   * config file.
   *
   * Returns true if successful.
   */
  bool setNodeConfig(const std::string& configJson);

  /** Returns the config file contents (as a JSON string). */
  std::string getNodeConfigJson() const;

  /**
   * Returns the MD5 hash of the config JSON, or an empty string upon an error.
   *
   * If a delayed action was scheduled, returns the MD5 hash of the config JSON
   * prior to writing the new node config. We want to ignore config confirmation
   * success on status reports that are sent prior to restarting the staging
   * algorithm.
   */
  std::string getConfigMd5() const;

  /** Set true if a delayed node action is scheduled after a config change. */
  void usePreviousConfigMd5(bool hasDelayedNodeAction);

  /**
   * Returns link parameters for the given responder.
   *
   * This combines per-link overrides (if present for responderMac) with the
   * base parameters.
   */
  thrift::LinkParams getLinkParams(std::string responderMac) const;

  /**
   * Returns radio parameters for the given radio.
   *
   * This combines per-radio overrides (if present for radioMac) with the base
   * parameters.
   */
  thrift::RadioParams getRadioParams(std::string radioMac = "") const;

  /**
   * Construct and return the NodeAirtime config based on values in
   * `linkParamsOverride.{mac}.airtimeConfig`.
   */
  thrift::NodeAirtime getNodeAirtime() const;

  //
  // Convenience methods to retrieve shared pointers to config structs.
  //

  /** Returns a pointer to the node config structure. */
  std::shared_ptr<const thrift::NodeConfig> getNodeConfig() const;
  /** Returns a pointer to the link config structure. */
  std::shared_ptr<const std::map<std::string, thrift::LinkParams>>
      getLinkConfig() const;
  /** Returns a pointer to the environment config structure. */
  std::shared_ptr<const thrift::NodeEnvParams> getEnvConfig() const;
  /** Returns a pointer to the stats agent config structure. */
  std::shared_ptr<const thrift::StatsAgentParams> getStatsAgentParams() const;
  /** Returns a pointer to the logtail config structure. */
  std::shared_ptr<const thrift::LogTailParams> getLogTailParams() const;
  /** Returns a pointer to the KvStore config structure. */
  std::shared_ptr<const std::map<std::string, std::string>>
      getKvstoreParams() const;
  /** Returns a pointer to the Open/R config structure. */
  std::shared_ptr<const thrift::OpenrParams> getOpenrParams() const;
  /** Returns a pointer to the DHCP config structure. */
  std::shared_ptr<const thrift::DhcpParams> getDhcpParams() const;
  /** Returns a pointer to the system config structure. */
  std::shared_ptr<const thrift::SystemParams> getSysParams() const;
  /** Returns a pointer to the topology info structure. */
  std::shared_ptr<const thrift::TopologyInfo> getTopologyInfo() const;
  /** Returns a pointer to the bgpParams config structure. */
  std::shared_ptr<const thrift::BgpParams> getBgpParams() const;
  /** Returns a pointer to the eapolParams config structure. */
  std::shared_ptr<const thrift::EAPoLParams> getEAPoLParams() const;

 private:
  /**
   * Initialize all internal shared pointers based on the current nodeConfig_.
   */
  void initializePointers();

  /**
   * Compute the MD5 hash of the config JSON and save the old MD5 hash before
   * writing the new node config.
   */
  void computeConfigMd5();

  /** The location of the config file. */
  std::string nodeConfigFile_;

  /** The config file contents (as a JSON string). */
  std::string nodeConfigJson_;

  /** The config file contents (as a dynamic object). */
  folly::dynamic nodeConfigDynamic_ = folly::dynamic::object;

  /** The MD5 hash of the config JSON. */
  std::string configMd5_;

  /** The MD5 hash of the config JSON prior to writing the new node config. */
  std::string prevConfigMd5_;

  /** Whether or not the node is scheduled to perform a delayed node action. */
  bool hasDelayedNodeAction_{false};

  // Shared pointers to config structs.
  /** \{ **/
  std::shared_ptr<thrift::NodeConfig> nodeConfig_;
  std::shared_ptr<thrift::LinkParams> linkParamsBase_;
  std::shared_ptr<thrift::RadioParams> radioParamsBase_;
  std::shared_ptr<thrift::NodeEnvParams> envParams_;
  std::shared_ptr<thrift::StatsAgentParams> statsAgentParams_;
  std::shared_ptr<thrift::LogTailParams> logTailParams_;
  std::shared_ptr<std::map<std::string, std::string>> kvstoreParams_;
  std::shared_ptr<thrift::OpenrParams> openrParams_;
  std::shared_ptr<thrift::DhcpParams> dhcpParams_;
  std::shared_ptr<thrift::SystemParams> sysParams_;
  std::shared_ptr<thrift::TopologyInfo> topologyInfo_;
  std::shared_ptr<thrift::BgpParams> bgpParams_;
  std::shared_ptr<thrift::EAPoLParams> eapolParams_;
  /** \} **/
};

} // namespace terragraph
} // namespace facebook
