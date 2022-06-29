/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

namespace cpp2 facebook.terragraph.thrift
namespace py terragraph_thrift.Aggregator
namespace lua terragraph_thrift.Aggregator

cpp_include "<unordered_map>"

enum AggrMessageType {

  // ===  StatusApp  === //
  // Requests handled (by Aggr StatusApp)
  GET_STATUS_DUMP = 101,
  IS_ALIVE = 102,
  // Responses given (by Aggr StatusApp)
  STATUS_DUMP = 201,

  // === NmsPublisher === //
  // Messages originated (by agent)
  STATS_REPORT = 402,
  HIGH_FREQUENCY_STATS_REPORT = 403,

  // === LogTail === //
  // Messages originated (by logtail)
  SYSLOG_REPORT = 451,

  // ===  ConfigApp  === //
  GET_AGGR_CONFIG_REQ = 601,
  GET_AGGR_CONFIG_RESP = 602,
  SET_AGGR_CONFIG_REQ = 603,
  GET_AGGR_CONFIG_METADATA_REQ = 604,
  GET_AGGR_CONFIG_METADATA_RESP = 605,

  // Common
  AGGR_ACK = 1001,
  GET_TOPOLOGY = 1002,
  TOPOLOGY = 1003,
}

#############  StatusApp ##############

/**
 * @apiDefine AggrGetStatusDump
 */
struct AggrGetStatusDump {}

/**
 * @apiDefine AggrStatusDump_SUCCESS
 * @apiSuccess {String} [version]
 *             The aggregator version sourced from "/etc/tgversion"
 */
struct AggrStatusDump {
  // deprecated: 1 (adjacencyMap), 2 (statusReports)
  3: optional string version;
}

#############  StatsApp ##############

struct AggrStat {
  1: string key;
  2: i64 timestamp;
  3: double value;
  4: bool isCounter;
  5: optional string entity;
}

struct AggrStatsReport {
  1: list<AggrStat> stats;
  2: list<string> events;
}

struct AggrSyslog {
  1: i64 timestamp;
  2: string index;
  3: string log;
}

struct AggrSyslogReport {
  1: string macAddr;
  2: list<AggrSyslog> syslogs;
}

struct AggrGetTopology {}

// Used for local serialization only
struct EventLog {
  1: list<string> events; // list of json seralized Event thrift structures
}

############# Config App #############

/**
 * @apiDefine AggrGetConfigReq
 */
struct AggrGetConfigReq {}

/**
 * @apiDefine AggrGetConfigResp_SUCCESS
 * @apiSuccess {String} config The aggregator config (JSON)
 */
struct AggrGetConfigResp {
  1: string config; // Json of aggregator config
}

/**
 * @apiDefine AggrSetConfigReq
 * @apiParam {String} config The aggregator config (JSON)
 */
struct AggrSetConfigReq {
  1: string config; // Json of aggregator config
}

/**
 * @apiDefine AggrGetConfigMetadata
 */
struct AggrGetConfigMetadata {}

/**
 * @apiDefine AggrGetConfigMetadataResp_SUCCESS
 * @apiSuccess {String} metadata The aggregator config parameter metadata (JSON)
 */
struct AggrGetConfigMetadataResp {
  1: string metadata;
}

############# Common #############

struct AggrMessage {
  1: AggrMessageType mType;
  2: binary value;
  3: optional bool compressed;
  4: optional AggrCompressionFormat compressionFormat;
}

enum AggrCompressionFormat {
  SNAPPY = 1,
}

/**
 * @apiDefine AggrAck_SUCCESS
 * @apiSuccess {Boolean} success The response status
 * @apiSuccess {String} message The response message
 */
// Ack to asynchronous requests
struct AggrAck {
  1: bool success;
  2: string message;
}

############# Aggregator Config #############

struct AggrDataEndpoint {
  1: string host;
  2: string statsWriterSuffix;
  3: bool statsEnabled;
  4: string eventsWriterSuffix;
  5: bool eventsEnabled;
  6: string logsWriterSuffix;
  7: bool logsEnabled;
  8: string highFreqStatsWriterSuffix;
  9: bool highFreqStatsEnabled;
}

struct AggregatorConfig {
  1: map<string, string> (cpp.template = "std::unordered_map") flags;
  // Endpoints to push stats, events and/or logging data to
  2: map<string, AggrDataEndpoint> (cpp.template = "std::unordered_map")
      dataEndpoints;
} (no_default_comparators)
