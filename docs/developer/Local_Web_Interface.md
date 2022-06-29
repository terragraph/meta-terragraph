# Local Web Interface
This document describes the implementation of the local administrative web
interface.

## Overview
Terragraph provides a proof-of-concept web portal that is accessible over Wi-Fi,
with the primary purpose of enabling easy initial setup. This currently supports
status monitoring, configuration editing, assoc/dissoc actions, topology scans,
and more.

The web portal is disabled by default. To access the web portal, the following
steps are required:
1. Enable the web server in node config (`envParams.WEBUI_ENABLED`).
2. Connect to the ESP32 Wi-Fi network (PSK: `facebook`).
3. Visit `192.168.5.1` in a web browser.

There are two components involved: a web server (`webui`) and UI (`tg95`), both
described in sections below. For more details on the Wi-Fi architecture, refer
to [Wi-Fi](WiFi.md).

## Web Server
The local HTTP server (`webui`) is written in C++ using [µWebSockets], chosen
for its small footprint. The server contains three types of endpoints:
1. REST APIs for management
2. Websocket endpoints for streaming stats
3. Static file serving for the UI (`tg95`)

`webui` is installed via `recipes-utils/webui/webui_0.1.bb`. Sources are located
in `recipes-utils/webui/files/src/webui/`, with routes defined in `WebUI.cpp`
and implemented in `WebRoute.cpp`.

## UI
The experimental "Terragraph95" UI (`tg95`) is written in [React] and
bootstrapped with [Create React App]. To accommodate slow UART speeds, bundle
size is reduced by substituting React with [Preact], code splitting, etc.

`tg95` is installed via `recipes-utils/webui/tg95_0.1.bb`. Sources are located
in `src/tg95/`. An optimized build is checked in to `src/tg95/build/`, which
gets installed to `/etc/webui/tg95.tar.gz` and is extracted by `webui` in its
`sv` startup script.

## Resources
* [µWebSockets] - C/C++ web server
* [React] - Meta's JavaScript UI library
* [Create React App] - Meta's React app toolchain
* [Preact] - Small, API-compatible React alternative

[µWebSockets]: https://github.com/uNetworking/uWebSockets
[React]: https://reactjs.org/
[Create React App]: https://create-react-app.dev/
[Preact]: https://preactjs.com/
