/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <deque>

#include <folly/dynamic.h>

#include "e2e/common/ConfigMetadata.h"
#include "e2e/common/ConfigUtil.h"
#include "e2e/common/Consts.h"
#include "e2e/common/EventClient.h"
#include "e2e/if/gen-cpp2/NodeConfig_types.h"
#include "topology/TopologyWrapper.h"

namespace facebook {
namespace terragraph {

/**
 * Helper for managing and manipulating Terragraph's layered node configuration.
 */
class ConfigHelper {
 public:
  /**
   * Empty constructor.
   *
   * The caller is expected to invoke setConfigFiles() before using any other
   * methods.
   */
  ConfigHelper() {};

  /**
   * Node config state.
   *
   * This struct provides quick access to frequently-used node config elements.
   */
  struct NodeConfigState {
    /** Whether this node's config is managed. */
    bool isManaged;
    /** The MD5 hash of this node's config. */
    std::string md5;
    /** This node's software version. */
    std::string swVersion;
    /** This node's firmware version. */
    std::string fwVersion;
    /** This node's hardware board ID. */
    std::string hwBoardId;
    /**
     * This node's computed config.
     * @see buildNodeConfig()
     */
    folly::dynamic configObj;
    /** This node's computed config, as a formatted JSON string. */
    std::string configJson;
    /** The latest node status report timestamp. */
    std::int64_t statusTime;
    /** The latest time that new node config was set. */
    std::int64_t configTime;
    /** Whether this node's hardware type and/or version are unknown. */
    bool isUnknownHardware;
    /** The latest time that we requested base config from this node. */
    std::int64_t baseConfigRequestedTime;
  };

  /**
   * Per-link topology parameters.
   *
   * These were previously part of the thrift::Link struct.
   */
  struct TopologyLinkParams {
    /** Golay index. */
    std::optional<thrift::GolayIdx> golayIdx = std::nullopt;
    /** Control superframe. */
    std::optional<int64_t> controlSuperframe = std::nullopt;
  };

  /**
   * Set the config file names.
   * This also reads the new config files and re-initializes the object.
   */
  void setConfigFiles(
      const std::string& baseConfigDir,
      const std::string& firmwareBaseConfigDir,
      const std::string& hardwareBaseConfigDir,
      const std::string& hardwareTypesFile,
      const std::string& nodeConfigOverridesFile,
      const std::string& autoNodeConfigOverridesFile,
      const std::string& networkConfigOverridesFile,
      const std::string& nodeConfigMetadataFile,
      const std::string& configBackupDir,
      const std::unordered_set<std::string>& topologyNodeNames);

  /**
   * Construct the full node config using the provided layers in order:
   * - Base
   * - Firmware base
   * - Hardware base
   * - Automatic node overrides
   * - Network overrides
   * - User node overrides
   */
  folly::dynamic buildNodeConfig(
      const std::optional<std::string> nodeName,
      const std::string& swVersion,
      const std::optional<std::string> fwVersion,
      const std::optional<std::string> hwBoardId,
      const std::optional<folly::dynamic> autoNodeOverrides,
      const std::optional<folly::dynamic> networkOverrides,
      const std::optional<folly::dynamic> nodeOverrides);

  /**
   * Update tunnel config auto layer when a node overrides is set.
   */
  void updateTunnelConfigAutoLayerForNodeOverride(
      const std::string& nodeName,
      const folly::dynamic& newTunnelConfig,
      std::string& errorMsg);

  /**
   * Validate and set the given node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides the node overrides to validate and set
   * @param topologyNodeNames vector of node names in topology (for validation)
   * @param newNodesOverrides populated with node-override pairs
   * @param overwrite true if existing node overrides should be completely
   *                  overwritten; false if overrides should be merged
   * @param errorMsg failure cause (when return is false) or
   *                 unrecognized configs (when return is true)
  */
  bool validateAndSetNewNodeOverrides(
      const std::string& overrides,
      const std::unordered_set<std::string>& topologyNodeNames,
      folly::dynamic& newNodesOverrides,
      bool overwrite,
      std::string& errorMsg);

  /**
   * Validate the given node overrides.
   *
   * Returns true if valid.
   *
   * @param overrides node overrides to validate
   * @param topologyNodeNames vector of node names in topology (for validation)
   * @param newNodesOverrides populated with node-override pairs
   * @param errorMsg failure cause (when return is false) or
   *                 unrecognized configs (when return is true)
   */
  bool validateNewNodeOverrides(
      const std::string& overrides,
      const std::unordered_set<std::string>& topologyNodeNames,
      folly::dynamic& newNodesOverrides,
      std::string& errorMsg);

  /**
   * Save the given node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides full node overrides object
   * @param errorMsg populated with failure cause (when return is false)
   */
  bool setNewNodeOverrides(
      const folly::dynamic& overrides, std::string& errorMsg);

  /**
   * Save the given node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides full node overrides JSON string
   * @param errorMsg populated with failure cause (when return is false)
   */
  bool setNewNodeOverrides(
      const std::string& overrides, std::string& errorMsg);

  /**
   * Save the given automatic node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides full node overrides object
   * @param errorMsg populated with failure cause (when return is false)
   */
  bool setNewAutoNodeOverrides(
      const folly::dynamic& overrides, std::string& errorMsg);

  /**
   * Save the given automatic node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides full node overrides JSON string
   * @param errorMsg populated with failure cause (when return is false)
   */
  bool setNewAutoNodeOverrides(
      const std::string& overrides, std::string& errorMsg);

  /**
   * Validate and set the given network overrides.
   *
   * Returns true if successful.
   *
   * @param overrides network overrides to validate and set
   * @param newNetworkOverrides populated with network-override pairs
   * @param overwrite true if existing network overrides should be completely
   *                  overwritten; false if overrides should be merged
   * @param errorMsg failure cause (when return is false) or
   *                 unrecognized configs (when return is true)
   */
  bool validateAndSetNewNetworkOverrides(
      const std::string& overrides,
      folly::dynamic& newNetworkOverrides,
      bool overwrite,
      std::string& errorMsg);

  /**
   * Validate the given network overrides.
   *
   * Returns true if successful.
   *
   * @param overrides network overrides to validate
   * @param newNetworkOverrides populated with network-override pairs
   * @param errorMsg failure cause (when return is false) or
   *                 unrecognized configs (when return is true)
   */
  bool validateNewNetworkOverrides(
      const std::string& overrides,
      folly::dynamic& newNetworkOverrides,
      std::string& errorMsg);

  /**
   * Set new network overrides without validation.
   *
   * Returns true if successful, otherwise puts failure cause in 'errorMsg' and
   * returns false.
   */
  bool setNewNetworkOverrides(
      const std::string& overrides, std::string& errorMsg);

  /**
   * Get the network overrides as a JSON string.
   *
   * Returns std::nullopt if it fails to serialize (not expected to ever
   * happen).
   */
  std::optional<std::string> getNetworkOverridesJson() const;

  /**
   * Get the node overrides as a JSON string.
   *
   * Returns std::nullopt if it fails to serialize (not expected to ever
   * happen).
   *
   * @param nodeNames list of nodes (all nodes if empty)
   */
  std::optional<std::string> getNodeOverridesJson(
      const std::vector<std::string>& nodeNames) const;

  /**
   * Get the automatic node overrides as a JSON string.
   *
   * Returns std::nullopt if it fails to serialize (not expected to ever
   * happen).
   *
   * @param nodeNames list of nodes (all nodes if empty)
   */
  std::optional<std::string> getAutoNodeOverridesJson(
      const std::vector<std::string>& nodeNames) const;

  /**
   * Get the base version string that best matches the given software version.
   *
   * If 'swVer' is not a valid SwVersion (major, minor), try to match by tokens.
   *
   * Returns std::nullopt if all forms of matching fail.
   *
   * @param swVer the software version
   * @param baseConfigObjects if omitted, will use baseConfigObjects_
   */
  std::optional<std::string> matchBaseVersion(
      const SwVersion& swVer,
      const std::optional<folly::dynamic>
          baseConfigObjects = std::nullopt) const;

  /**
   * Get the firmware version string that best matches the given version.
   *
   * Returns std::nullopt if all forms of matching fail.
   *
   * @param fwVer the firmware version
   * @param fwConfigObjects if omitted, will use fwConfigObjects_
   */
  std::optional<std::string> matchFirmwareVersion(
      const std::string& fwVer,
      const std::optional<folly::dynamic>
          fwConfigObjects = std::nullopt) const;

  /**
   * Get the base config for the specified software versions as a JSON string.
   *
   * If 'swVersions' is empty, returns for all available base configs.
   */
  std::string getBaseConfigJson(const std::vector<std::string>& swVersions);

  /**
   * Get the firmware base config for the specified firmware versions as a JSON
   * string.
   *
   * If 'fwVersions' is empty, returns for all available firmware base configs.
   */
  std::string getFirmwareBaseConfigJson(
      const std::vector<std::string>& fwVersions);

  /**
   * Get the hardware base config for the specified hardware board IDs and
   * software versions as a JSON string.
   *
   * If 'hwBoardIds' or 'swVersions' is empty, returns for all available
   * hardware base configs.
   */
  std::string getHardwareBaseConfigJson(
      const std::vector<std::string>& hwBoardIds,
      const std::vector<std::string>& swVersions);

  /** Get the latest base config version string. */
  std::string getLatestBaseVersion() const;

  /**
   * Get the config state for the specified node.
   *
   * Returns std::nullopt if no config state exists.
   */
  std::optional<NodeConfigState> getConfigState(
      const std::string& nodeName) const;

  /** Get the config state for all nodes. */
  std::unordered_map<std::string, NodeConfigState> getAllConfigStates() const;

  /** Initialize the config state for a node. */
  std::optional<NodeConfigState> initConfigState(
      const std::string& nodeName,
      const std::string& swVersion,
      const std::string& fwVersion,
      const std::string& hwBoardId);

  /** Returns whether the given board ID and software version are recognized. */
  bool isUnknownHardware(
      const std::string& hwBoardId, const std::string& swVersion) const;

  /** Update the `isUnknownHardware` flag in a node's config state. */
  void updateNodeIsUnknownHardware(const std::string& nodeName);

  /** Update a node's config state with the latest node status report time. */
  void setNodeStatusTime(
      const std::string& nodeName, const std::int64_t statusTime);

  /**
   * Update a node's config state with the latest time that new node config was
   * set.
   */
  void setNodeConfigTime(
      const std::string& nodeName, const std::int64_t configTime);

  /**
   * Update a node's config state with the latest time that we requested base
   * configs.
   */
  void setNodeBaseConfigRequestedTime(
      const std::string& nodeName, const std::int64_t baseConfigRequestedTime);

  /** Check if the node configuration is managed. */
  bool isManaged(const folly::dynamic& config) const;

  /**
   * Get the node config metadata as a JSON string.
   *
   * Returns std::nullopt if it fails to serialize (not expected to ever
   * happen).
   */
  std::optional<std::string> getConfigMetadata() const;

  /** Migrate node config overrides for a node that was renamed. */
  bool migrateNodeOverrides(
      const std::string& oldNodeName, const std::string& newNodeName);

  /**
   * Set the automatic node override for 'specificNetworkPrefixes'.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setAutoNodeSpecificNetworkPrefixes(
      const std::string& nodeName,
      const std::optional<std::string> specificNetworkPrefixes,
      std::string& errorMsg);

  /**
   * Set automatic node override for 'dstIp'.
   */
  bool setAutoNodeSpecificTunnelConfigIp(
      const std::string& nodeName,
      const std::unordered_map<std::string, std::optional<std::string>>&
          tunnelConfigUpdates,
      std::string& errorMsg);

  /** Perform actions when a node IP address changes. */
  void onIpAddrChange(const thrift::Node& node, const std::string& ipv6Address);

  /** Get the automatic node override for 'specificNetworkPrefixes'. */
  std::optional<std::string> getAutoNodeSpecificNetworkPrefixes(
      const std::string& nodeName);

  /**
   * Set the auto node override for 'envParams.OPENR_STATIC_PREFIX_ALLOC'.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setAutoNodeOpenrStaticPrefixAlloc(
      const std::string& nodeName,
      const bool openrStaticPrefixAllocEnabled,
      std::string& errorMsg);

  /**
   * Get the config from linkParams that was formerly stored in the topology.
   */
  TopologyLinkParams getLinkTopologyConfig(
      const std::string& nodeName, const std::string& responderMac) const;

  /**
   * Set the automatic node override for 'topologyInfo' for each given node.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setNodeTopologyInfo(
      const std::vector<std::string>& nodeNames,
      const TopologyWrapper& topologyW,
      std::string& errorMsg);

  /**
   * Set the user/auto node override for
   * 'radioParamsOverride[radioMac].fwParams.polarity'.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setNodePolarity(
      const std::string& nodeName,
      const std::optional<std::string> radioMac,
      const std::optional<thrift::PolarityType> polarity,
      bool forUserConfig,
      std::string& errorMsg);

  /**
   * Set the user/auto node override for 'polarity' for all MAC addresses in the
   * provided map.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setNodePolarities(
      std::unordered_map<std::string, std::optional<thrift::PolarityType>>
          mac2Polarity,
      std::map<std::string, std::string> mac2NodeName,
      bool forUserConfig,
      std::string& errorMsg);

  /**
   * Set the node override for 'controlSuperframe' for both ends of the link
   * (the value is duplicated).
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setLinkControlSuperframe(
      const thrift::Link& link,
      const std::optional<int64_t> controlSuperframe,
      bool forUserConfig,
      std::string& errorMsg);

  /**
   * Set the node override for 'golayIdx' for both ends of the link (the value
   * is duplicated).
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setLinkGolay(
      const thrift::Link& link,
      const std::optional<thrift::GolayIdx> golayIdx,
      bool forUserConfig,
      std::string& errorMsg);

  /**
   * Set the node override for 'channel' for both ends of the link (the value
   * is duplicated).
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setLinkChannel(
      const thrift::Link& link,
      const std::optional<int8_t> channel,
      bool forUserConfig,
      std::string& errorMsg);

  /**
   * Set the node override for 'laMaxMcs' for a link.
   *
   * Returns true if successful, otherwise puts the failure cause in 'errorMsg'
   * and returns false.
   */
  bool setLinkMaxMcs(
      const std::string& nodeName,
      const std::string& radioMac,
      const std::string& responderMac,
      const int laMaxMcs,
      std::string& errorMsg);

  /**
   * Migrate 'radioParamsOverride' for the given nodeName from 'oldMac' to
   * 'newMac'.
   */
  bool updateAutoNodeTopologyConfigMac(
      const std::string& nodeName,
      const std::string& oldMac,
      const std::string& newMac,
      std::string& errorMsg);

  /**
   * Migrate 'linkParamsOverride' for the given link from 'oldMac' to 'newMac'.
   */
  bool updateAutoLinkTopologyConfigMac(
      const thrift::Link& link,
      const std::string& oldMac,
      const std::string& newMac,
      std::string& errorMsg);

  /**
   * Delete the automatic node overrides for 'linkParamsOverride' for both nodes
   * on the given link.
   */
  bool deleteAutoLinkOverrides(
      const thrift::Link& link, std::string& errorMsg);

  /** Delete the automatic and user node overrides for the given node. */
  bool deleteAllNodeOverrides(
      const std::string& nodeName, std::string& errorMsg);

  /**
   * Returns a map of WLAN MAC addresses to their assigned polarity.
   * @param nodes the list of nodes
   * @param userConfiguredOnly if true, only include MAC addresses that have a
   *                           user config override
   */
  std::unordered_map<std::string, thrift::PolarityType> getPolarities(
      const std::vector<thrift::Node>& nodes,
      bool userConfiguredOnly = false) const;

  /**
   * Get all node configs for a node as specified in the provided config paths.
   *
   * A config path can have wildcards ('*') that enumerate all valid configs at
   * that path layer.
   */
  std::optional<folly::dynamic> getNodeConfigPaths(
      const std::string& nodeName, const std::vector<std::string>& configPaths);

  /**
   * Get the assigned polarity of a specific radio.
   *
   * @param nodeName the node name
   * @param macAddr the radio MAC address
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present)
   */
  std::optional<thrift::PolarityType> getRadioPolarity(
      const std::string& nodeName,
      const std::string& macAddr,
      bool userConfiguredOnly) const;

  /**
   * Get the configured channel of a specific radio.
   *
   * @param nodeName the node name
   * @param macAddr the radio MAC address
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present), and 'autoConfiguredOnly'
   *                           is ignored
   * @param autoConfiguredOnly if true and 'userConfiguredOnly' is false, only
   *                           the auto-configured value is returned (if
   *                           present)
   */
  std::optional<int8_t> getRadioChannel(
      const std::string& nodeName,
      const std::string& macAddr,
      bool userConfiguredOnly,
      bool autoConfiguredOnly) const;

  /**
   * Get the configured channel of a specific link.
   *
   * @param link the link
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present), and 'autoConfiguredOnly'
   *                           is ignored
   * @param autoConfiguredOnly if true and 'userConfiguredOnly' is false, only
   *                           the auto-configured value is returned (if
   *                           present)
   */
  std::optional<int8_t> getLinkChannel(
      const thrift::Link& link,
      bool userConfiguredOnly,
      bool autoConfiguredOnly) const;

  /** Returns whether 'forceGpsDisable' > 0 for any radio on the given node. */
  bool isForceGpsDisable(const thrift::Node& node) const;

  /** Add auto configurations for a link that was added to topology. */
  void onAddLink(const TopologyWrapper& topologyW, const thrift::Link& newLink);

  /** Adjust auto configurations after changing a radio MAC address. */
  void onChangeRadioMac(
      const TopologyWrapper& topologyW,
      const std::string& nodeName,
      const std::string& oldMacAddr,
      const std::string& newMacAddr);

  /**
   * Get the configured Golay index of a specific link.
   *
   * @param link the link
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present)
   */
  std::optional<thrift::GolayIdx> getLinkGolayIdx(
      const thrift::Link& link, bool userConfiguredOnly);

  /**
   * Get the configured Golay index of a specific link.
   *
   * @param nodeName the node name
   * @param responderMac the responder MAC address
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present)
   */
  std::optional<thrift::GolayIdx> getLinkGolayIdx(
      const std::string& nodeName,
      const std::string& responderMac,
      bool userConfiguredOnly);

  /**
   * Get the configured control superframe of a specific link.
   *
   * @param nodeName the node name
   * @param responderMac the responder MAC address
   * @param userConfiguredOnly if true, only the user-configured value is
   *                           returned (if present)
   */
  std::optional<int64_t> getLinkControlSuperframe(
      const std::string& nodeName,
      const std::string& responderMac,
      bool userConfiguredOnly) const;

  /**
   * Validate link configuration (polarity, golay and control superframe).
   *
   * Generates events for validation failures and attempts to fix invalid
   * configuration.
   */
  bool validateLinkConfig(
      const TopologyWrapper& topologyW,
      const thrift::Link& link,
      EventClient& eventClient);

  /** Returns the set of enabled channels used in automatic assignment. */
  std::set<int8_t> getEnabledChannels() const;

  /**
   * Returns the set of channels currently in use in the topology.
   *
   * User layer configs are ignored.
   */
  std::set<int8_t> getUsedChannels(const TopologyWrapper& topologyW) const;

  /**
   * Clear specified auto layer overrides for specified nodes.
   *
   * Returns true on success, false otherwise.
   */
  bool clearAutoNodeOverrides(
      std::vector<std::string> nodeNames,
      std::vector<std::string> configPaths,
      std::string& errorMsg);

  /**
   * Add a new base config (without overwriting).
   *
   * Returns true on success, false otherwise.
   */
  bool addBaseConfig(const std::string& ver, const std::string& configJson);

  /**
   * Add a new hardware base config (without overwriting).
   *
   * Returns true on success, false otherwise.
   */
  bool addHardwareBaseConfig(
      const std::string& hwType,
      const std::string& ver,
      const std::string& configJson);

  /**
   * Add a new hardware type and board ID association (without overwriting).
   *
   * Returns true on success, false otherwise.
   */
  bool addHardwareTypeMapping(
      const std::string& hwBoardId, const std::string& hwType);

  /**
   * Returns the hardware type for the given board ID, or std::nullopt if no
   * mapping exists.
   */
  std::optional<std::string> getHardwareTypeMapping(
      const std::string& hwBoardId) const;

  /**
   * Merge the given config metadata into the existing metadata.
   *
   * Returns true if successful, false otherwise.
   *
   * TODO: This is implemented as a plain JSON merge without overwriting
   *       existing keys. It should do more careful checks to avoid modifying
   *       existing metadata structures in any way (e.g. adding additional
   *       constraints to existing keys is possible, but shouldn't be).
   */
  bool mergeConfigMetadata(
      const std::string& configMeta, bool validateCfgAction = true);

 private:
  /**
   * Get the base config that best matches the given software version,
   * via matchBaseVersion(), and using 'defaultVersion' if no match was found.
   *
   * This will read the config file from disk if not already loaded.
   *
   * If no associated base config is found in 'baseConfigObjects', this returns
   * an empty folly::dynamic::object.
   */
  folly::dynamic getBaseConfig(
      const SwVersion& swVer,
      folly::dynamic& baseConfigObjects,
      const std::string& defaultVersion,
      bool isHwBaseConfig);

  /**
   * Get the firmware config that best matches the given firmware version,
   * via matchFirmwareVersion().
   *
   * This will read the config file from disk if not already loaded.
   *
   * If no associated firmware config is found in 'fwConfigObjects', this
   * returns an empty folly::dynamic::object.
   */
  folly::dynamic getFirmwareConfig(
      const std::string& fwVer, folly::dynamic& fwConfigObjects);

  /**
   * Read, parse, and validate all base config files in a directory.
   *
   * Returns a [config, latestSwVer] pair.
   */
  std::pair<folly::dynamic, std::string> readBaseConfigFiles(
      const std::string& directory) const;

  /**
   * Read all configs from disk (base versions, overrides).
   * @param topologyNodeNames vector of node names in the topology (for
   *                          validation)
   */
  void readLocalConfigStore(
      const std::unordered_set<std::string>& topologyNodeNames);
  /** Read base configs from disk. */
  void readBaseConfigs();
  /** Read firmware base configs from disk. */
  void readFirmwareBaseConfigs();
  /** Read hardware base configs from disk. */
  void readHardwareBaseConfigs();
  /** Read the hardware types map from disk. */
  void readHardwareTypesFile();
  /** Read network overrides from disk. */
  void readNetworkConfigOverrides();
  /**
   * Read user node overrides from disk.
   * @param topologyNodeNames vector of node names in the topology (for
   *                          validation)
   */
  void readNodeConfigOverrides(
      const std::unordered_set<std::string>& topologyNodeNames);
  /**
   * Read automatic node overrides from disk.
   * @param topologyNodeNames vector of node names in the topology (for
   *                          validation)
   */
  void readAutoNodeConfigOverrides(
      const std::unordered_set<std::string>& topologyNodeNames);

  /**
   * Filter out validation result entries that aren't from the given overrides.
   */
  void filterValidationResults(
      ConfigMetadata::ValidationResult& result,
      const folly::dynamic& overrides) const;

  /**
   * Get the node overrides as a JSON string.
   *
   * Returns std::nullopt if it fails to serialize (not expected to ever
   * happen).
   *
   * @param nodeNames list of nodes (all nodes if empty)
   * @param nodeOverrides node overrides object containing all node overrides
   */
  std::optional<std::string> getPerNodeOverridesJson(
      const std::vector<std::string>& nodeNames,
      const folly::dynamic& nodeOverrides) const;

  /**
   * Save the given node overrides.
   *
   * Returns true if successful.
   *
   * @param overrides full node overrides JSON strng
   * @param nodeConfigOverridesFile file to write node overrides to
   * @param nodeOverrides populated with overrides if successful
   * @param errorMsg populated with failure cause (when return is false)
   */
  bool setNewPerNodeOverrides(
      const folly::dynamic& overrides,
      const std::string& nodeConfigOverridesFile,
      folly::dynamic& nodeOverrides,
      std::string& errorMsg);

  /** Migrate node overrides from 'oldNodeName' to 'newNodeName'. */
  bool migratePerNodeOverrides(
      const std::string& oldNodeName,
      const std::string& newNodeName,
      folly::dynamic& nodeOverrides,
      const std::string& nodeConfigOverridesFile);

  /** Create the directory tree, as needed, for the given file path. */
  bool createDirectory(const std::string& filepath);

  /** Read per-node config overrides from disk. */
  void readPerNodeConfigOverrides(
      const std::unordered_set<std::string>& topologyNodeNames,
      const std::string& nodeConfigOverridesFile,
      folly::dynamic& nodeOverrides,
      bool isAutoLayer);

  /** Save a network/auto/node config to a backup file. */
  void writeBackupConfigFile(
      const std::string& configPath, const folly::dynamic& config) const;

  /**
   * Read and return the given base config file from disk.
   *
   * Throws various exceptions upon failure.
   *
   * @param path the config file path
   * @param swVersion if given, apply base configs when validating,
   *                  e.g. when reading hardware base configs
   */
  folly::dynamic loadConfigFile(
      const std::string& path,
      const std::optional<std::string> swVersion = std::nullopt);

  /**
   * Merge and return a node config blob made up of only the automatic and user
   * overrides.
   *
   * If 'nodeName' is not provided, only 'networkOverrides' are returned.
   */
  folly::dynamic getConfigOverridesForNode(
      const std::optional<std::string> nodeName,
      const std::optional<folly::dynamic> autoNodeOverrides,
      const std::optional<folly::dynamic> networkOverrides,
      const std::optional<folly::dynamic> nodeOverrides) const;

  /**
   * Set the node override for 'controlSuperframe' for a single node in a link.
   */
  void setLinkControlSuperframeForNode(
      folly::dynamic& autoNodesOverrides,
      const std::string& nodeName,
      const std::string& responderMac,
      const std::optional<int64_t> controlSuperframe);

  /** Set the node override for 'golayIdx for a single node in a link. */
  void setLinkGolayForNode(
      folly::dynamic& nodesOverrides,
      const std::string& nodeName,
      const std::string& responderMac,
      const std::optional<thrift::GolayIdx> golayIdx);

  /** Set the radio override for 'channel' for a single node in a link. */
  void setLinkChannelForRadio(
      folly::dynamic& nodesOverrides,
      const std::string& nodeName,
      const std::string& radioMac,
      const std::optional<int8_t> channel);

  /**
   * Returns a reference to the folly::dynamic located at the end of 'keyPath'.
   *
   * 'keyPath' is an ordered list of keys the function will traverse. If any key
   * in the path does not exist, an empty folly::dynamic::object will be created
   * and the traversal will continue until it reaches the end of path.
   */
  folly::dynamic& getDynamicObjectAtPath(
      folly::dynamic& obj, std::deque<std::string> keyPath) const;

  /**
   * Convert the value at the given key to an int64_t if possible, otherwise
   * return std::nullopt.
   */
  std::optional<int64_t> maybeReadInt(
      const folly::dynamic& obj, const std::string& key) const;

  /**
   * Migrate linkParamsOverride for the given node name from 'oldMac' to
   * 'newMac'.
   */
  void updateAutoLinkTopologyConfigMacforNode(
      folly::dynamic& autoNodeOverrides,
      const std::string& nodeName,
      const std::string& oldMac,
      const std::string& newMac);

  /** Delete the link config for a given node and responder MAC address. */
  void deleteAutoLinkOverridesForNode(
      folly::dynamic& autoNodeOverrides,
      const std::string& nodeName,
      const std::string& responderMac);

  /**
   * Returns a standardized form of the given MAC address.
   *
   * If the input string is empty, returns an empty string.
   *
   * If the input string is not a valid MAC address, return the input string.
   */
  std::string getStandardizedMac(const std::string& mac) const;

  /**
   * Returns all configurations from a provided config object as specified by
   * the provided config path.
   *
   * A config path can have wildcards ('*') that enumerate all valid configs at
   * that path layer.
   */
  std::optional<folly::dynamic> getConfigPathWithWildcards(
      folly::dynamic& configObj, std::deque<std::string> configPathTokens);

  /**
   * Returns information about neighbor nodes (specifically, nodes which this
   * node has a link to).
   */
  std::optional<std::unordered_map<std::string, thrift::NeighborInfo>>
      getNeighborInfo(const TopologyWrapper& topologyW, thrift::Node& node);

  /**
   * Returns an assignment of nodes to radios to colors based on the node's
   * site location. The color is used to determine the ignition slot of the
   * radio when attempting to self-ignite, and is based on polarity.
   */
  std::unordered_map<std::string /* nodeName */,
      std::unordered_map<std::string /* radioMac */, int /* color */>>
      getColorAssignments(const TopologyWrapper& topologyW);

  /** Get the Golay index from the provided dynamic object. */
  thrift::GolayIdx getGolayIdx(
      folly::dynamic& nodeOverrides, const std::string& responderMac);

  /** Name of folder containing all base config files. */
  std::string baseConfigDir_;

  /** Name of folder containing all firmware base config files. */
  std::string firmwareBaseConfigDir_;

  /** Name of folder containing all hardware base config files. */
  std::string hardwareBaseConfigDir_;

  /** Name of JSON file holding hardware config types mapping. */
  std::string hardwareTypesFile_;

  /** Name of JSON file holding all node config overrides. */
  std::string nodeConfigOverridesFile_;

  /** Name of JSON file holding all automatic node config overrides. */
  std::string autoNodeConfigOverridesFile_;

  /** Name of JSON file holding network-wide config overrides. */
  std::string networkConfigOverridesFile_;

  /** Name of folder containing backup config files. */
  std::string configBackupDir_;

  /** Per-node config state. */
  std::unordered_map<std::string, NodeConfigState> configState_{};

  /**
   * Base config objects (swVer -> config).
   *
   * If the value is a string (instead of an object), the config has not yet
   * been loaded from disk, and the string is the config file path.
   */
  folly::dynamic baseConfigObjects_ = folly::dynamic::object;

  /** Firmware base config objects (fwVer -> config). */
  folly::dynamic fwConfigObjects_ = folly::dynamic::object;

  /** Hardware base config objects (hw -> swVer -> config). */
  folly::dynamic hwConfigObjects_ = folly::dynamic::object;

  /** Config overrides per node. */
  folly::dynamic nodesOverrides_ = folly::dynamic::object;

  /** Automatic config overrides per node. */
  folly::dynamic autoNodesOverrides_ = folly::dynamic::object;

  /** Network config overrides. */
  folly::dynamic networkOverrides_ = folly::dynamic::object;

  /** Node config metadata. */
  std::unique_ptr<ConfigMetadata> nodeConfigMeta_;

  /** Latest base config version (by major/minor version). */
  std::string latestBaseVersion_;

  /** Latest hardware base config versions (by major/minor version). */
  std::unordered_map<std::string /* hw */, std::string /* swVer */>
      latestHwBaseVersions_;

  /** Map of hardware board IDs to hardware config types. */
  std::unordered_map<std::string, std::string> hardwareConfigTypeMap_;
};

} // namespace terragraph
} // namespace facebook
