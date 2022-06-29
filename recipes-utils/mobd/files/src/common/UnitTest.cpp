/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UnitTest.h"
#include "Const.h"
#include "Crypto.h"
#include "Message.pb.h"
#include "MessageHandler.h"
#include "Parser.h"
#include "Utils.h"
#include <cstdlib>
#include <ctype.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>

DECLARE_bool(encryption);

namespace facebook {
namespace terragraph {
namespace mobd {

static bool
protoBufSanityTest() {
  char buf[256];
  const int cookie{12345};
  const std::string error_message{"Oops"};
  Message hello;
  hello.set_message_type(Message::HELLO);
  hello.set_cookie(cookie);
  hello.set_error_message(error_message);
  size_t sz = hello.ByteSizeLong();
  Message m;
  const bool ret =
      (hello.SerializeToArray(buf, sz) && m.ParseFromArray(buf, sz) &&
       m.has_message_type() && m.message_type() == Message::HELLO &&
       m.has_cookie() && m.cookie() == cookie && m.has_error_message() &&
       m.error_message() == error_message);
  LOG(INFO) << "protoBufSanityTest " << (ret ? "passed " : "failed");
  return ret;
}

static bool
cryptoTest() {
  const std::string keygen_phrase = "Hello World!";
  const char* text =
      "We hold these truths to be self-evident, that all men are created "
      "equal, that they are endowed by their Creator";
  const int block_size = crypto::get_block_size();
  const size_t blocks = (strlen(text) + block_size - 1) / block_size;
  const size_t bytes = blocks * block_size;
  char plain[512];
  char enc[512];
  memset(plain, 0x0, sizeof(plain));
  memset(enc, 0x0, sizeof(enc));
  memcpy(plain, text, strlen(text) + 1); // lint does not like strcpy here
  hexdump(plain, bytes, 16);

  crypto::init(keygen_phrase);
  crypto::encrypt(plain, enc, blocks);
  hexdump(enc, bytes, 16);

  memset(plain, 0x0, sizeof(plain));
  crypto::decrypt(plain, enc, blocks);
  hexdump(plain, bytes, 16);

  const bool ret = (strcmp(text, plain) == 0);
  LOG(INFO) << "cryptoTest " << (ret ? "passed " : "failed");
  return ret;
}

static bool
serializeTest(std::size_t buf_size, std::size_t* len = nullptr) {
  crypto::init(DEFAULT_KEYGEN_PHRASE);
  std::unique_ptr<char[]> buf(new char[buf_size]);
  std::unique_ptr<char[]> plain(new char[buf_size]);
  Message hello;
  hello.set_message_type(Message::HELLO);
  hello.set_cookie(1234);
  hello.set_error_message("Oops");
  const int helloLen = serialize(hello, buf.get(), plain.get(), buf_size);
  LOG(INFO) << "serialize len " << helloLen;
  if (len != nullptr) {
    *len = helloLen;
  }
  hexdump(buf.get(), helloLen);
  Parser parser(256);
  for (int i = 0; i < helloLen; i++) {
    const Message* m = parser.parse(buf[i]);
    if (m != nullptr && m->has_message_type() &&
        m->message_type() == hello.message_type() && m->has_cookie() &&
        m->cookie() == hello.cookie() && m->has_error_message() &&
        m->error_message() == hello.error_message()) {
      return true;
    }
  }
  return false;
}

static bool
adequateBufSizeSerializeTest() {
  bool ret = serializeTest(256);
  LOG(INFO) << "adequateBufSizeSerializeTest " << (ret ? "passed" : "failed");
  return ret;
}

static bool
inadequateBufSizeSerializeTest() {
  std::size_t min_buf_length;
  serializeTest(256, &min_buf_length);
  bool ret = !serializeTest(min_buf_length - 1);
  LOG(INFO) << "inadequateBufSizeSerializeTest " << (ret ? "passed" : "failed");
  return ret;
}

/*
Do the first two steps of the bluetooth security handshake:
  1. client ----> server
     Encryption: pre-shared key
     Content   : empty
  2. server ----> client
     Encryption: pre-shared key
     Content   : keygen phrase for the session key
*/
static bool
partialHandshakeToSessionKeyTest() {
  LOG(INFO) << "Start handshake.";
  // Send HELLO
  Message m;
  m.Clear();
  m.set_message_type(Message::HELLO);
  hexdump(crypto::getKeyGenPhrase().data(), crypto::getKeyGenPhrase().length());
  const Message* response = messageHandler(m);
  if (response == nullptr) {
    LOG(INFO) << "No response to HELLO";
    return false;
  }

  // Sanity check response
  Hello hello;
  if (!response->has_message_body() ||
      !hello.ParseFromString(response->message_body()) ||
      !hello.has_keygen_phrase()) {
    LOG(INFO) << "Bad response to HELLO";
    return false;
  }

  // Sanity check session key
  hexdump(hello.keygen_phrase().data(), hello.keygen_phrase().length());
  const std::string& keygen_phrase = crypto::getKeyGenPhrase();
  hexdump(keygen_phrase.data(), keygen_phrase.length());
  if (hello.keygen_phrase() != keygen_phrase ||
      keygen_phrase == DEFAULT_KEYGEN_PHRASE) {
    LOG(INFO) << "Bad session key";
    return false;
  }
  return true;
}

static bool
securityHandshakeTest() {
  resetAuthentication();
  Message m;
  LOG(INFO) << "Send a request before handshake.";
  m.Clear();
  m.set_message_type(Message::NODE_INFO);
  if (messageHandler(m) != nullptr) {
    LOG(INFO) << "Response before complete handshake";
    return false;
  }

  // Start handshake
  if (!partialHandshakeToSessionKeyTest()) {
    LOG(INFO) << "Failed to start handshake.";
    return false;
  }

  // Send an unexpected message in middle of handshake
  m.Clear();
  m.set_message_type(Message::NODE_INFO);
  if (messageHandler(m) != nullptr) {
    LOG(INFO) << "Response before complete handshake";
    return false;
  }

  if (crypto::getKeyGenPhrase() != DEFAULT_KEYGEN_PHRASE) {
    LOG(INFO) << "Bad handshake fails to reset encryption key";
    return false;
  }

  // Restart the handshake (expect authentication state to be reset)
  if (!partialHandshakeToSessionKeyTest()) {
    LOG(INFO) << "Failed to restart handshake.";
    return false;
  }

  // Complete the handshake
  m.Clear();
  m.set_message_type(Message::HELLO);
  const Message* response = messageHandler(m);
  if (response == nullptr) {
    LOG(INFO) << "No response to session key ack";
    return false;
  }

  // Verify that we are authenticated
  Hello hello;
  if (!response->has_message_body() ||
      !hello.ParseFromString(response->message_body()) ||
      !hello.has_auth_ok() || !hello.auth_ok()) {
    LOG(INFO) << "No auth_ok";
    return false;
  }

  LOG(INFO) << "Send request after authentication";
  m.Clear();
  m.set_message_type(Message::NODE_INFO);
  if (messageHandler(m) == nullptr) {
    LOG(INFO) << "No response after complete handshake";
    return false;
  }

  LOG(INFO) << "Initiate duplicate handshake after authentication";
  m.Clear();
  m.set_message_type(Message::HELLO);
  if (messageHandler(m)) {
    LOG(INFO) << "Response to duplicate handshake";
    return false;
  }

  LOG(INFO) << "securityHandshakeTest passed";
  return true;
}

bool
unitTest() {
  return protoBufSanityTest() && cryptoTest() &&
         adequateBufSizeSerializeTest() && inadequateBufSizeSerializeTest() &&
         (!FLAGS_encryption || securityHandshakeTest());
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
