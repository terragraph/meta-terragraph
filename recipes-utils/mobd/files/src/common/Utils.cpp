/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Utils.h"
#include "Const.h"
#include "Crypto.h"
#include "Message.pb.h"
#include "Parser.h"

#include <boost/crc.hpp>
#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <limits>
#include <stdio.h>

DECLARE_bool(encryption);

namespace facebook {
namespace terragraph {
namespace mobd {

namespace {
const std::string kWdogDisableCmd{"/etc/init.d/watchdog.sh dis"};
const std::string kWdogEnableCmd{"/etc/init.d/watchdog.sh en"};
#ifdef TG_PLATFORM
const int wdog_default_disable_minutes{3};
const int kOk{0}; // script execution success
const int kErr{-1}; // script error
#endif
}; // namespace

// Wire frame offsets for serialization
enum {
  FRAME1 = 0, // Framing byte #1 'T'
  FRAME2 = 1, // Framing byte #2 'G'
  PAYLOAD_SZ = 2, // Size of the payload
  PAYLOAD = 4, // Payload starts here
  PAD_COUNTER = 4, // Number of padding bytes added by block crypto to the
                   // serialized protobuf Message.
  MESSAGE = 5 // Serialized Message
};

static void
writeBigEndian16(char* buf, uint16_t v) {
  buf[0] = (v >> 8) & 0xff;
  buf[1] = v & 0xff;
}

// Encrypt and serialize a message
// Returns: serialized length or 0 on errors
static int
cryptoSerialize(
    const Message& message, char* wire, char* plain, std::size_t len) {

  // Basic sanity checks.
  if (wire == nullptr || plain == nullptr) {
    LOG(ERROR) << "Invalid buffers";
    return 0;
  }

  // encryption block size
  const std::size_t block_sz = crypto::get_block_size();
  // plain serialized message size
  const std::size_t plain_sz = message.ByteSizeLong();
  // encrypted blocks, we need to round up
  const std::size_t num_blocks = (plain_sz + block_sz - 1) / block_sz;
  // encrypted byte size of the the serialized message
  const std::size_t encrypted_sz = num_blocks * block_sz;
  // total payload field size, +1 for the crypto pad byte counter
  const std::size_t payload_sz = encrypted_sz + 1;
  // size of the entire wire frame
  const std::size_t wire_frame_sz = payload_sz + SERIALIZE_OVERHEAD;

  // Sanity check the buffer sizes
  if (len < wire_frame_sz ||
      payload_sz > std::numeric_limits<uint16_t>::max()) {
    LOG(ERROR) << "Bad buffer sizes";
    return 0; // encrypted message would be too large
  }

  // Write the frame header
  wire[FRAME1] = 'T';
  wire[FRAME2] = 'G';

  // Write the payload size
  writeBigEndian16(&wire[PAYLOAD_SZ], payload_sz);

  // Write the crypto pad byte counter
  wire[PAD_COUNTER] = (uint8_t)(encrypted_sz - plain_sz);

  // Serialize the protobuf message
  if (!message.SerializeToArray(plain, plain_sz)) {
    LOG(ERROR) << "SerializeToArray failed";
    return 0; // unexpected serialization failure
  }

  VLOG(1) << "Plain serial " << plain_sz;
  hexdump(plain, plain_sz);

  // Encrypt the serialized protobuf message directly into the wire buffer
  // Note: we are _not_ encrypting the pad byte counter.
  crypto::encrypt(plain, &wire[MESSAGE], num_blocks);

  // Compute and write the payload crc
  boost::crc_16_type crc;
  crc.process_bytes(&wire[PAYLOAD], payload_sz);
  writeBigEndian16(&wire[PAYLOAD + payload_sz], (uint16_t)crc.checksum());

  VLOG(1) << "Checksum " << crc.checksum();
  return payload_sz + SERIALIZE_OVERHEAD;
}

// Decrypt and deserialize an encrypted payload
// Returns: true on success
static bool
cryptoDeserialize(
    Message& message, const char* payload, char* plain, std::size_t len) {
  if (payload == nullptr || plain == nullptr || len == 0) {
    return 0;
  }
  const std::size_t block_sz = crypto::get_block_size();
  const std::size_t encrypted_message_sz =
      len - 1; // size of the encrypted protobuf Message, -1 for pad counter
  const std::size_t num_pad = (uint8_t)payload[0];
  const std::size_t blocks = encrypted_message_sz / block_sz;
  const std::size_t plain_sz =
      encrypted_message_sz - num_pad; // size of the plain protobuf Message

  VLOG(1) << "deser encr payload " << len;
  hexdump((void*)payload, len);

  // Sanity check the payload size.
  if ((encrypted_message_sz % block_sz) != 0) {
    return false;
  }

  crypto::decrypt(plain, payload + 1, blocks); // +1 skips pad byte counter
  VLOG(1) << "plain deserial " << plain_sz;
  hexdump(plain, plain_sz);

  message.Clear();
  return message.ParseFromArray(plain, plain_sz);
}

// Serialize a message without encrypting it.
// Returns: serialized length or 0 on errors
static int
plainSerialize(const Message& message, char* buf, std::size_t buf_len) {
  std::string payload;
  message.SerializeToString(&payload);
  if (payload.size() > (buf_len - SERIALIZE_OVERHEAD)) {
    return 0;
  }
  buf[FRAME1] = 'T';
  buf[FRAME2] = 'G';
  writeBigEndian16(&buf[PAYLOAD_SZ], payload.size());
  memcpy(&buf[PAYLOAD], payload.data(), payload.size());

  boost::crc_16_type crc;
  crc.process_bytes(payload.data(), payload.size());
  writeBigEndian16(&buf[PAYLOAD + payload.size()], (uint16_t)crc.checksum());
  return payload.size() + SERIALIZE_OVERHEAD;
}

int
serialize(const Message& message, char* wire, char* plain, std::size_t len) {
  return FLAGS_encryption ? cryptoSerialize(message, wire, plain, len)
                          : plainSerialize(message, wire, len);
}

bool
deserialize(
    Message& message, const char* payload, char* plain, std::size_t len) {
  return FLAGS_encryption ? cryptoDeserialize(message, payload, plain, len)
                          : message.ParseFromArray(payload, len);
}

void
hexdump(const void* ptr, int buflen, int bytes_per_line) {
  if (!VLOG_IS_ON(2)) {
    return;
  }
  std::string s;
  char sbuf[1024];
  unsigned char* buf = (unsigned char*)ptr;
  int i, j;
  for (i = 0; i < buflen; i += bytes_per_line) {
    std::snprintf(sbuf, sizeof(sbuf), "%06x: ", i);
    s += sbuf;
    for (j = 0; j < bytes_per_line; j++) {
      if (i + j < buflen)
        std::snprintf(sbuf, sizeof(sbuf), "%02x ", buf[i + j]);
      else
        std::snprintf(sbuf, sizeof(sbuf), "   ");
      s += sbuf;
    }
    s += " ";
    for (j = 0; j < bytes_per_line; j++) {
      if (i + j < buflen) {
        std::snprintf(
            sbuf, sizeof(sbuf), "%c", isprint(buf[i + j]) ? buf[i + j] : '.');
        s += sbuf;
      }
    }
    LOG(INFO) << s;
    s.erase();
  }
}

#ifndef TG_PLATFORM

bool
disableWatchdog(const Message& m) {
  return true;
}
bool
enableWatchdog(const Message& m) {
  return true;
}

#else // #ifndef TG_PLATFORM

bool
disableWatchdog(const Message& m) {
  int minutes = wdog_default_disable_minutes;
  if (m.has_wdog_disable_minutes()) {
    minutes = m.wdog_disable_minutes();
  }
  bool ret = true;
  if (minutes > 0) {
    std::string disableCmd = kWdogDisableCmd + " " + std::to_string(minutes);
    ret = system(disableCmd, kOk);
  }
  return ret;
}

bool
enableWatchdog(const Message& m) {
  bool ret = true;
  if (m.has_wdog_disable_minutes() && m.wdog_disable_minutes() == 0) {
    ret = system(kWdogEnableCmd, kOk);
  }
  return ret;
}

// TODO: Use facebook::terragraph::SysUtils::system()
int
system(const std::string& command) {
  const int status = std::system(command.c_str());

  if (status < 0) {
    LOG(ERROR) << "std::system(" << command << ") failed. status " << status;
    return kErr;
  }

  if (!WIFEXITED(status)) {
    std::stringstream err;
    err << "std::system(" << command << ") abnormal exit.";
    if (WIFSIGNALED(status)) {
      err << " Killed by signal " << WTERMSIG(status);
    }
    LOG(ERROR) << err.str();
    return kErr;
  }

  return WEXITSTATUS(status);
}

// TODO: Use facebook::terragraph::SysUtils::system()
bool
system(const std::string& command, int expectedExitStatus) {

  CHECK(expectedExitStatus >= 0) << "invalid expectedExitStatus";

  const int status = system(command);

  if (status != kErr && status != expectedExitStatus) {
    LOG(ERROR) << "std::system(" << command << ") bad exit status " << status;
  }

  return status == expectedExitStatus;
}

#endif // #ifndef TG_PLATFORM

} // namespace mobd
} // namespace terragraph
} // namespace facebook
