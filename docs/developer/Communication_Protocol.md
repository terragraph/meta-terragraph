# Communication Protocol
This document describes the transport and serialization protocols used across
the Terragraph E2E stack.

## Transport Layer
### About ZeroMQ
Terragraph uses [ZeroMQ] (or ZMQ) for all inter- and intra-process message
passing at the application layer. ZMQ offers a protocol-agnostic abstraction
layer for socket communication, as well as a framework for building lock-free
concurrent applications. E2E specifically uses [fbzmq], a C++ wrapper over
`libzmq`, which provides some helpful abstractions: an async framework with
event loops (`fbzmq::ZmqEventLoop`) and timeouts (`fbzmq::ZmqTimeout`), along
with methods to easily send and receive Thrift objects over sockets
(`fbzmq::Socket`).

Sockets are the core abstraction of ZMQ, and act as an *asynchronous message
queue* rather than a synchronous interface. From a programming perspective, this
means that "sending" a message only enqueues it, with no indication as to
whether or when the message was actually delivered or dropped. Additionally,
ZMQ defines various *socket types* which enable different *messaging patterns*
(e.g. request-reply, publish-subscribe). Unlike conventional sockets,
ZMQ sockets allow *many-to-many* connections depending on the socket type.

Specific behaviors of ZMQ sockets can be configured via a lengthy list of
*socket options*. An important option is the *high watermark*, which is a hard
limit on the size of the message queue; any further messages will either be
dropped or result in blocking, depending on the socket type. This value is
configured separately for outbound (`ZMQ_SNDHWM`) and inbound (`ZMQ_RCVHWM`)
messages, and the actual limit may be "60-70% lower" than the given value.

### Transport Architecture
E2E heavily uses the ZMQ [request-reply pattern], exposing router sockets
(`ZMQ_ROUTER`) externally and using dealer sockets (`ZMQ_DEALER`) internally for
intra-process communication. Both socket types are bidirectional. Router sockets
accept multiple client connections from dealer sockets, each with a unique
*socket identity* (the `ZMQ_IDENTITY` socket option). For outbound messages in
router sockets, the first *message part* must contain the destination's
identity; the receiving socket uses this identity to route the message to the
appropriate client, and will replace the first message part with the sender's
identity.

The controller exposes two external router sockets: an "app socket" and "minion
socket". These sockets are contained within the controller's `Broker`, which
runs as a separate app (i.e. `fbzmq::ZmqEventLoop` thread). Every other
controller app has its own dealer socket, contained within the `CtrlApp` base
class, that connects to the broker's app socket. The app socket also accepts
connections from external clients, such as TG CLI and API service.

Each minion connects to the controller on a dealer socket in the minion's
`Broker`. In addition, the broker has a *local* router socket, which is
otherwise analogous to the controller's app socket: all other minion apps
connect to the local router socket via dealer sockets in the `MinionApp` base
class. The router socket accepts connections from other local clients, which
must connect using a ZMQ ID with prefix `:FWD:` (`kAppSockForwardPrefix`) to
receive replies (or else they will get routed to the controller by default).
The minion also exposes a publish socket (`ZMQ_PUB`) for other clients on which
it broadcasts certain periodic or asynchronous messages (e.g. heartbeats, link
status).

All socket identities of apps are fixed strings (defined in `E2EConsts`). The
socket identity of each minion is its MAC address. External clients connect
using arbitrary, non-conflicting identity strings; these are typically
randomized for sending one-off requests.

<p align="center">
  <img src="../media/figures/architecture.svg" width="650" />
</p>

### Implementation Details
ZMQ socket details are generally abstracted away from E2E apps through the
controller/minion base classes and brokers. When an app's dealer socket receives
a message, it simply passes the message up to a `processMessage()` virtual
function for the app to handle. This function will never be called concurrently.

The controller's app and minion sockets expect multi-part messages, with all
non-final message parts flagged with `ZMQ_SNDMORE`. The first part is the
destination's identity, as required by router sockets. The second part is the
sender's app, and the third is the actual message contents. All message contents
are Thrift structures serialized using the [Thrift compact protocol].

The minion finds the controller's minion socket address (i.e. IP and port)
through reading the `e2e-ctrl-url` key in the Open/R `KvStore`, a process
described in other documents. The minion's broker automatically disconnects and
reconnects from the controller upon a URL change or a timeout.

The controller uses the [ZeroMQ Authentication Protocol] (ZAP), but currently
only for debug purposes to log and associate peer IP addresses with their socket
connections. If enabled, the controller will spawn a thread to receive and
respond to authentication requests via a `ZMQ_REP` socket on
`inproc://zeromq.zap.01`. The app and/or minion sockets will be marked with an
arbitrary, non-empty `ZMQ_ZAP_DOMAIN` socket option, causing ZMQ to forward
connection details to the ZAP handler. The handler simply echoes received peer
IPs into the metadata in its response as the `Ip-Address` property. This
metadata becomes associated with the ZMQ socket, and can be queried whenever
messages arrive.

Each app in both the controller and minion will bump a unique "socketMonitor"
counter once per minute (by default) to indicate that its dealer socket is
healthy and the thread itself is alive. These stats are published via the local
`ZmqMonitor` instance (refer to [Stats, Events, Logs](Stats_Events_Logs.md) for
further details).

### Global Objects
Apart from the intra-process communication architecture, E2E uses a small number
of globally-shared objects across apps. These objects, defined in
`SharedObjects`, are only accessible through acquiring read-write locks using
the [folly::Synchronized] abstraction.

## Serialization Layer
### About Thrift
Terragraph E2E serializes all messages using [Thrift], specifically the
[fbthrift] branch. Thrift includes an interface definition language (IDL) with a
cross-language code generator, as well as a serialization framework for the
generated structures. Terragraph does not use Thrift's RPC framework, in favor
of ZMQ.

### Thrift Interfaces
Thrift structures are defined within `*.thrift` files, located inside various
`if/` directories. All Thrift files used in E2E are listed below.

| File                   | Description                                     |
| ---------------------- | ----------------------------------------------- |
| `Controller.thrift`    | Core structures used by the controller          |
| `Aggregator.thrift`    | Core structures used by the aggregator          |
| `Topology.thrift`      | Topology structures                             |
| `NodeConfig.thrift`    | Node configuration structures                   |
| `FwOptParams.thrift`   | Firmware-specific node configuration structures |
| `Event.thrift`         | Event structures                                |
| `PassThru.thrift`      | Firmware pass-through message structures        |
| `DriverMessage.thrift` | Driver message structures                       |
| `BWAllocation.thrift`  | Bandwidth and airtime allocation structures     |

### Implementation Details
Terragraph exclusively uses the [Thrift compact protocol] for messages
transported over ZMQ. When writing Thrift structures to disk, the JSON
serializer is used instead.

For consistency at the ZMQ layer, the outermost Thrift structure for transport
is always `thrift::Message` (shown below). This structure must include a
message type and compact-serialized binary value, which the receiver can then
deserialize into another Thrift structure.

```c
struct Message {
  1: MessageType mType;
  2: binary value;
  3: optional bool compressed;
  4: optional CompressionFormat compressionFormat;
}
```

Optionally, messages can be compressed using any supported format. The receiver
should first decompress the binary value before deserializing it. Currently,
only a handful of message types are compressed, and only the [Snappy] format is
supported.

## Resources
* [ZeroMQ] - Distributed messaging library
* [fbzmq] - Meta's ZeroMQ wrappers
* [request-reply pattern] - ZeroMQ request-reply pattern specification
* [ZeroMQ Authentication Protocol] - ZeroMQ authentication
* [Thrift compact protocol] - Thrift's "compact" serialization protocol
* [Thrift] - Meta's interface definition language
* [fbthrift] - Meta's branch of Apache Thrift
* [folly::Synchronized] - Meta's C++ lock abstraction
* [Snappy] - High-speed compression/decompression library

[ZeroMQ]: http://zeromq.org/
[fbzmq]: https://github.com/facebook/fbzmq
[request-reply pattern]: http://rfc.zeromq.org/spec:28/REQREP
[ZeroMQ Authentication Protocol]: https://rfc.zeromq.org/spec:27/ZAP/
[Thrift compact protocol]: https://github.com/apache/thrift/blob/master/doc/specs/thrift-compact-protocol.md
[Thrift]: https://thrift.apache.org/
[fbthrift]: https://github.com/facebook/fbthrift
[folly::Synchronized]: https://github.com/facebook/folly/blob/master/folly/docs/Synchronized.md
[Snappy]: https://google.github.io/snappy/
