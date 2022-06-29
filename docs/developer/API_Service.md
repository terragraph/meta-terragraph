# API Service
This document describes the REST API service for the E2E stack.

## Overview
Terragraph's `api_service` provides a REST API for the E2E controller and NMS
aggregator. The service translates HTTP/JSON requests into ZMQ/Thrift calls to
the controller or aggregator, then returns the responses to the client
JSON-encoded.

API Service is written in C++ using a [Proxygen] web server, and includes
documentation generated from Javadoc-style annotations in the source code using
[apiDoc].

## Structure
The service exposes three routes:
* `/api/`: A comprehensive REST API for E2E services.
* `/api/stream/`: A push API using server-sent events (SSE).
* `/docs/`: Static API documentation.

The classes containing the request handlers and method definitions are shown in
the table below.

| Route          | Request Handler        | Method Definitions |
| -------------- | ---------------------- | ------------------ |
| `/api/`        | `RequestHandler`       | `ApiClient`        |
| `/api/stream/` | `StreamRequestHandler` | `StreamApiClient`  |
| `/docs/`       | `StaticHandler`        | -                  |

## API Responses
Upon success, every API method returns with status `200 OK` and "Content-Type"
header set to "application/json". A "success" response is sent if API service is
able to translate the request and receive a response from the underlying
service, regardless of whether the actual service endpoint returned a logic
error.

The following HTTP error codes may also be returned:
* `400 Bad Request` - malformed request
* `401 Unauthorized` - missing or malformed authorization header (more details
  below)
* `403 Forbidden` - authorization failure (more details below)
* `503 Service Unavailable` - connection error to the underlying service, or
  request deserialization failure

## Authorization
API Service can perform permission enforcement and sender verification on its
`/api/` route using a public key. If a key is provided, each request must
contain a signed JWT (JSON Web Token) in the "Authorization" HTTP header as
follows:
```
Authorization: Bearer <token>
```

The token payload must contain a "roles" claim. Each role is effectively a
permission, with the format `<prefix><ApiCategory>_<ApiLevel>`. A request is
permitted only if *any* role meets the base permission level required by the
endpoint.

API categories and levels are defined in `Permissions.thrift`. The category
`ALL` is a special case representing all categories. Levels are defined in a
hierarchy, such that a higher enum value implies all lower values (e.g. `WRITE`
implies `READ`).

## Auditing
API Service creates a separate audit log for all "write" requests received. This
log is written to `/data/audit_logs/api_audit.log` (or `--audit_log_path`, if
provided). Each entry is a JSON object containing the endpoint, request body,
and values from the authorization token.

## Documentation
Refer to `src/terragraph-api/README.md` for instructions on building the REST
API documentation from the source files (listed below). The generated HTML files
are checked into the `apidoc/` directory.

### Source Files
apiDoc collects data from the following places:
* The method-level comments (`@api`) in `ApiClient.cpp` and
  `StreamApiClient.cpp`.
* The imported parameter-level comments (`@apiUse`/`@apiDefine`) in all
  referenced `.thrift` files.
* The ordering of methods specified in `apidoc.json`.

### Example
The example below describes the `/api/getNodeConfig` endpoint.

#### `ApiClient.cpp`
```
 1: /**
 2:  * @api {post} /getNodeConfig Get Node Config
 3:  * @apiName GetNodeConfig
 4:  * @apiPermission CONFIG_READ
 5:  * @apiGroup NodeConfiguration
 6:  *
 7:  * @apiDescription Retrieves the full configuration for the given node.
 8:  *
 9:  * @apiUse GetCtrlConfigReq
10:  * @apiExample {curl} Example:
11:  *    curl -id '{"node": "terra111.f5.tb.a404-if", "swVersion": "RELEASE_M21"}' http://localhost:443/api/getNodeConfig
12:  * @apiUse GetCtrlConfigResp_SUCCESS
13:  * @apiSuccessExample {json} Success-Response:
14:  * {
15:  *     "config": "{...}"
16:  * }
17:  */
```

| Line  | Syntax | Description | Notes |
| ----- | ------ | ----------- | ----- |
| 2     | `@api {[method]} /[apiPath] [Method Title]` | Method declaration | `/api` is prepended to all paths |
| 3     | `@apiName [MethodName]` | Method name | Not rendered in HTML |
| 4     | `@apiPermission [PermissionName]` | Permission name | If authorization is enabled, the minimum required permission level (excluding prefix) |
| 5     | `@apiGroup [CategoryName]` | Category name | To render with spaces, declare elsewhere:<br />`@apiDefine [CategoryName] [Name With Spaces]` |
| 7     | `@apiDescription [Long method description.]` | Method description | Allows multi-line strings |
| 9     | `@apiUse [ApiDefineName{_GROUP}]` | Import an `@apiDefine` block by name | Write all *request* fields here |
| 10-11 | `@apiExample {[type]} Example:`<br />&nbsp;&nbsp;&nbsp;&nbsp;`[example command]` | Request example | - |
| 12    | `@apiUse [ApiDefineName_SUCCESS]` | Import an `@apiDefine` block by name | Write all *response* fields here |
| 13-16 | `@apiSuccessExample {[type]} Success-Response:`<br />&nbsp;&nbsp;&nbsp;&nbsp;`[example JSON response]` | Response example | - |

Note that a block imported via `@apiUse` cannot `@apiUse` another block (i.e.
nesting is not supported). All nested request parameters must be written in the
method definition blocks.

#### `Controller.thrift`
```
 1: /**
 2:  * @apiDefine GetCtrlConfigReq
 3:  * @apiParam {String} node The node name
 4:  * @apiParam {String} [swVersion]
 5:  *           The software version to use as the base config.
 6:  *           If this is omitted, the controller will use the last version that
 7:  *           the node reported; if no version is known to the controller, an
 8:  *           error will be returned.
 9:  */
10: struct GetCtrlConfigReq {
11:   1: string node;
12:   2: optional string swVersion;
13: }
14:
15: /**
16:  * @apiDefine GetCtrlConfigResp_SUCCESS
17:  * @apiSuccess {String} config The full node config (JSON)
18:  */
19: struct GetCtrlConfigResp {
20:   1: string config;
21: }
```

| Line   | Syntax | Description | Notes |
| ------ | ------ | ----------- | ----- |
| 2      | `@apiDefine [ThriftName{_GROUP}]` | Block declaration | Use the Thrift struct name (with optional `_GROUP` suffix for nested request parameters) |
| 3, 4-8 | `@apiParam (:[GroupName]) {[DataType]{=[enum]}} [field]{=[default]}`<br />&nbsp;&nbsp;&nbsp;&nbsp;`[Long field description.]` | Request parameter | For nested structs, add `_GROUP` suffix and `(:Group)` tag; for enumerations, fill in list of allowed values (comma-separated); for optional fields, wrap field name in square brackets `[]` |
| 16     | `@apiDefine [ThriftName_SUCCESS]` | Block declaration | Use the Thrift struct name (with mandatory `_SUCCESS` suffix for response parameters) |
| 17     | `@apiSuccess (:[GroupName]) {[DataType]{=[enum]}} [field]`<br />&nbsp;&nbsp;&nbsp;&nbsp;`[Long field description.]` | Response parameter | See `@apiParam` |

Note that using *groups* (not to be confused with `@apiGroup`, which represents
*categories*) is mandatory for nested Thrift structs. Otherwise, apiDoc will
render the parameters for all structs in one combined block.

#### `apidoc.json`
```json
{
  "order": [
    "...",
    "NodeConfiguration",
      "GetNodeConfig",
      "...",
  ]
}
```

The "order" property determines the order of the methods in the generated HTML
files. Use method names as written in `@apiName` and category names as written
in `@apiGroup`.

## Resources
* [Proxygen] - Meta's C++ HTTP libraries
* [apiDoc] - Documentation generator for REST APIs

[Proxygen]: https://github.com/facebook/proxygen
[apiDoc]: http://apidocjs.com/
