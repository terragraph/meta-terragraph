/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "IgnitionApp.h"

#include <fbzmq/service/logging/LogSample.h>
#include <fbzmq/zmq/Zmq.h>
#include <folly/MapUtil.h>
#include <folly/Memory.h>
#include <gflags/gflags.h>

#include "IgnitionAppUtil.h"
#include "SharedObjects.h"
#include "algorithms/ControlSuperframeHelper.h"
#include "algorithms/GolayHelper.h"
#include "algorithms/PolarityHelper.h"
#include "e2e/common/Consts.h"
#include "e2e/common/MacUtils.h"

using namespace fbzmq;
using apache::thrift::detail::TEnumMapFactory;

using std::string;

DEFINE_bool(
    enable_linkup_slot_exclusion,
    false,
    "Whether to enable the Slot Exclusion feature during initial beamforming");
DEFINE_int32(
    linkup_exclusion_distance_short,
    50,
    "The short distance threshold (in meters) used to determine initial "
    "beamforming exclusion set");
DEFINE_int32(
    linkup_exclusion_distance_long,
    350,
    "The long distance threshold (in meters) used to determine initial "
    "beamforming exclusion set");
DEFINE_int32(
    linkup_exclusion_angle_rel,
    5,
    "The relative link angle (in degrees) used to determine initial "
    "beamforming exclusion set");

namespace facebook {
namespace terragraph {

IgnitionApp::IgnitionApp(
    fbzmq::Context& zmqContext,
    const std::string& routerSockUrl,
    const std::string& monitorSockUrl,
    std::chrono::seconds extendedDampenInterval,
    std::chrono::seconds extendedDampenFailureInterval,
    std::chrono::seconds backupCnLinkInterval,
    bool ignoreDampenIntervalAfterResp)
    : CtrlApp(
          zmqContext,
          routerSockUrl,
          monitorSockUrl,
          E2EConsts::kIgnitionAppCtrlId),
      extendedDampenInterval_(extendedDampenInterval),
      extendedDampenFailureInterval_(extendedDampenFailureInterval),
      backupCnLinkInterval_(backupCnLinkInterval),
      ignoreDampenIntervalAfterResp_{ignoreDampenIntervalAfterResp} {
  // Read and set ignition param overrides from config
  const auto ignitionParams = SharedObjects::getE2EConfigWrapper()
                                  ->rlock()
                                  ->getConfig()
                                  ->ignitionParams;

  if (ignitionParams.enable_ref().has_value()) {
    autoIgnitionEnabled_ = ignitionParams.enable_ref().value();
  }
  if (ignitionParams.linkUpInterval_ref().has_value()) {
    linkUpInterval_ =
        std::chrono::seconds(ignitionParams.linkUpInterval_ref().value());
  }
  if (ignitionParams.linkUpDampenInterval_ref().has_value()) {
    dampenInterval_ =
        std::chrono::seconds(ignitionParams.linkUpDampenInterval_ref().value());
  }
  if (ignitionParams.linkAutoIgnite_ref().has_value()) {
    for (const auto& linkAutoIgniteIt :
        ignitionParams.linkAutoIgnite_ref().value()) {
      if (!linkAutoIgniteIt.second) {
        linkAutoIgniteOff_.insert(linkAutoIgniteIt.first);
      }
    }
  }
  if (ignitionParams.bfTimeoutSec_ref().has_value()) {
    bfTimeout_ =
        std::chrono::seconds(ignitionParams.bfTimeoutSec_ref().value());
  }

  linkupTimeout_ =
      ZmqTimeout::make(this, [this]() noexcept { linkUpTimeoutExpired(); });
  linkupTimeout_->scheduleTimeout(linkUpInterval_, true /* isPeriodic */);
}

thrift::IgnitionParams
IgnitionApp::getIgnitionParams() const {
  std::unordered_map<string, bool> linksAutoIgniteState;
  for (const auto& linkname : linkAutoIgniteOff_) {
    linksAutoIgniteState[linkname] = false;
  }

  thrift::IgnitionParams ignitionParams;
  ignitionParams.enable_ref() = autoIgnitionEnabled_;
  ignitionParams.linkUpInterval_ref() = linkUpInterval_.count();
  ignitionParams.linkUpDampenInterval_ref() = dampenInterval_.count();
  ignitionParams.linkAutoIgnite_ref() = std::move(linksAutoIgniteState);
  ignitionParams.bfTimeoutSec_ref() = bfTimeout_.count();
  return ignitionParams;
}

void
IgnitionApp::processMessage(
    const std::string& minion,
    const std::string& senderApp,
    const thrift::Message& message) noexcept {
  switch (message.mType) {
    case thrift::MessageType::SET_LINK_STATUS_REQ:
      processSetLinkStatusReq(minion, senderApp, message);
      break;
    case thrift::MessageType::FORCE_DISSOC_REQ:
      processForceDissocReq(senderApp, message);
      break;
    case thrift::MessageType::GET_IGNITION_STATE:
      processGetIgnitionState(minion, senderApp, message);
      break;
    case thrift::MessageType::SET_IGNITION_PARAMS:
      processSetIgnitionParams(minion, senderApp, message);
      break;
    case thrift::MessageType::LINK_STATUS_EVENT:
      processLinkStatusEvent(senderApp, message);
      break;
    default:
      LOG(ERROR)
          << "Wrong type of message ("
          << folly::get_default(
                 TEnumMapFactory<thrift::MessageType>::makeValuesToNamesMap(),
                 message.mType, "UNKNOWN")
          << ") received from " << minion << ":" << senderApp;
  }
}

void
IgnitionApp::processSetLinkStatusReq(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  LOG(INFO) << "Received SetLinkStatusReq from " << minion << ":" << senderApp;
  auto setLinkStatusReq = maybeReadThrift<thrift::SetLinkStatusReq>(message);
  if (!setLinkStatusReq) {
    handleInvalidMessage("SetLinkStatusReq", senderApp, minion);
    return;
  }

  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  // validate link existence
  auto linkName = lockedTopologyW->getLinkName(
      setLinkStatusReq->initiatorNodeName, setLinkStatusReq->responderNodeName);
  if (!linkName) {
    LOG(ERROR) << "Received SetLinkStatusReq for unrecognized link "
               << setLinkStatusReq->initiatorNodeName << " -> "
               << setLinkStatusReq->responderNodeName;
    return sendE2EAck(senderApp, false, "Unrecognized link.");
  }
  auto link = lockedTopologyW->getLink(*linkName);
  if (!link) {  // shouldn't happen...
    return sendE2EAck(
        senderApp, false, "Unrecognized link (please try again).");
  }

  auto initiatorNode =
      lockedTopologyW->getNode(setLinkStatusReq->initiatorNodeName);
  auto responderNode =
      lockedTopologyW->getNode(setLinkStatusReq->responderNodeName);

  // validate initiator node type
  if (initiatorNode->node_type != thrift::NodeType::DN) {
    LOG(ERROR) << "Received SetLinkStatusReq for an initiator node ("
               << setLinkStatusReq->initiatorNodeName << ") which is not a DN";
    return sendE2EAck(senderApp, false, "Initiator node is not a DN.");
  }

  // validate initiator node mac address
  if (initiatorNode->mac_addr.empty()) {
    LOG(ERROR) << "Received SetLinkStatusReq with initiator node that does not "
               << "have a valid MAC address: " << initiatorNode->name;
    return sendE2EAck(
        senderApp, false, "No valid MAC address found for initiator node.");
  }

  // validate mac addresses on link
  if (link->a_node_mac.empty() || link->z_node_mac.empty()) {
    LOG(ERROR) << "Received SetLinkStatusReq for link without MAC addresses "
                  "set on both ends: " << link->name;
    return sendE2EAck(
        senderApp, false, "No MAC address found on both ends of the link.");
  }

  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  thrift::IgnitionCandidate igCandidate;
  igCandidate.initiatorNodeName = setLinkStatusReq->initiatorNodeName;
  igCandidate.linkName = *linkName;

  switch (setLinkStatusReq->action) {
    case thrift::LinkActionType::LINK_UP:
      processLinkUpReq(
          igCandidate,
          link.value(),
          initiatorNode.value(),
          responderNode.value(),
          senderApp);
      break;
    case thrift::LinkActionType::LINK_DOWN:
      processLinkDownReq(igCandidate, link.value(), senderApp);
      break;
    case thrift::LinkActionType::LINK_ADD:
    case thrift::LinkActionType::LINK_DELETE:
      return sendE2EAck(senderApp, false, "Unsupported action.");
  }
}

void
IgnitionApp::processForceDissocReq(
    const string& senderApp, const thrift::Message& message) {
  LOG(INFO) << "Received ForceDissocReq from " << senderApp;
  auto req = maybeReadThrift<thrift::ForceDissocReq>(message);
  if (!req) {
    handleInvalidMessage("ForceDissocReq", senderApp);
    return;
  }

  // Validate MAC address format
  std::string initiatorMac;
  std::string responderMac;
  try {
    initiatorMac = MacUtils::standardizeMac(req->initiatorMac);
  } catch (const std::invalid_argument& ex) {
    sendE2EAck(
        senderApp, false, "Invalid initiator MAC format: " + req->initiatorMac);
    return;
  }
  try {
    responderMac = MacUtils::standardizeMac(req->responderMac);
  } catch (const std::invalid_argument& ex) {
    sendE2EAck(
        senderApp, false, "Invalid responder MAC format: " + req->responderMac);
    return;
  }

  // Validate initiator (must exist in topology)
  auto maybeInitiatorNode = SharedObjects::getTopologyWrapper()->rlock()
      ->getNodeByMac(initiatorMac);
  if (!maybeInitiatorNode) {
    sendE2EAck(
        senderApp, false, "Initiator MAC does not exist: " + req->initiatorMac);
    return;
  }

  // Send request
  thrift::SetLinkStatus setLinkStatus;
  setLinkStatus.linkStatusType = thrift::LinkStatusType::LINK_DOWN;
  setLinkStatus.initiatorMac = initiatorMac;
  setLinkStatus.responderMac = responderMac;
  sendToMinionApp(
      maybeInitiatorNode->mac_addr,
      E2EConsts::kIgnitionAppMinionId,
      thrift::MessageType::SET_LINK_STATUS,
      setLinkStatus);
  sendE2EAck(senderApp, true, "Force dissoc command was sent.");

  // Log event
  bumpCounter(maybeInitiatorNode->mac_addr + ".forceDissocReq.sent");
  eventClient_->logEventThrift(
      thrift::EventCategory::IGNITION,
      thrift::EventId::FORCE_DISSOC,
      thrift::EventLevel::INFO,
      folly::sformat(
          "Sending force dissoc from {} to {}", initiatorMac, responderMac),
      setLinkStatus,
      std::nullopt,
      std::make_optional(maybeInitiatorNode->mac_addr),
      std::make_optional(maybeInitiatorNode->name));
}

void
IgnitionApp::processSetIgnitionParams(
    const string& minion,
    const string& senderApp,
    const thrift::Message& message) {
  LOG(INFO) << "Received SET_IGNITION_PARAMS from " << minion << ":"
            << senderApp;
  auto ignitionParams = maybeReadThrift<thrift::IgnitionParams>(message);
  if (!ignitionParams) {
    handleInvalidMessage("IgnitionParams", senderApp, minion);
    return;
  }

  if (ignitionParams->linkUpInterval_ref().has_value()) {
    // reset link up timeout
    linkUpInterval_ =
        std::chrono::seconds(ignitionParams->linkUpInterval_ref().value());
    LOG(INFO) << "Setting ignition frequency to " << linkUpInterval_.count()
              << " seconds";
    linkupTimeout_->scheduleTimeout(linkUpInterval_, true /* isPeriodic */);
  }

  if (ignitionParams->linkUpDampenInterval_ref().has_value()) {
    dampenInterval_ =
        std::chrono::seconds(
            ignitionParams->linkUpDampenInterval_ref().value());
    LOG(INFO) << "Setting ignition dampen interval to "
              << dampenInterval_.count() << " seconds";
  }

  if (ignitionParams->enable_ref().has_value()) {
    if (ignitionParams->enable_ref().value()) {
      if (!autoIgnitionEnabled_) {
        LOG(INFO) << "Turning network-wide auto ignition ON";
        autoIgnitionEnabled_ = true;
      }
    } else {
      if (autoIgnitionEnabled_) {
        LOG(INFO) << "Turning network-wide auto ignition OFF";
        autoIgnitionEnabled_ = false;
      }
    }

    // Clear per-link auto ignition table
    linkAutoIgniteOff_.clear();

    // Clear per-link ignition attempts
    linkToAttemptTs_.clear();
    linkToInitialAttemptTs_.clear();
    cnToPossibleIgnitionTs_.clear();
    initiatorToAttemptTs_.clear();
  }

  if (ignitionParams->linkAutoIgnite_ref().has_value()) {
    if (autoIgnitionEnabled_) {
      for (const auto& kv : ignitionParams->linkAutoIgnite_ref().value()) {
        bool linkAutoIgniteChanged;
        if (kv.second) {
          linkAutoIgniteChanged = linkAutoIgniteOff_.erase(kv.first);
        } else {
          linkAutoIgniteChanged = linkAutoIgniteOff_.insert(kv.first).second;
        }

        LOG(INFO) << "Auto ignition for link " << kv.first
                  << (kv.second ? " is ON" : " is OFF");

        // Clear per-link ignition attempts
        if (linkAutoIgniteChanged) {
          linkToAttemptTs_.erase(kv.first);
          linkToInitialAttemptTs_.erase(kv.first);
        }
      }
    } else {
      sendE2EAck(senderApp, false,
          "Network-wide auto ignition is OFF. "
          "Per-link ignition is not effective");
      return;
    }
  }

  if (ignitionParams->bfTimeoutSec_ref().has_value()) {
    bfTimeout_ =
        std::chrono::seconds(ignitionParams->bfTimeoutSec_ref().value());
    LOG(INFO) << "Setting BF timeout to " << bfTimeout_.count() << " seconds";
  }

  // Modify controller config. This could get us in an inconsistent state if
  // the runtime config is successfully changed, but we fail to save to the
  // config file
  auto lockedE2EConfigWrapper = SharedObjects::getE2EConfigWrapper()->wlock();
  thrift::ControllerConfig config = *lockedE2EConfigWrapper->getConfig();
  config.ignitionParams = getIgnitionParams();
  bool success = lockedE2EConfigWrapper->setConfigFromThrift(config);
  lockedE2EConfigWrapper.unlock();  // lockedE2EConfigWrapper -> NULL

  if (success) {
    sendE2EAck(senderApp, true, "Ignition params set.");
  } else {
    sendE2EAck(senderApp, false, "Failed to set ignition params.");
  }
}

void
IgnitionApp::processGetIgnitionState(
    const string& minion,
    const string& senderApp,
    const thrift::Message& /* message */) {
  VLOG(4) << "Received getIgnitionState message from " << minion << ":"
          << senderApp;

  // get all reachable live nodes and non-alive links hanging off of the
  // reachable nodes
  std::vector<thrift::IgnitionCandidate> igCandidates;
  {
    auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();
    std::unordered_map<std::string, size_t> linkIterationIndex;  // unused here
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        cnToPossibleIgnitionTs(cnToPossibleIgnitionTs_);  // create a copy
    std::unordered_map<std::string, std::chrono::steady_clock::time_point>
        initiatorToAttemptTs(initiatorToAttemptTs_); // create a copy
    igCandidates = IgnitionAppUtil::findAllIgnitionCandidates(
        *lockedTopologyW,
        cnToPossibleIgnitionTs,
        initiatorToAttemptTs,
        bfTimeout_,
        backupCnLinkInterval_,
        linkIterationIndex,
        linkAutoIgniteOff_);
  }

  thrift::IgnitionState ignitionState;
  ignitionState.igCandidates = igCandidates;
  ignitionState.lastIgCandidates = lastIgCandidates_;
  ignitionState.igParams = getIgnitionParams();
  sendToCtrlApp(senderApp, thrift::MessageType::IGNITION_STATE, ignitionState);
}

void
IgnitionApp::processLinkStatusEvent(
    const string& senderApp, const thrift::Message& message) {
  auto linkStatusEvent = maybeReadThrift<thrift::LinkStatusEvent>(message);
  if (!linkStatusEvent) {
    handleInvalidMessage("LinkStatusEvent", senderApp);
    return;
  }

  // We received a response, so the reporting minion can now receive another
  // ignition command. Clear the controller's timeout too.
  initiatorToAttemptTs_.erase(linkStatusEvent->nodeName);

  // Erase the "initial ignition attempt" time when the link comes up.
  if (linkStatusEvent->linkStatusType == thrift::LinkStatusType::LINK_UP) {
    linkToInitialAttemptTs_.erase(linkStatusEvent->linkName);
  }

  // The "last ignition time" is being used mainly as a timeout (and secondarily
  // to dampen ignition commands). Minion only reports a link status at the end
  // of an ignition procedure regardless of success/failure. If configured,
  // reset the timeout UNLESS ignition has been unsuccessful for an extended
  // period of time.
  auto now = std::chrono::steady_clock::now();
  auto iter = linkToInitialAttemptTs_.find(linkStatusEvent->linkName);
  if (iter == linkToInitialAttemptTs_.end() ||
      (ignoreDampenIntervalAfterResp_ &&
       now - iter->second < extendedDampenFailureInterval_)) {
    linkToAttemptTs_.erase(linkStatusEvent->linkName);
  }
}

void
IgnitionApp::linkUpTimeoutExpired() {
  LOCK_TOPOLOGY_AND_CONFIG(
    lockedTopologyW, rlock, lockedConfigHelper, rlock);

  // Find all ignition candidates that can be done in parallel
  auto igCandidates = IgnitionAppUtil::findAllParallelIgnitionCandidates(
      *lockedTopologyW,
      linkToAttemptTs_,
      linkToInitialAttemptTs_,
      cnToPossibleIgnitionTs_,
      initiatorToAttemptTs_,
      bfTimeout_,
      dampenInterval_,
      extendedDampenInterval_,
      extendedDampenFailureInterval_,
      backupCnLinkInterval_,
      linkupIterationIndex_,
      autoIgnitionEnabled_ ? linkAutoIgniteOff_
                           : std::unordered_set<std::string>());

  // If auto-ignition is disabled, we only check for link status
  if (!autoIgnitionEnabled_) {
    for (const auto& igCandidate : igCandidates) {
      auto link = lockedTopologyW->getLink(igCandidate.linkName);
      if (!link) {
        continue;
      }

      auto aNode = lockedTopologyW->getNode(link->a_node_name);
      auto zNode = lockedTopologyW->getNode(link->z_node_name);
      if (!aNode->mac_addr.empty() &&
          aNode->status != thrift::NodeStatusType::OFFLINE) {
        thrift::GetLinkStatus getLinkStatus;
        getLinkStatus.responderMac = link->z_node_mac;
        sendToMinionApp(
            aNode->mac_addr,
            E2EConsts::kIgnitionAppMinionId,
            thrift::MessageType::GET_LINK_STATUS,
            getLinkStatus);
      } else if (!zNode->mac_addr.empty() &&
                 zNode->status != thrift::NodeStatusType::OFFLINE) {
        thrift::GetLinkStatus getLinkStatus;
        getLinkStatus.responderMac = link->a_node_mac;
        sendToMinionApp(
            zNode->mac_addr,
            E2EConsts::kIgnitionAppMinionId,
            thrift::MessageType::GET_LINK_STATUS,
            getLinkStatus);
      }
    }

    // Clear per-link ignition attempts (since we aren't actually igniting)
    linkToAttemptTs_.clear();
    linkToInitialAttemptTs_.clear();
    cnToPossibleIgnitionTs_.clear();
    initiatorToAttemptTs_.clear();

    return;
  }

  lastIgCandidates_ = igCandidates;

  // Clear initial ignition attempt times
  cleanUpInitialLinkUpAttempts(*lockedTopologyW);
  cleanUpCnLinkUpAttempts(*lockedTopologyW);

  // Remove nodes which are responders in this ignition attempt from
  // the list of nodes which should stop being responders
  std::unordered_set<
      std::pair<std::string /*node name*/, std::string /*mac*/>> newResponders;
  for (const auto& igCandidate : igCandidates) {
    auto link = lockedTopologyW->getLink(igCandidate.linkName);
    if (link) {
      std::pair<std::string, std::string> respNameAndMac;
      if (igCandidate.initiatorNodeName == link->a_node_name) {
        respNameAndMac = std::make_pair(link->z_node_name, link->z_node_mac);
      } else {
        respNameAndMac = std::make_pair(link->a_node_name, link->a_node_mac);
      }
      newResponders.insert(respNameAndMac);
    }
  }

  // Tell new responders to become responders
  for (const auto& pair : newResponders) {
    std::string nodeName = pair.first;
    std::string radioMac = pair.second;

    std::optional<thrift::Node> node = lockedTopologyW->getNode(nodeName);
    if (!node) {
      LOG(ERROR) << "Unrecognized node name (" << nodeName
                 << ") among new responders";
      continue;
    }
    if (node->mac_addr.empty() ||
        node->status == thrift::NodeStatusType::OFFLINE) {
      continue;
    }
    if (node->node_type == thrift::NodeType::CN) {
      // CNs can only form one link, and will already be in responder mode if
      // the link is not yet ignited
      continue;
    }

    // Send BF_RESP_SCAN to node
    VLOG(3) << "Sending BF_RESP_SCAN to " << node->name << " (" << radioMac
            << ")";
    thrift::BfRespScan bfRespScan;
    bfRespScan.radioMac = radioMac;
    bfRespScan.secondsToDisable = linkUpInterval_.count();
    sendToMinionApp(
        node->mac_addr,
        E2EConsts::kIgnitionAppMinionId,
        thrift::MessageType::BF_RESP_SCAN,
        bfRespScan);
  }

  // Send slot exclusion command to nearby nodes
  std::unordered_set<std::string> exclusionSet;
  if (FLAGS_enable_linkup_slot_exclusion) {
    exclusionSet = IgnitionAppUtil::getExclusionSet(
        *lockedTopologyW,
        *lockedConfigHelper,
        igCandidates,
        FLAGS_linkup_exclusion_distance_short,
        FLAGS_linkup_exclusion_distance_long,
        FLAGS_linkup_exclusion_angle_rel);
  }
  for (const string& radioMac : exclusionSet) {
    std::optional<thrift::Node> node = lockedTopologyW->getNodeByMac(radioMac);
    if (!node || node->mac_addr.empty()) {
      continue;
    }

    VLOG(3) << "Sending BF_SLOT_EXCLUSION_REQ to node: "
            << node->name << " (" << radioMac << ")";
    thrift::BfSlotExclusionReq bfSlotExclusionReq;
    bfSlotExclusionReq.radioMac = radioMac;
    bfSlotExclusionReq.startBwgdIdx = 0;
    sendToMinionApp(
        node->mac_addr,
        E2EConsts::kIgnitionAppMinionId,
        thrift::MessageType::BF_SLOT_EXCLUSION_REQ,
        bfSlotExclusionReq);
  }

  lockedTopologyW.unlock();  // lockedTopologyW -> NULL
  lockedConfigHelper.unlock();  // lockedConfigHelper -> NULL

  // Give responder nodes a small headstart to get into responder mode before
  // asking initiator nodes to bring link up
  scheduleTimeout(
      std::chrono::seconds(1), [this, igCandidates = std::move(igCandidates)] {
        // Send link up requests to initiators
        for (const auto& igCandidate : igCandidates) {
          sendSetLinkStatus(
              igCandidate, thrift::LinkStatusType::LINK_UP, getId());
        }
      });
}

void
IgnitionApp::processLinkUpReq(
    const thrift::IgnitionCandidate& igCandidate,
    const thrift::Link& link,
    const thrift::Node& initiatorNode,
    const thrift::Node& responderNode,
    const string& senderApp) {
  // check that the link is currently down
  if (link.is_alive) {
    LOG(ERROR) << "Received SetLinkStatusReq(LINK_UP) for link that is already "
                  "alive: " << link.name;
    sendE2EAck(senderApp, false, "Link is already alive.");
    return;
  }

  // check that the initiator is properly time-synced (i.e. GPS is enabled)
  if (initiatorNode.status != thrift::NodeStatusType::ONLINE_INITIATOR) {
    std::string initiatorStatus = folly::get_default(
        TEnumMapFactory<thrift::NodeStatusType>::makeValuesToNamesMap(),
        initiatorNode.status,
        "UNKNOWN");

    LOG(INFO) << "Received SetLinkStatusReq(LINK_UP) for unqualified initiator "
                 "node " << igCandidate.initiatorNodeName << ", current state "
              << initiatorStatus;
    sendE2EAck(
        senderApp,
        false,
        folly::sformat(
            "Initiator node is not qualified, current state: {}.",
            initiatorStatus));
    return;
  }

  // prevent igniting multiple links to the same CN
  if (responderNode.node_type == thrift::NodeType::CN) {
    auto cnLinks = SharedObjects::getTopologyWrapper()->rlock()
        ->getLinksByNodeName(responderNode.name);
    for (const auto& cnLink : cnLinks) {
      if (cnLink.link_type != thrift::LinkType::WIRELESS) {
        continue;
      }
      if (cnLink.is_alive) {
        LOG(ERROR) << "Received SetLinkStatusReq(LINK_UP) to responder CN "
                   << responderNode.name << " which has another link that is "
                      "already alive: " << cnLink.name;
        sendE2EAck(
            senderApp, false, "Cannot ignite multiple links to same CN.");
        return;
      }
    }
  }

  // send assoc request
  sendSetLinkStatus(igCandidate, thrift::LinkStatusType::LINK_UP, senderApp);
  sendE2EAck(
      senderApp,
      true,
      "SetLinkStatus(LINK_UP) dispatched to initiator node.");
}

void
IgnitionApp::processLinkDownReq(
    const thrift::IgnitionCandidate& igCandidate,
    const thrift::Link& link,
    const string& senderApp) {
  // check that the link is currently up
  if (!link.is_alive) {
    LOG(ERROR) << "Received SetLinkStatusReq(LINK_DOWN) for link that is "
                  "already down: " << link.name;
    sendE2EAck(senderApp, false, "Link is already down.");
    return;
  }

  // send dissoc request
  sendSetLinkStatus(igCandidate, thrift::LinkStatusType::LINK_DOWN, senderApp);
  sendE2EAck(
      senderApp,
      true,
      "SetLinkStatus(LINK_DOWN) dispatched to initiator node.");
}

void
IgnitionApp::sendSetLinkStatus(
    const thrift::IgnitionCandidate& igCandidate,
    const thrift::LinkStatusType& linkStatusType,
    const std::string& source) {
  // get initiator and responder node
  auto lockedTopologyW = SharedObjects::getTopologyWrapper()->rlock();

  auto initiatorNode = lockedTopologyW->getNode(igCandidate.initiatorNodeName);
  if (!initiatorNode) {
    LOG(ERROR) << "Unrecognized node name (" << igCandidate.initiatorNodeName
               << ") for initiator node";
    return;
  }

  auto responderNode = lockedTopologyW->getNbrNode(
      igCandidate.initiatorNodeName, igCandidate.linkName);
  if (!responderNode) {
    LOG(ERROR) << "Unrecognized link name (" << igCandidate.linkName << ") for "
               << igCandidate.initiatorNodeName;
    return;
  }

  auto link = lockedTopologyW->getLink(igCandidate.linkName);

  lockedTopologyW.unlock();  // lockedTopologyW -> NULL

  // get initiator/responder macs (needed for multi-radio DNs)
  std::string initiatorMac = link->a_node_mac;
  std::string responderMac = link->z_node_mac;
  if (link->z_node_name == igCandidate.initiatorNodeName) {
    std::swap(initiatorMac, responderMac);
  }

  // fill out SetLinkStatus
  thrift::SetLinkStatus setLinkStatus;
  setLinkStatus.linkStatusType = linkStatusType;
  setLinkStatus.responderMac = responderMac;
  setLinkStatus.initiatorMac = initiatorMac;
  // fill out fields for LINK_UP request
  if (linkStatusType == thrift::LinkStatusType::LINK_UP) {
    // Validate link configuration
    LOCK_TOPOLOGY_AND_CONFIG(
        lockedTopologyW, rlock, lockedConfigHelper, wlock);
    if (!lockedConfigHelper->validateLinkConfig(
            *lockedTopologyW, link.value(), *eventClient_)) {
      std::string errMsg = folly::sformat(
          "Unable to ignite `{}` due to configuration error.", link->name);
      LOG(ERROR) << errMsg;
      eventClient_->logEventDynamic(
          thrift::EventCategory::IGNITION,
          thrift::EventId::SET_LINK_STATUS,
          thrift::EventLevel::ERROR,
          errMsg,
          folly::dynamic::object("source", source),
          std::make_optional(igCandidate.linkName),
          std::make_optional(initiatorNode->mac_addr),
          std::make_optional(igCandidate.initiatorNodeName));
      return;
    }

    auto initiatorPolarity = lockedConfigHelper->getRadioPolarity(
        initiatorNode->name, initiatorMac, false);
    auto responderPolarity = lockedConfigHelper->getRadioPolarity(
        responderNode->name, responderMac, false);

    auto initiatorChannel = lockedConfigHelper->getRadioChannel(
        initiatorNode->name, initiatorMac, false, false);
    auto responderChannel = lockedConfigHelper->getRadioChannel(
        responderNode->name, responderMac, false, false);

    auto initiatorLinkConfig = lockedConfigHelper->getLinkTopologyConfig(
        initiatorNode->name, responderMac);
    lockedConfigHelper.unlock(); // lockedConfigHelper -> NULL
    lockedTopologyW.unlock(); // lockedTopologyW -> NULL

    auto polarityTypeMap =
        TEnumMapFactory<thrift::PolarityType>::makeValuesToNamesMap();
    VLOG(3) << folly::sformat(
        "LINK_UP attempt of link {} with: Polarities {}/{}, GolayIdx {}/{}, "
        "ControlSuperframe {}, Channel {}/{}",
        igCandidate.linkName,
        initiatorPolarity ? folly::get_default(
                                polarityTypeMap,
                                initiatorPolarity.value(),
                                "UNKNOWN")
                          : "EMPTY",
        responderPolarity ? folly::get_default(
                                polarityTypeMap,
                                responderPolarity.value(),
                                "UNKNOWN")
                          : "EMPTY",
        initiatorLinkConfig.golayIdx
            ? std::to_string(initiatorLinkConfig.golayIdx->txGolayIdx)
            : "EMPTY",
        initiatorLinkConfig.golayIdx
            ? std::to_string(initiatorLinkConfig.golayIdx->rxGolayIdx)
            : "EMPTY",
        initiatorLinkConfig.controlSuperframe
            ? std::to_string(initiatorLinkConfig.controlSuperframe.value())
            : "EMPTY",
        initiatorChannel ? std::to_string(initiatorChannel.value()) : "EMPTY",
        responderChannel ? std::to_string(responderChannel.value()) : "EMPTY");

    // Send responder node parameters to initiator. Initator will foward these
    // params to the responder.
    setLinkStatus.responderNodeType_ref() = responderNode->node_type;
    if (initiatorLinkConfig.golayIdx.has_value()) {
      setLinkStatus.golayIdx_ref() = initiatorLinkConfig.golayIdx.value();
    }
    if (initiatorLinkConfig.controlSuperframe.has_value()) {
      setLinkStatus.controlSuperframe_ref() =
          initiatorLinkConfig.controlSuperframe.value();
    }
    if (responderPolarity.has_value()) {
      setLinkStatus.responderNodePolarity_ref() = responderPolarity.value();
    }
  }

  std::string linkStatusTypeStr = folly::get_default(
      TEnumMapFactory<thrift::LinkStatusType>::makeValuesToNamesMap(),
      linkStatusType, "UNKNOWN");
  LOG(INFO) << folly::sformat(
      "Sending {} to {} for {} using responder MAC {}",
      linkStatusTypeStr,
      igCandidate.initiatorNodeName,
      initiatorMac,
      responderMac);

  bumpCounter(initiatorNode->mac_addr + ".setLinkStatus.sent");
  eventClient_->logEventDynamic(
      thrift::EventCategory::IGNITION,
      thrift::EventId::SET_LINK_STATUS,
      thrift::EventLevel::INFO,
      folly::sformat(
          "Sending {} to {}", linkStatusTypeStr, igCandidate.linkName),
      folly::dynamic::object("source", source),
      std::make_optional(igCandidate.linkName),
      std::make_optional(initiatorNode->mac_addr),
      std::make_optional(igCandidate.initiatorNodeName));

  // send request
  sendToMinionApp(
      initiatorNode->mac_addr,
      E2EConsts::kIgnitionAppMinionId,
      thrift::MessageType::SET_LINK_STATUS,
      setLinkStatus);

  // request to bump linkup attempts in topologyApp
  if (linkStatusType == thrift::LinkStatusType::LINK_UP) {
    thrift::BumpLinkUpAttempts bumpLinkUpAttempts;
    bumpLinkUpAttempts.linkName = igCandidate.linkName;
    sendToCtrlApp(
        E2EConsts::kTopologyAppCtrlId,
        thrift::MessageType::BUMP_LINKUP_ATTEMPTS,
        bumpLinkUpAttempts);
  }
}

void
IgnitionApp::cleanUpInitialLinkUpAttempts(const TopologyWrapper& topologyW) {
  for (auto it = linkToInitialAttemptTs_.begin();
       it != linkToInitialAttemptTs_.end();) {
    auto link = topologyW.getLink(it->first);
    if (!link) {
      it = linkToInitialAttemptTs_.erase(it);
      continue;  // shouldn't happen
    }

    auto aNode = topologyW.getNode(link->a_node_name);
    auto zNode = topologyW.getNode(link->z_node_name);
    if (!aNode || !zNode ||
        (aNode->status == thrift::NodeStatusType::OFFLINE &&
         zNode->status == thrift::NodeStatusType::OFFLINE)) {
      it = linkToInitialAttemptTs_.erase(it);
      continue;
    }

    it++;
  }
}

void
IgnitionApp::cleanUpCnLinkUpAttempts(const TopologyWrapper& topologyW) {
  for (auto it = cnToPossibleIgnitionTs_.begin();
       it != cnToPossibleIgnitionTs_.end();) {
    bool shouldErase = true;
    for (const auto& link : topologyW.getLinksByNodeName(it->first)) {
      if (link.link_type == thrift::LinkType::ETHERNET) {
        continue;  // shouldn't happen, but would break this logic
      }
      if (link.is_alive) {
        break;  // a link is alive, so erase the entry
      }

      auto nbrNode = topologyW.getNbrNode(it->first, link);
      if (!nbrNode || nbrNode->node_type != thrift::NodeType::DN) {
        break;  // shouldn't happen
      }
      if (nbrNode->status == thrift::NodeStatusType::ONLINE_INITIATOR) {
        shouldErase = false;
        break;  // a valid initiator still exists, so keep the entry
      }
    }

    if (shouldErase) {
      it = cnToPossibleIgnitionTs_.erase(it);
    } else {
      it++;
    }
  }
}

} // namespace terragraph
} // namespace facebook
