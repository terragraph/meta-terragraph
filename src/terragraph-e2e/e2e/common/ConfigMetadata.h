/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <folly/dynamic.h>
#include <gflags/gflags.h>

#include "e2e/if/gen-cpp2/Controller_types.h"

DECLARE_string(node_config_metadata_file);
DECLARE_string(controller_config_metadata_file);
DECLARE_string(aggregator_config_metadata_file);

namespace facebook {
namespace terragraph {

/**
 * Wrapper class for accessing a Terragraph node configuration metadata file.
 */
class ConfigMetadata {
 public:
  /**
   * Construct ConfigMetadata from a metadata file.
   *
   * @param metadataFile the path to the metadata file
   * @param keepFullMetadata whether to keep configMeta_ in memory after parsing
   *                         (if false, get() will return an empty object)
   * @param hasFlags whether to load flags default values via gflags api into
   *                 the config metadata json (does not load into the config
                     structs)
   * @param validateCfgAction whether to throw exceptions when parsing invalid
   *                          thrift::CfgAction strings
   */
  explicit ConfigMetadata(
      const std::string& metadataFile,
      bool keepFullMetadata,
      bool hasFlags,
      bool validateCfgAction = true);

  /**
   * Construct ConfigMetadata from a metadata object.
   *
   * @param configMeta the metadata object
   * @param keepFullMetadata whether to keep configMeta_ in memory after parsing
   *                         (if false, get() will return an empty object)
   * @param hasFlags whether to load flags default values via gflags api into
   *                 the config metadata json (does not load into the config
                     structs)
   * @param validateCfgAction whether to throw exceptions when parsing invalid
   *                          thrift::CfgAction strings
   */
  explicit ConfigMetadata(
      const folly::dynamic& configMeta,
      bool keepFullMetadata,
      bool hasFlags,
      bool validateCfgAction = true);

  /**
   * Returns the full config metadata.
   *
   * If keepFullMetadata_ is false, this will return an empty object.
   */
  folly::dynamic get();

  /**
   * Returns the actions for the given config, as a map from each action to the
   * associated config keys.
   */
  std::unordered_map<thrift::CfgAction, std::vector<std::string>> getActions(
      const folly::dynamic& config);

  /** Validation details. */
  struct ValidationResult {
    /** List containing all keys without metadata (thus not validated). */
    std::vector<std::string> unrecognizedKeys;

    /** List containing all read-only keys found. */
    std::vector<std::string> readOnlyKeys;

    /** List containing all deprecated keys found. */
    std::vector<std::string> deprecatedKeys;

    /** List containing all non-syncable keys (in controller config, via HA). */
    std::vector<std::string> bstarUnsyncedKeys;
  };

  /**
   * Validates the given config.
   *
   * Returns a ValidationResult struct containing more validation details.
   *
   * Throws various exceptions if validation fails.
   */
  ValidationResult validate(const folly::dynamic& config);

  /**
   * Set flags default values using gflags lookup.
   */
  void setGFlagsDefaultValues(folly::dynamic& configMeta);

  /** Returns a full key from the given parts, delimited by '.' (period). */
  static std::string toFullKey(const std::vector<std::string>& keys);

  /**
   * Returns a vector of key parts from a full key, delimited by '.' (period).
   */
  static std::vector<std::string> splitFullKey(const std::string& fullKey);

  /**
   * Returns a JSON Pointer string from the given full key (delimited by '.').
   */
  static std::string toJsonPointer(const std::string& key);

 private:
  // Forward declarations (since some of these structs reference each other)
  struct CfgIntegerParam;
  struct CfgFloatParam;
  struct CfgStringParam;
  struct CfgBooleanParam;
  struct CfgMapParam;
  struct CfgObjectParam;
  struct CfgIntegerRange;
  struct CfgFloatRange;
  struct CfgObjectValMetadata;

  /** Base type for recursive config parameters. */
  struct CfgRecursiveParam {
    /** Constructor. */
    explicit CfgRecursiveParam(
        const folly::dynamic& val, bool validateCfgAction = true);
    /** Data type. */
    thrift::CfgParamType type;
    /** \{ **/
    std::unique_ptr<CfgIntegerParam> intVal;
    std::unique_ptr<CfgFloatParam> floatVal;
    std::unique_ptr<CfgStringParam> strVal;
    std::unique_ptr<CfgBooleanParam> boolVal;
    std::unique_ptr<CfgObjectParam> objVal;
    std::unique_ptr<CfgMapParam> mapVal;
    /** \} **/
  };

  /** Integer-type config parameter validation. */
  struct CfgIntegerParam {
    /** Constructor. */
    explicit CfgIntegerParam(const folly::dynamic& val);
    /** Allowed value ranges. */
    std::unique_ptr<std::vector<CfgIntegerRange>> allowedRanges;
    /** Allowed values, in addition to allowedRanges. */
    std::unique_ptr<std::unordered_set<int64_t>> allowedValues;
  };

  /** Float-type config parameter validation. */
  struct CfgFloatParam {
    /** Constructor. */
    explicit CfgFloatParam(const folly::dynamic& val);
    /** Allowed value ranges. */
    std::unique_ptr<std::vector<CfgFloatRange>> allowedRanges;
    /** Allowed values, in addition to allowedRanges. */
    std::unique_ptr<std::unordered_set<double>> allowedValues;
  };

  /** Boolean-type config parameter validation. */
  struct CfgBooleanParam {
    /** Constructor. */
    explicit CfgBooleanParam(const folly::dynamic& val);
  };

  /** String-type config parameter validation. */
  struct CfgStringParam {
    /** Constructor. */
    explicit CfgStringParam(const folly::dynamic& val);
    /** Regular expression constraints. */
    std::unique_ptr<std::string> regexMatches;
    /** Allowed integer value ranges (for stringified integers). */
    std::unique_ptr<std::vector<CfgIntegerRange>> intRanges;
    /** Allowed floating-point value ranges (for stringified floats). */
    std::unique_ptr<std::vector<CfgFloatRange>> floatRanges;
    /** Allowed values, in addition to regexMatches. */
    std::unique_ptr<std::unordered_set<std::string>> allowedValues;
  };

  /** Map-type config parameter validation. */
  struct CfgMapParam : CfgRecursiveParam {
    /** Constructor. */
    explicit CfgMapParam(const folly::dynamic& val);
  };

  /** Object-type config parameter validation. */
  struct CfgObjectParam {
    /** Constructor. */
    explicit CfgObjectParam(
        const folly::dynamic& val, bool validateCfgAction = true);
    /** Map of object properties. */
    std::unordered_map<std::string, std::unique_ptr<CfgObjectValMetadata>>
        properties;
  };

  /** Integer range. */
  struct CfgIntegerRange {
    /** Constructor. */
    explicit CfgIntegerRange(const folly::dynamic& val);
    /** Minimum allowed value. */
    int64_t min;
    /** Maximum allowed value (inclusive). */
    int64_t max;
  };

  /** Float range. */
  struct CfgFloatRange {
    /** Constructor. */
    explicit CfgFloatRange(const folly::dynamic& val);
    /** Minimum allowed value. */
    double min;
    /** Maximum allowed value (inclusive). */
    double max;
  };

  /** Metadata associated with each config parameter within an object param. */
  struct CfgObjectValMetadata : CfgRecursiveParam {
    /** Constructor. */
    explicit CfgObjectValMetadata(
        const folly::dynamic& val, bool validateCfgAction = true);
    /** Config description. */
    std::string desc;
    /** Config action in addition to base action (in CfgParamMetadata). */
    std::unique_ptr<thrift::CfgAction> action;
    /** Required property, default false. */
    bool required;
    /** Read-only property (only allow GET), default false. */
    bool readOnly;
    /** Deprecated property (only allow GET), default false. */
    bool deprecated;
    /** Sync with BinaryStar peer (controller config only), default false. */
    bool sync;
    /** Optional tag string */
    std::unique_ptr<std::string> tag;
  };

  /**
   * Metadata associated with each config parameter.
   *
   * NOTE: This is the root struct for each parameter.
   */
  struct CfgParamMetadata : CfgRecursiveParam {
    /** Constructor. */
    explicit CfgParamMetadata(
        const folly::dynamic& val, bool validateCfgAction);
    /** Config description. */
    std::string desc;
    /** Config action. */
    thrift::CfgAction action;
    /** Read-only parameter (only allow GET), default false. */
    bool readOnly;
    /** Deprecated parameter (only allow GET), default false. */
    bool deprecated;
    /** Sync with BinaryStar peer (controller config only), default false. */
    bool sync;
    /** Optional tag string. */
    std::unique_ptr<std::string> tag;
  };

  /**
   * Record the actions mapped to the given config recursively.
   * @param config The config structure at the current key.
   * @param actions The actions map to return.
   * @param keys The current stack of partial keys (normally returns
                 unmodified).
   * @param metaMap The metadata structure at the current key.
   */
  void getActions(
      const folly::dynamic& config,
      std::unordered_map<thrift::CfgAction, std::vector<std::string>>& actions,
      std::vector<std::string>& keys,
      folly::dynamic& metaMap);

  /**
   * Record the actions mapped to the given parameter recursively.
   *
   * This only adds actions tied to object properties.
   */
  void getParamActions(
      const folly::dynamic& param,
      const CfgRecursiveParam& paramMeta,
      std::unordered_map<thrift::CfgAction, std::vector<std::string>>& actions,
      std::vector<std::string>& keys);

  /**
   * Validate the given config recursively.
   * @param config The config structure at the current key.
   * @param keys The current stack of partial keys (normally returns
   *             unmodified).
   * @param metaMap The metadata structure at the current key.
   * @param result The validation details.
   */
  void validate(
      const folly::dynamic& config,
      std::vector<std::string>& keys,
      folly::dynamic& metaMap,
      ValidationResult& result);

  /** Validate the given parameter recursively using the provided metadata. */
  void validateParam(
      const folly::dynamic& param,
      const CfgRecursiveParam& paramMeta,
      std::vector<std::string>& keys,
      ValidationResult& result);

  /** Load and parse the config metadata. */
  void loadConfigMetadata(const folly::dynamic& configMeta);

  /**
   * Preprocess the config metadata recursively.
   *
   * This only expands the copy-block macros (in place).
   *
   * Throws various exceptions if macro expansion fails.
   *
   * @param configMeta The original, full config metadata structure
   *                   (unmodified).
   * @param preprocessedObj The structure at the current key to be preprocessed.
   * @param keys The current stack of partial keys (normally returns
   *             unmodified).
   * @param copyBlockExpansions The current stack of expanded copy-block macros
   *                            (normally returns unmodified).
   */
  void preprocessConfigMetadata(
      const folly::dynamic& configMeta,
      folly::dynamic& preprocessedObj,
      std::vector<std::string>& keys,
      std::vector<std::string>& copyBlockExpansions);

  /**
   * Expand the given config metadata object's copy-block macro, if it exists
   * in the top-level element. This expansion is non-recursive.
   *
   * Returns true if an expansion was performed, or false otherwise.
   *
   * Throws various exceptions if macro expansion fails.
   *
   * @param configMeta The original, full config metadata structure
   *                   (unmodified).
   * @param obj The structure at the current key to be macro-expanded.
   * @param keys The current stack of partial keys.
   * @param copyBlockExpansions The current stack of expanded copy-block macros.
   */
  bool preprocessCopyBlock(
      const folly::dynamic& configMeta,
      folly::dynamic& obj,
      const std::vector<std::string>& keys,
      std::vector<std::string>& copyBlockExpansions);

  /**
   * Parse the metadata recursively.
   *
   * Throws various exceptions if the metadata is malformed.
   */
  void parseConfigMetadata(const folly::dynamic& obj, folly::dynamic& indexMap);

  /**
   * If false, the original metadata file won't be stored and get() calls will
   * fail.
   */
  bool keepFullMetadata_;

  /** Whether this metadata contains gflags. */
  bool hasFlags_;

  /**
   * Whether to throw exceptions when parsing invalid thrift::CfgAction strings.
   */
  bool validateCfgAction_;

  /** Config metadata object. */
  folly::dynamic configMeta_ = folly::dynamic::object;

  /**
   * Index map from config keys into configMetaArray_
   * (holding CfgParamMetadata objects).
   */
  folly::dynamic configMetaIndexMap_ = folly::dynamic::object;

  /** Vector holding all CfgParamMetadata elements. */
  std::vector<CfgParamMetadata> configMetaArray_{};
};

} // namespace terragraph
} // namespace facebook
