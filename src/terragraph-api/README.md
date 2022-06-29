# Terragraph E2E/NMS HTTP API Service

## About
This module implements a REST API service which connects to the E2E controller
and NMS aggregator. The service translates HTTP/JSON requests into ZMQ/Thrift
calls to the controller or aggregator, then returns the responses to the client
JSON-encoded.

The API service runs on a proxygen web server with three routes:
* `/api/`: All API calls are handled by `RequestHandler`, using methods defined
  in `ApiClient::MethodMap`.
* `/api/stream/`: All streaming API calls are handled by `StreamRequestHandler`,
  using streams and events defined in `StreamApiClient::StreamEventMap` and
  `StreamApiClient::EventFunctionMap`.
* `/docs/`: The static documentation is served through `StaticHandler`.

## API Documentation
The REST API documentation is generated from Javadoc-style annotations in the
source code using [apidoc](http://apidocjs.com/). The order of the methods in
the generated webpage can be specified in `apidoc.json`.

apidoc can be installed using [npm](https://www.npmjs.com/get-npm):
```bash
$ npm install -g apidoc@"0.27.1"
```

The command below will read all filtered (`-f`) files in the input
directory (`-i`) and output the generated HTML (`-o`) using the given
configuration file (`-c`).
```bash
$ cd /path/to/meta-terragraph  # change to project root directory
$ apidoc \
  -i ./src/ \
  -c ./src/terragraph-api/ \
  -o ./src/terragraph-api/apidoc/ \
  -f 'ApiClient.cpp' \
  -f 'StreamApiClient.cpp' \
  -f 'Controller.thrift' \
  -f 'Topology.thrift' \
  -f 'BWAllocation.thrift' \
  -f 'Aggregator.thrift'
```
