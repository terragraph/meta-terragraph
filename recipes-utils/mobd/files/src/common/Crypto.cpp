/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "Crypto.h"

#include <chrono>
#include <cstring>
#include <functional>
#include <openssl/aes.h>
#include <openssl/md5.h>
#include <random>

#define KEY_BITS 128

#if KEY_BITS != (8 * MD5_DIGEST_LENGTH) || (8 * AES_BLOCK_SIZE) != KEY_BITS
#error Key size!
#endif

namespace facebook {
namespace terragraph {
namespace mobd {

static std::string keygen_phrase_;
static unsigned char key_[KEY_BITS / 8];

std::size_t
crypto::get_block_size() {
  return AES_BLOCK_SIZE;
}

const std::string&
crypto::getKeyGenPhrase() {
  return keygen_phrase_;
}

// Set key from keygen_phrase.
void
crypto::init(const std::string& keygen_phrase) {
  MD5_CTX context;
  MD5_Init(&context);
  MD5_Update(&context, keygen_phrase.data(), keygen_phrase.length());
  MD5_Final(key_, &context);
  keygen_phrase_ = keygen_phrase;
}

// Set random key.
void
crypto::init() {
  auto seed =
      std::chrono::high_resolution_clock::now().time_since_epoch().count();
  auto r = std::bind(
      std::uniform_int_distribution<int32_t>(0, 255), std::mt19937(seed));
  std::string keygen_phrase;
  keygen_phrase.reserve(AES_BLOCK_SIZE);
  for (int i = 0; i < AES_BLOCK_SIZE; i++) {
    keygen_phrase.push_back(r());
  }
  init(keygen_phrase);
}

void
crypto::encrypt(const char* plain, char* enc, int blocks) {
  std::memset(enc, 0x0, blocks * AES_BLOCK_SIZE);
  AES_KEY aes_key;
  AES_set_encrypt_key(key_, KEY_BITS, &aes_key);
  for (int i = 0; i < blocks; i++) {
    AES_encrypt((unsigned char*)plain, (unsigned char*)enc, &aes_key);
    plain += AES_BLOCK_SIZE;
    enc += AES_BLOCK_SIZE;
  }
}

void
crypto::decrypt(char* plain, const char* enc, int blocks) {
  std::memset(plain, 0x0, blocks * AES_BLOCK_SIZE);
  AES_KEY aes_key;
  AES_set_decrypt_key(key_, KEY_BITS, &aes_key);
  for (int i = 0; i < blocks; i++) {
    AES_decrypt((unsigned char*)enc, (unsigned char*)plain, &aes_key);
    plain += AES_BLOCK_SIZE;
    enc += AES_BLOCK_SIZE;
  }
}

} // namespace mobd
} // namespace terragraph
} // namespace facebook
