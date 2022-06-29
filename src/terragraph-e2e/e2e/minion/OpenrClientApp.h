/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <fbzmq/async/ZmqTimeout.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/Expected.h>
#include <openr/common/OpenrClient.h>

#include "MinionApp.h"
#include "e2e/common/Consts.h"
#include "e2e/if/gen-cpp2/Controller_types.h"

namespace facebook {
namespace terragraph {
namespace minion {

/**
 * App that handles Open/R-related functions.
 */
class OpenrClientApp final : public MinionApp {
 public:
  /**
   * Constructor.
   *
   * This will create the connection to Open/R and initialize various timers.
   *
   * @param zmqContext the ZMQ context
   * @param brokerRouterUrl the broker address for the E2E minion
   * @param monitorSockUrl the ZmqMonitor address for the E2E minion
   * @param macAddr our MAC address
   * @param myNetworkInfoFile the network information file
   */
  OpenrClientApp(
      fbzmq::Context& zmqContext,
      const std::string& brokerRouterUrl,
      const std::string& monitorSockUrl,
      const std::string& macAddr,
      const std::string& myNetworkInfoFile);

  void processMessage(
      const std::string& senderApp,
      const thrift::Message& message) noexcept override;

 private:
  /** Open/R KvStore error types. */
  enum class KvStoreError { CONNECTION_ERROR, KEY_NOT_FOUND, EMPTY_VALUE };

  /** Process a request for routing adjacencies. */
  void processGetRoutingAdjacencies(const std::string& senderApp);
  /** Process a request to set link metrics. */
  void processSetLinkMetric(
      const std::string& senderApp, const thrift::Message& message);
  /** Process a request to sync with LinkMonitor. */
  void processSyncLinkMonitor();
  /** Process a request to sync with KvStore. */
  void processInjectKvstoreKeys();

  /**
   * Process a request to set prefixes.
   *
   * This writes the given prefixes via kvStoreClient_.
   */
  void processSetPrefixes(
      const std::string& senderApp, const thrift::Message& message);

  /** Process a request for local adjacencies from our firmware. */
  void processFwAdjReq(
      const std::string& senderApp, const thrift::Message& message);

  /** Injects KvStore keys from the node config file. */
  void injectKvStoreKeys();

  /** Periodic sync of the network information file with KvStore. */
  void networkInfoSync();

  /** Periodic sync with LinkMonitor. */
  void linkMonitorSync();

  /**
   * Constructs NetworkInfo by reading values from KvStore.
   *
   * If a read fails due to an Open/R connection error, returns std::nullopt.
   */
  std::optional<thrift::NetworkInfo> createNetworkInfo();

  /**
   * Read the network information file and deserialize it.
   *
   * Returns std::nullopt on failure.
  */
  std::optional<thrift::NetworkInfo> readMyNetworkInfoFile();

  /**
   * Write the given NetworkInfo struct to the network information file.
   *
   * Returns true if successful.
  */
  bool writeMyNetworkInfoFile(const thrift::NetworkInfo& networkInfo);

  /**
   * Read the value of the given key from KvStore, and get the Value struct.
   *
   * Upon failure, returns KvStoreError specifying the cause. Upon success,
   * returns the Value thrift struct.
  */
  folly::Expected<openr::thrift::Value, KvStoreError> kvStoreGetValue(
        const std::string& key);

  /**
   * Read the value of the given key from KvStore, and get the content of the
   * value.
   *
   * Upon failure, returns KvStoreError specifying the cause. Upon success,
   * returns a string containing the contents of the value without metadata.
  */
  folly::Expected<std::string, KvStoreError> kvStoreGetValueString(
        const std::string& key);

  /**
   * Get the version of a key-value pair in KvStore.
   *
   * Returns 0 if the key is not present in KvStore.
  */
  int64_t kvStoreGetVersion(const std::string key);

  /**
   * Check if a key exists in KvStore and its value matches the string provided.
   *
   * Returns true if the key is in KvStore and the value matches.
  */
  bool checkKvPairExists(const std::string key, const std::string value);

  /**
   * Create a KeySetParams containing one key-value pair.
   *
   * Returns a KeySetParams.
  */
  openr::thrift::KeySetParams makeKeySetParams(const std::string key,
      const std::string originator, const std::string value,
      int64_t ttl = openr::Constants::kTtlInfinity, int64_t ttlVersion = 0,
      std::optional<int64_t> hash = std::nullopt);

  /**
   * Dump all key-value pairs with the given prefix from KvStore.
   *
   * Returns a map of keys to openr::thrift::Value
  */
  std::map<std::string, openr::thrift::Value>
      kvStoreDumpKeysRaw(const std::string& prefix);

  /**
   * Dump all key-value pairs with the given prefix from KvStore.
   *
   * Returns a map of keys to values.
  */
  std::map<std::string, std::string> kvStoreDumpKeys(const std::string& prefix);

  /**
   * Create pre-M80 prefix entries in KvStore.
  */
  void deprecatedPrefixSync();

  /**
   * Parse the given URI and return a normalized copy of it.
   *
   * Returns std::nullopt if parsing failed.
  */
  std::optional<std::string> parseUri(const std::string& uri);

  /**
   * Set the link metric for the given interface via LinkMonitor.
   *
   * If 'metric' is std::nullopt, this will send an UNSET request instead.
  */
  void setLinkMetric(
      std::optional<int32_t> metric, const std::string& interface);

  /**
   * Fetch the link dump from LinkMonitor.
   *
   * Returns std::nullopt on failure.
  */
  std::optional<openr::thrift::DumpLinksReply> linkMonitorDumpLinks();

  /**
   * Fetch this node's routing adjacencies from KvStore.
   *
   * Returns std::nullopt on failure.
  */
  std::optional<openr::thrift::AdjacencyDatabase> getRoutingAdjacencies();

  /** Initialize the Open/R client (openrCtrlClient_). */
  void initOpenrCtrlClient();

  /** EventBase to create the Open/R client. */
  folly::EventBase evb_;

  /** Cached OpenrCtrlClient instance to talk to Open/R. */
  std::unique_ptr<openr::thrift::OpenrCtrlAsyncClient>
    openrCtrlClient_{nullptr};

  /** Network information file. */
  const std::string myNetworkInfoFile_{};

  /** Is Open/R enabled in node config? */
  bool openrEnabled_{true};

  /** Timer to sync the network information file with KvStore periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> networkInfoSyncTimer_{nullptr};

  /** Timer to inject KvStore values periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> injectKvStoreKeysTimer_{nullptr};

  /** Timer to sync with LinkMonitor periodically. */
  std::unique_ptr<fbzmq::ZmqTimeout> linkMonitorSyncTimer_{nullptr};

  /** Timer to add deprecated prefixes periodically/. */
  std::unique_ptr<fbzmq::ZmqTimeout> deprecatedPrefixSyncTimer_{nullptr};

  /** Map of link MAC addresses to link metrics. */
  std::unordered_map<std::string /* mac */, int32_t> linkMetricMap_{};
};

} // namespace minion
} // namespace terragraph
} // namespace facebook
