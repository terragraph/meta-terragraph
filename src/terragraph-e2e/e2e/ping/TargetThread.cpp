/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "TargetThread.h"

#include <arpa/inet.h>
#include <netdb.h>

#include <chrono>
#include <cstring>
#include <exception>
#include <string>
#include <thread>
#include <utility>

#include <folly/Format.h>
#include <folly/GLog.h>
#include <folly/Memory.h>
#include <folly/portability/GFlags.h>

DEFINE_int32(
    net_socket_buffer_size, 4000000, "The UDP socket receive buffer size");
DEFINE_int32(
    err_sleep_ms,
    100,
    "Sleep time (in ms) upon error while receiving a probe, or 0 to disable");

namespace facebook {
namespace terragraph {

using namespace facebook::terragraph;

using std::string;

/**
 * Utility function for translating client address to IP/port pair in human-
 * readable form.
 *
 * @param probe Pointer to probe containing client's socket address.
 *
 * @throw runtime_error if translation fails.
 */
static string
getIpPortStr(const Probe* probe) {
  char host[NI_MAXHOST], port[NI_MAXSERV];
  if (int res = getnameinfo(
          (struct sockaddr*)&probe->clientAddr,
          probe->clientAddrLen,
          host,
          sizeof(host),
          port,
          sizeof(port),
          NI_NUMERICHOST | NI_NUMERICSERV)) {
    throw std::runtime_error(folly::sformat(
        "getnameinfo() failed to get IP/port: {}", gai_strerror(res)));
  }
  return folly::sformat("{}:{}", host, port);
}

/*
 * Binds a socket to host/port. Since socket option IPV6_V6ONLY is false by
 * default, an IPv6 socket can handle IPv4 probes as well on a dual-stack host.
 */
int
initUdpServer(const string& host, int port, std::optional<int32_t> timeout_m) {
  int sockFd;

  if ((sockFd = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
    LOG(ERROR) << "socket() failed: " << std::strerror(errno);
    return kSockFdInvalid;
  }

  VLOG(1) << "socket() success";

  // Permit multiple AF_INET6 sockets to be bound to an identical socket address
  int one = 1;
  if (setsockopt(sockFd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
    LOG(WARNING) << "setsockopt() SO_REUSEPORT failed: "
                 << std::strerror(errno);
  }

  // Generate a timestamp for each packet coming in with nanosecond resolution
  if (setsockopt(sockFd, SOL_SOCKET, SO_TIMESTAMPNS, &one, sizeof(one)) < 0) {
    LOG(ERROR) << "setsockopt() SO_TIMESTAMPNS failed: "
               << std::strerror(errno);
    close(sockFd);
    return kSockFdInvalid;
  }

  // Set a read timeout
  if (timeout_m) {
    struct timeval tv;
    tv.tv_sec = *timeout_m * 60;
    tv.tv_usec = 0;
    if (setsockopt(
            sockFd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv) < 0) {
      LOG(ERROR) << "setsockopt() SO_RCVTIMEO failed: " << std::strerror(errno);
      close(sockFd);
      return kSockFdInvalid;
    }
  }

  // Set the maximum socket receive buffer size to handle many pings
  int32_t buffSize = FLAGS_net_socket_buffer_size;
  if (setsockopt(sockFd, SOL_SOCKET, SO_RCVBUF, &buffSize, sizeof(buffSize)) <
      0) {
    LOG(WARNING) << "setsockopt() SO_RCVBUF failed: " << std::strerror(errno);
  }

  struct sockaddr_in6 sa;
  memset(&sa, 0, sizeof(sa));
  sa.sin6_family = AF_INET6;
  sa.sin6_port = htons(port);

  switch (inet_pton(AF_INET6, host.c_str(), &(sa.sin6_addr))) {
    case 1:
      VLOG(1) << "inet_pton() success";
      break;
    case 0:
      LOG(ERROR) << "inet_pton() invalid input: " << host;
      close(sockFd);
      return kSockFdInvalid;
    default:
      LOG(ERROR) << "inet_pton() conversion error";
      close(sockFd);
      return kSockFdInvalid;
  }

  if (bind(sockFd, (struct sockaddr*)&sa, sizeof(struct sockaddr_in6)) < 0) {
    LOG(ERROR) << "bind() failed: " << std::strerror(errno);
    close(sockFd);
    return kSockFdInvalid;
  }

  VLOG(1) << "bind() success";
  return sockFd;
}

TargetReceiverThread::TargetReceiverThread(
    int sockFd,
    std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue)
    : sockFd_(sockFd), probeQueue_(probeQueue) {}

bool
TargetReceiverThread::receiveProbe(Probe* probe) {
  struct msghdr msg;
  struct iovec entry;

  // control data buffer
  char cbuf[kProbeDataLen];

  memset(&msg, 0, sizeof(msg));

  msg.msg_iov = &entry;
  msg.msg_iovlen = 1;

  entry.iov_base = probe->data;
  entry.iov_len = kProbeDataLen;

  msg.msg_control = cbuf;
  msg.msg_controllen = sizeof(cbuf);

  // Prepare to receive either v4 or v6 addresses
  ::memset(&probe->clientAddr, 0, sizeof(probe->clientAddr));
  probe->clientAddrLen = sizeof(probe->clientAddr);

  msg.msg_name = &probe->clientAddr;
  msg.msg_namelen = sizeof(probe->clientAddr);

  // This is a blocking call
  int recvLen = ::recvmsg(sockFd_, &msg, 0);
  if (recvLen == 0) {
    throw std::runtime_error("recvmsg() returned 0 (unexpected)");
  }
  if (recvLen == -1) {
    if (errno == EWOULDBLOCK || errno == EAGAIN) {
      LOG(ERROR) << "recvmsg() timed out: " << std::strerror(errno);
      return false;
    }

    throw std::runtime_error(
        folly::sformat("recvmsg() failed: {}", std::strerror(errno)));
  }
  if (recvLen < kProbeDataLen) {
    LOG(ERROR) << "Received " << recvLen << " bytes expected " << kProbeDataLen;
    throw std::runtime_error("recvmsg() truncated probe (unexpected)");
  }

  struct cmsghdr* cmsg{nullptr};
  struct timespec* stamp{nullptr};

  for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
    switch (cmsg->cmsg_level) {
      case SOL_SOCKET:
        switch (cmsg->cmsg_type) {
          case SO_TIMESTAMPNS: {
            stamp = (struct timespec*)CMSG_DATA(cmsg);
            break;
          }
        }
        break;
    }
  }

  // Kernel returned the timestamp
  if (stamp) {
    probe->probeBody.targetRcvdTime =
        htonl(stamp->tv_sec * 1000000 + stamp->tv_nsec / 1000);
  } else {
    FB_LOG_EVERY_MS(INFO, 1000) << "Kernel timestamp not available";

    // Use system time to approximate
    probe->probeBody.targetRcvdTime =
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
  }

  return true;
}

void
TargetReceiverThread::enqueueProbe(std::unique_ptr<Probe>&& probe) {
  probeQueue_->blockingWrite(std::move(probe));
}

void
TargetReceiverThread::run() {
  while (1) {
    auto probe = std::make_unique<Probe>();
    try {
      if (!receiveProbe(probe.get())) {
        break;
      }
    } catch (const std::runtime_error& e) {
      string clientIpPort;
      try {
        clientIpPort = getIpPortStr(probe.get());
      } catch (const std::runtime_error& e) {
        clientIpPort = "unknown client";
      }
      LOG(ERROR) << "receiveProbe() from " << clientIpPort
                 << " failed: " << folly::exceptionStr(e);
      if (FLAGS_err_sleep_ms > 0) {
        /* sleep override */ std::this_thread::sleep_for(
            std::chrono::milliseconds(FLAGS_err_sleep_ms));
      }
      continue;
    }
    enqueueProbe(std::move(probe));
  }

  LOG(INFO) << "Finished run()";
}

TargetSenderThread::TargetSenderThread(
    int sockFd,
    std::shared_ptr<folly::MPMCQueue<std::unique_ptr<Probe>>> probeQueue)
    : sockFd_(sockFd), probeQueue_(probeQueue) {}

std::optional<std::unique_ptr<Probe>>
TargetSenderThread::dequeueProbe(std::optional<int32_t> timeout_m) {
  std::unique_ptr<Probe> probe;
  if (!timeout_m) {
    probeQueue_->blockingRead(probe);
  } else if (!probeQueue_->tryReadUntil(
                 std::chrono::steady_clock::now() +
                     std::chrono::minutes(*timeout_m),
                 probe)) {
    LOG(ERROR) << "dequeueProbe() timed out after waiting " << *timeout_m
               << " minutes";
    return std::nullopt;
  }

  return probe;
}

void
TargetSenderThread::echoProbe(Probe* probe) {
  uint32_t now = std::chrono::duration_cast<std::chrono::microseconds>(
                     std::chrono::system_clock::now().time_since_epoch())
                     .count();

  probe->probeBody.targetRespTime = htonl(now);

  FB_LOG_EVERY_MS(INFO, 1000) << folly::sformat(
      "Probe originated at {}, received at {} responded at {}, adjusted by {}",
      ntohl(probe->probeBody.pingerSentTime),
      ntohl(probe->probeBody.targetRcvdTime),
      ntohl(probe->probeBody.targetRespTime),
      ntohl(probe->probeBody.targetRespTime) -
          ntohl(probe->probeBody.targetRcvdTime));

  // prepare the message for sending
  struct msghdr msg;
  struct cmsghdr* cmsg;
  struct iovec entry;

  // control data buffer, hardcoded - we only store tclass there
  char cbuf[256];

  memset(&msg, 0, sizeof(msg));
  memset(cbuf, 0, sizeof(cbuf));
  cmsg = (struct cmsghdr*)cbuf;

  msg.msg_iov = &entry;
  msg.msg_iovlen = 1;

  entry.iov_base = probe->data;
  entry.iov_len = kProbeDataLen;

  // Set the ancilliary data (tclass in this case)
  int tclass = probe->probeBody.tclass;
  msg.msg_control = cmsg;
  msg.msg_controllen = CMSG_SPACE(sizeof(tclass));

  cmsg->cmsg_len = CMSG_LEN(sizeof(tclass));
  cmsg->cmsg_level = IPPROTO_IPV6;
  cmsg->cmsg_type = IPV6_TCLASS;

  ::memcpy(CMSG_DATA(cmsg), &tclass, sizeof(tclass));

  msg.msg_name = &probe->clientAddr;
  msg.msg_namelen = sizeof(probe->clientAddr);

  // this is a blocking call
  int sendLen = sendmsg(sockFd_, &msg, 0);

  if (sendLen == -1) {
    throw std::runtime_error(
        folly::sformat("sendmsg() error: {}", std::strerror(errno)));
  }
  if (sendLen < kProbeDataLen) {
    throw std::runtime_error(
        "sendto() didn't send entire datagram (unexpected)");
  }
}

void
TargetSenderThread::run(std::optional<int32_t> timeout_m) {
  while (1) {
    auto probe = dequeueProbe(timeout_m);
    if (!probe) {
      break;
    }

    try {
      echoProbe(probe->get());
    } catch (const std::runtime_error& e) {
      string clientIpPort;
      try {
        clientIpPort = getIpPortStr(probe->get());
      } catch (const std::runtime_error& e) {
        clientIpPort = "unknown client";
      }
      LOG(ERROR) << "echoProbe() to " << clientIpPort
                 << " failed: " << folly::exceptionStr(e);
      continue;
    }
  }

  shutdown(sockFd_, SHUT_RDWR);
  LOG(INFO) << "Finished run()";
}

} // namespace terragraph
} // namespace facebook
