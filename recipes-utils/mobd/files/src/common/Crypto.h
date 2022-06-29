/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstring>
#include <string>

namespace facebook {
namespace terragraph {
namespace mobd {

namespace crypto {

std::size_t get_block_size();

// Returns: keygen_phrase that generated the current key.
const std::string& getKeyGenPhrase();

// Set random key.
void init();

// Set key from keygen_phrase.
void init(const std::string& keygen_phrase);

void encrypt(const char* plain, char* enc, int blocks);

void decrypt(char* plain, const char* enc, int blocks);
} // namespace crypto

} // namespace mobd
} // namespace terragraph
} // namespace facebook
