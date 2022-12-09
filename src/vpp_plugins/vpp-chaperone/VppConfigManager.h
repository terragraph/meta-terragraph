/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <unordered_map>

#include <folly/dynamic.h>

#include "VppClient.h"

namespace facebook
{
namespace terragraph
{
namespace vpp
{

//
// Config manager class, which contains all VPP config logic.
//
class VppConfigManager final
{
public:
  // Constructor.
  // Parses the given environment file, aborting on any errors encountered.
  VppConfigManager (const std::string &configEnvFile,
                    const std::string &nodeConfigFile);

  // Apply all configs.
  void run (VppClient &vppClient);

private:
  // Apply individual configs.
  void doSlowPathConfig (VppClient &vppClient);
  void doPopConfig (VppClient &vppClient);

  // Apply CPE config.
  void doCpeConfig (VppClient &vppClient);

  // Add and delete CPE Policers.
  void doCpePolicerConfig (VppClient &vppClient, const std::string &interface,
                           const folly::dynamic &policers);

  // Delete Existing CPE Policers.
  void deleteExistingPolicers (VppClient &vppClient,
                               const std::string &cpeInterface);

  // Derive CPE IP prefix based on node prefix.
  std::string deriveCpeIpPrefix (VppClient &vppClient,
                                 const std::string &interface);

  // Configure a CPE interface.
  void doCpeInterfaceConfig (VppClient &vppClient,
                             const std::string &interface,
                             const std::string &prefixCfg);

  // Get the first CPE enabled interface.
  std::string getFirstCpeInterface ();

  // Configure DHCPv6 relay on CPE interface.
  void doCpeDhcpv6RelayConfig (VppClient &vppClient,
                               const std::string &cpeInterface,
                               const folly::dynamic &dhcpRelayConfig);

  // Configure tunnels.
  void doTunnelConfig (VppClient &vppClient);

  // Configure VxLAN L2 tunnel.
  void doVxlanTunnelConfig (VppClient &vppClient,
                            const folly::dynamic &tunnelConfig,
                            VxlanTunnelMap &currTunnels);

  // Add tunnel interface and local interface into L2 bridge on POP node
  // Return TRUE if added into bridge, FALSE otherwise
  bool doVxLanBridgeConfig (VppClient &vppClient, const std::string &srcIface,
                            const std::string &tunnelIface,
                            const std::string &dstNodeIP);

  // Xconnect tunnel interface with local interface on non-POP nodes
  // Return TRUE if Xconnected, FALSE otherwise
  bool doVxLanXconnectConfig (VppClient &vppClient,
                              const std::string &srcIface,
                              const std::string &tunnelIface);

  // Configure SRv6 L2 tunnel.
  void doSrTunnelConfig (VppClient &vppClient,
                         const folly::dynamic &tunnelConfig,
                         Srv6TunnelMap &currSrPolicies);

  // Configure TC-DSCP mapping.
  void doQosMapping (VppClient &vppClient, const folly::dynamic &qos);

  // Set one entry in the TC translation table for a WiGig interface.
  void setTctblEntry (VppClient &vppClient, const u32 sw_if_index,
                      const u32 dscp, const folly::dynamic dscpParams);

  // Basic validation of tunnel config. The config will be changed if
  // localInterface is empty.
  bool validateTunnelConfig (folly::dynamic &tunnel);

  // Validation of POP config.
  bool validatePopConfig ();

  // Configure NAT64.
  void doNat64Config (VppClient &vppClient);

  // Configure QoS.
  void doQosConfig (VppClient &vppClient);

  // Process a node configuration environment file.
  std::unordered_map<std::string, std::string>
  readConfigEnvFile (const std::string &configEnvFile) const;

  // Read a value from envConfig_.
  // Returns an empty string if the key is not found.
  std::string getEnv (const std::string &key) const;

  // Read node config file and save config as dynamic objects.
  void readNodeConfigFile (const std::string &nodeConfigFile);

  // Node configuration map
  std::unordered_map<std::string, std::string> envConfig_;

  // Tunnel config object.
  folly::dynamic tunnelConfig_ = folly::dynamic::object;

  // QoS config object.
  folly::dynamic qosConfig_ = folly::dynamic::object;

  // CPE config object.
  folly::dynamic cpeConfig_ = folly::dynamic::object;

  // The index that is used when deriving CPE interface prefix.
  int cpePrefixIndex_ = 1;

  // kvstoreParams config object.
  folly::dynamic kvstoreParams_ = folly::dynamic::object;
};

} // namespace vpp
} // namespace terragraph
} // namespace facebook
