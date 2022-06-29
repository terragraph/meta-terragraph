/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <folly/portability/GFlags.h>

#include <folly/init/Init.h>
#include <folly/dynamic.h>
#include <folly/String.h>

#include "../ConfigApp.h"
#include "../ConfigHelper.h"
#include "../SharedObjects.h"
#include <e2e/common/ConfigMetadata.h>
#include "e2e/common/ConfigUtil.h"
#include <e2e/common/Consts.h>

#include "CtrlFixture.h"
#include <e2e/common/TestUtils.h>

using namespace std;
using namespace facebook::terragraph;

DEFINE_string(
    controller_config_default_file,
    "/etc/e2e_config/controller_config_default.json",
    "Controller config default file");

DEFINE_string(
    aggregator_config_default_file,
    "/etc/stats_config/aggregator_config_default.json",
    "Aggregator config default file");

namespace {

class ConfigFixture : public CtrlFixture {
 public:
  ConfigFixture()
      : CtrlFixture(),
        configApp_(
            context_,
            ctrlAppSockUrl_,
            monitorSockUrl_,
            std::chrono::seconds(5),
            std::chrono::minutes(5),
            (pid_t) 0) {

    configAppThread_ = std::make_unique<std::thread>([this]() {
      VLOG(1) << "ConfigApp thread starting";
      configApp_.run();
      VLOG(1) << "ConfigApp thread terminating";
    });
    configApp_.waitUntilRunning();
    querySock_ = createAppSock(querySockId_);
  }

  ~ConfigFixture() {
    VLOG(1) << "Stopping the ConfigApp thread";
    configApp_.stop();
    configAppThread_->join();
  }

  std::unique_ptr<std::thread> configAppThread_;
  ConfigApp configApp_;

  fbzmq::Socket<ZMQ_DEALER, fbzmq::ZMQ_CLIENT> querySock_;
  const std::string querySockId_ = "QUERY_SOCK_ID";
  const std::string nodeName_ = "config_app_test_node_1";
};

} // namespace

// Test getting base configurations from controller
TEST_F(ConfigFixture, BaseConfig) {
  SCOPE_EXIT { LOG(INFO) << "ConfigApp test getting base config is done"; };

  string minion;
  string senderApp;
  thrift::Message getBaseConfigMsg{};
  thrift::Message getCtrlConfigBaseRespMsg{};
  getBaseConfigMsg.mType = thrift::MessageType::GET_CTRL_CONFIG_BASE_REQ;
  getBaseConfigMsg.value = fbzmq::util::writeThriftObjStr(
      thrift::GetCtrlConfigBaseReq(), serializer_);

  thrift::GetCtrlConfigBaseResp baseConfig{};

  // Send request to controller
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getBaseConfigMsg,
      serializer_);

  // Receive all base configurations
  std::tie(minion, senderApp, getCtrlConfigBaseRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_BASE_RESP,
      getCtrlConfigBaseRespMsg.mType);
  EXPECT_NO_THROW(
      baseConfig = fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigBaseResp>(
          getCtrlConfigBaseRespMsg.value, serializer_));

  // Convert JSON to dynamic object
  folly::dynamic baseConfigObject = folly::dynamic::object;
  EXPECT_NO_THROW(baseConfigObject = folly::parseJson(baseConfig.config));
  EXPECT_FALSE(baseConfigObject.empty());
}

// Test setting/getting network configuration overrides
TEST_F(ConfigFixture, NetworkOverrides) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test get/set network config overrides is done";
  };

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNetworkOverridesReq setCtrlConfigNetworkOverridesReq{};
  thrift::GetCtrlConfigNetworkOverridesResp getCtrlConfigNetworkOverridesResp{};
  thrift::Message getCtrlConfigNetworkOverridesReqMsg{};
  thrift::Message getCtrlConfigNetworkOverridesRespMsg{};
  thrift::Message setCtrlConfigNetworkOverridesReqMsg{};

  auto overrideValue = std::to_string(std::time(nullptr));
  folly::dynamic networkOverridesObject =
      folly::dynamic::object("testKey", overrideValue);

  setCtrlConfigNetworkOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ;
  setCtrlConfigNetworkOverridesReq = thrift::SetCtrlConfigNetworkOverridesReq();
  setCtrlConfigNetworkOverridesReq.overrides =
      folly::toJson(networkOverridesObject);
  setCtrlConfigNetworkOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNetworkOverridesReq,
      serializer_);

  // Send Network config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNetworkOverridesReqMsg,
      serializer_);

  // Wait for Ack
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNetworkOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_REQ;
  getCtrlConfigNetworkOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      thrift::GetCtrlConfigNetworkOverridesReq(), serializer_);

  // Request Network config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNetworkOverridesReqMsg,
      serializer_);

  // Receive network config overrides
  std::tie(minion, senderApp, getCtrlConfigNetworkOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NETWORK_OVERRIDES_RESP,
      getCtrlConfigNetworkOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigNetworkOverridesResp = fbzmq::util::readThriftObjStr<
          thrift::GetCtrlConfigNetworkOverridesResp>(
          getCtrlConfigNetworkOverridesRespMsg.value, serializer_));

  // Convert JSON to dynamic object
  EXPECT_NO_THROW(
      networkOverridesObject =
          folly::parseJson(getCtrlConfigNetworkOverridesResp.overrides));
  EXPECT_TRUE(
      networkOverridesObject.find("testKey") !=
      networkOverridesObject.items().end());
  EXPECT_EQ(networkOverridesObject["testKey"], overrideValue);

  // Clean up: remove config overrides
  setCtrlConfigNetworkOverridesReq = thrift::SetCtrlConfigNetworkOverridesReq();
  setCtrlConfigNetworkOverridesReq.overrides = "{}";
  setCtrlConfigNetworkOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNetworkOverridesReq, serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNetworkOverridesReqMsg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);
}

// Test setting/getting node configuration overrides
TEST_F(ConfigFixture, NodeOverrides) {
  SCOPE_EXIT {
    LOG(INFO) << "ConfigApp test get/set node config overrides is done";
  };

  // Add a node to global topology
  auto node = createNode(
      nodeName_,
      "01:01:01:01:01:01",
      "test_site",
      true,
      thrift::NodeStatusType::ONLINE,
      thrift::NodeType::DN);
  auto site = createSite("test_site", 1, 1, 1, 1);
  SharedObjects::getTopologyWrapper()->wlock()->setTopology(
      createTopology({node}, {}, {site}));

  string minion;
  string senderApp;
  thrift::SetCtrlConfigNodeOverridesReq setCtrlConfigNodeOverridesReq{};
  thrift::GetCtrlConfigNodeOverridesResp getCtrlConfigNodeOverridesResp{};
  thrift::Message getCtrlConfigNodeOverridesReqMsg{};
  thrift::Message getCtrlConfigNodeOverridesRespMsg{};
  thrift::Message setCtrlConfigNodeOverridesReqMsg{};

  auto overrideValue = std::to_string(std::time(nullptr));
  folly::dynamic nodeOverridesObject = folly::dynamic::object(
      nodeName_, folly::dynamic::object("testKey", overrideValue));

  setCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::SET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  setCtrlConfigNodeOverridesReq = thrift::SetCtrlConfigNodeOverridesReq();
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);

  // Send node config overrides to ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Wait for Ack back
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);

  getCtrlConfigNodeOverridesReqMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_REQ;
  thrift::GetCtrlConfigNodeOverridesReq getCtrlConfigNodeOverridesReq;
  getCtrlConfigNodeOverridesReq.nodes = {nodeName_};
  getCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      getCtrlConfigNodeOverridesReq,
      serializer_);

  // Request node config overrides from ConfigApp
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getCtrlConfigNodeOverridesReqMsg,
      serializer_);

  // Receive node config overrides
  std::tie(minion, senderApp, getCtrlConfigNodeOverridesRespMsg) =
      recvInCtrlApp(querySock_, serializer_);

  EXPECT_EQ(E2EConsts::kConfigAppCtrlId, senderApp);
  EXPECT_EQ(
      thrift::MessageType::GET_CTRL_CONFIG_NODE_OVERRIDES_RESP,
      getCtrlConfigNodeOverridesRespMsg.mType);
  EXPECT_NO_THROW(
      getCtrlConfigNodeOverridesResp =
          fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigNodeOverridesResp>(
              getCtrlConfigNodeOverridesRespMsg.value, serializer_));

  // Convert JSON to dynamic object
  EXPECT_NO_THROW(
      nodeOverridesObject =
          folly::parseJson(getCtrlConfigNodeOverridesResp.overrides));
  EXPECT_TRUE(
      nodeOverridesObject.find(nodeName_) !=
      nodeOverridesObject.items().end());
  EXPECT_TRUE(
      nodeOverridesObject[nodeName_].find("testKey") !=
      nodeOverridesObject[nodeName_].items().end());
  EXPECT_EQ(nodeOverridesObject[nodeName_]["testKey"], overrideValue);

  // Clean up: remove config overrides
  nodeOverridesObject.at(nodeName_) = folly::dynamic::object();
  setCtrlConfigNodeOverridesReq = thrift::SetCtrlConfigNodeOverridesReq();
  setCtrlConfigNodeOverridesReq.overrides = folly::toJson(nodeOverridesObject);
  setCtrlConfigNodeOverridesReqMsg.value = fbzmq::util::writeThriftObjStr(
      setCtrlConfigNodeOverridesReq,
      serializer_);
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      setCtrlConfigNodeOverridesReqMsg,
      serializer_);
  recvE2EAck(querySock_, E2EConsts::kConfigAppCtrlId, true, serializer_);
}

// Validate the default base config with the default config metadata,
// and check for any config parameters with missing metadata.
// Also validate the controller config metadata file.
TEST_F(ConfigFixture, ConfigMetadataValidation) {
  SCOPE_EXIT { LOG(INFO) << "ConfigApp validation with metadata is done"; };

  string minion;
  string senderApp;
  thrift::Message getBaseConfigMsg{};
  thrift::Message getCtrlConfigBaseRespMsg{};
  thrift::Message getConfigMetadataMsg{};
  thrift::Message getConfigMetadataRespMsg{};
  getBaseConfigMsg.mType = thrift::MessageType::GET_CTRL_CONFIG_BASE_REQ;
  getBaseConfigMsg.value = fbzmq::util::writeThriftObjStr(
      thrift::GetCtrlConfigBaseReq(), serializer_);
  getConfigMetadataMsg.mType =
      thrift::MessageType::GET_CTRL_CONFIG_METADATA_REQ;
  getConfigMetadataMsg.value = fbzmq::util::writeThriftObjStr(
      thrift::GetCtrlConfigMetadata(), serializer_);

  // Send base config request to controller
  thrift::GetCtrlConfigBaseResp baseConfig{};
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getBaseConfigMsg,
      serializer_);

  // Receive all base configurations
  std::tie(minion, senderApp, getCtrlConfigBaseRespMsg) =
      recvInCtrlApp(querySock_, serializer_);
  EXPECT_NO_THROW(
      baseConfig = fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigBaseResp>(
          getCtrlConfigBaseRespMsg.value, serializer_));

  // Get latest base config
  folly::dynamic baseConfigObject = folly::dynamic::object;
  EXPECT_NO_THROW(baseConfigObject = folly::parseJson(baseConfig.config));
  EXPECT_FALSE(baseConfigObject.empty());
  std::string latestBaseVersion =
      SharedObjects::getConfigHelper()->rlock()->getLatestBaseVersion();
  EXPECT_FALSE(latestBaseVersion.empty());
  folly::dynamic latestBaseConfig = folly::dynamic::object;
  EXPECT_NO_THROW(latestBaseConfig = baseConfigObject.at(latestBaseVersion));

  // Send config metadata request to controller
  thrift::GetCtrlConfigMetadataResp configMetadataResp{};
  sendInCtrlApp(
      querySock_,
      "",
      E2EConsts::kConfigAppCtrlId,
      querySockId_,
      getConfigMetadataMsg,
      serializer_);

  // Receive config metadata
  std::tie(minion, senderApp, getConfigMetadataRespMsg) =
      recvInCtrlApp(querySock_, serializer_);
  EXPECT_NO_THROW(
      configMetadataResp =
          fbzmq::util::readThriftObjStr<thrift::GetCtrlConfigMetadataResp>(
              getConfigMetadataRespMsg.value, serializer_));

  // Load config metadata
  std::unique_ptr<ConfigMetadata> configMeta;
  EXPECT_NO_THROW(
      configMeta = std::make_unique<ConfigMetadata>(
          folly::parseJson(configMetadataResp.metadata), false, false));

  // Validate config with metadata
  ConfigMetadata::ValidationResult result;
  EXPECT_NO_THROW(result = configMeta->validate(latestBaseConfig));

  // Check for missing metadata (using join() for more detailed print)
  EXPECT_EQ("", folly::join(", ", result.unrecognizedKeys));

  // Validate controller config metadata
  std::unique_ptr<ConfigMetadata> configControllerMeta;
  EXPECT_NO_THROW(
      configControllerMeta = std::make_unique<ConfigMetadata> (
          FLAGS_controller_config_metadata_file, false, false));

  // Validate default controller config with metadata
  std::string contents;
  EXPECT_TRUE(
      folly::readFile(FLAGS_controller_config_default_file.c_str(), contents));

  folly::dynamic cfg = folly::dynamic::object;
  EXPECT_NO_THROW(cfg = folly::parseJson(contents));
  EXPECT_NO_THROW(configControllerMeta->validate(cfg));

  // Validate aggregator config metadata (TODO: move to aggregator test)
  // TODO - hasFlags should be true but this would need to run from aggregator
  // to inject the flags correctly
  std::unique_ptr<ConfigMetadata> configAggregatorMeta;
  EXPECT_NO_THROW(
      configAggregatorMeta = std::make_unique<ConfigMetadata> (
          FLAGS_aggregator_config_metadata_file, false, false));

  // Validate default aggregator config with metadata
  EXPECT_TRUE(
      folly::readFile(FLAGS_aggregator_config_default_file.c_str(), contents));

  EXPECT_NO_THROW(cfg = folly::parseJson(contents));
  EXPECT_NO_THROW(configAggregatorMeta->validate(cfg));
}

TEST_F(ConfigFixture, ConfigSoftwareVersions) {
  // Major release (M21)
  SwVersion M21 = SwVersion(
      "Facebook Terragraph Release RELEASE_M21 "
      "(user@dev12345 Tue Jun 5 16:01:52 PDT 2018");

  // Major/minor release (M20.1)
  SwVersion M20_1 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_1 "
      "(user@dev12345 Tue Apr 24 09:38:31 PDT 2018)");

  // Major/minor release (M20.4)
  SwVersion M20_4 = SwVersion(
      "Facebook Terragraph Release RELEASE_M20_4-user "
      "(user@dev12345 Wed May 23 21:21:04 PDT 2018)");

  // Pre-release (M22-PRE)
  SwVersion M22_PRE = SwVersion(
      "Facebook Terragraph Release RELEASE_M22_PRE1-83-g5be6d6b-user "
      "(user@dev12345 Thu Jun  7 23:10:59 UTC 2018)");

  // Unparsed
  SwVersion unparsedSwVer = SwVersion(" asdf  ");

  // Old Release (before config management)
  SwVersion M5 = SwVersion(
      "Facebook Terragraph Release RELEASE_M5 "
      "(user@dev12345 Thu Jun  7 23:10:59 UTC 2018)");

  {
    auto lockedConfigHelper = SharedObjects::getConfigHelper()->rlock();

    // ---- Base Versions ----
    folly::dynamic baseConfigObjects = folly::dynamic::object;
    baseConfigObjects["RELEASE_M16"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M17"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M18"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M19"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M20"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M20_2"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M21"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M21_1"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M22"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M23"] = folly::dynamic::object;
    baseConfigObjects["RELEASE_M24"] = folly::dynamic::object;

    // Matching: Best base (M21 -> M21)
    EXPECT_EQ(
        "RELEASE_M21",
        *lockedConfigHelper->matchBaseVersion(M21, baseConfigObjects));
    // Matching: Best base (M20.1 -> M20)
    EXPECT_EQ(
        "RELEASE_M20",
        *lockedConfigHelper->matchBaseVersion(M20_1, baseConfigObjects));
    // Matching: Best base (M20.4 -> M20.2)
    EXPECT_EQ(
        "RELEASE_M20_2",
        *lockedConfigHelper->matchBaseVersion(M20_4, baseConfigObjects));
    // Matching: Best base (M22-PRE -> M22)
    EXPECT_EQ(
        "RELEASE_M22",
        *lockedConfigHelper->matchBaseVersion(M22_PRE, baseConfigObjects));
    // Matching: Best base (M5 -> "")
    EXPECT_EQ(
        "",
        *lockedConfigHelper->matchBaseVersion(M5, baseConfigObjects));
    // Matching: None
    EXPECT_FALSE(
        lockedConfigHelper->matchBaseVersion(unparsedSwVer, baseConfigObjects));

    // ---- Firmware Versions ----
    folly::dynamic fwConfigObjects = folly::dynamic::object;
    fwConfigObjects["10.5.0"] = folly::dynamic::object;
    fwConfigObjects["10.6.0"] = folly::dynamic::object;
    fwConfigObjects["10.7.0"] = folly::dynamic::object;
    fwConfigObjects["10.9.0"] = folly::dynamic::object;

    // Matching: Best firmware base (MS9 major/minor -> MS9 major)
    EXPECT_EQ(
        "10.6.0",
        *lockedConfigHelper->matchFirmwareVersion("10.6.0.1", fwConfigObjects));
    // Matching: Best firmware base (MS9[custom] major/minor -> MS9 major)
    EXPECT_EQ(
        "10.6.0",
        *lockedConfigHelper->matchFirmwareVersion("10.6.0.3", fwConfigObjects));
  }
}

int
main(int argc, char* argv[]) {
  folly::init(&argc, &argv);
  FLAGS_logtostderr = true;
  testing::InitGoogleTest(&argc, argv);

  return RUN_ALL_TESTS();
}
