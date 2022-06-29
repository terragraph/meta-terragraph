/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once

#include <cstddef> // for std::size_t
#include <string>

namespace facebook {
namespace terragraph {
namespace mobd {

const std::size_t SERIALIZE_OVERHEAD{6};
const std::size_t MAX_PAYLOAD_SIZE{32 * 1024};
const std::size_t UART_BUF_SIZE{MAX_PAYLOAD_SIZE + SERIALIZE_OVERHEAD};
const std::size_t MAX_TEST_BYTES{31 * 1024};
const std::string DEFAULT_KEYGEN_PHRASE{"Hello World!"};

} // namespace mobd
} // namespace terragraph
} // namespace facebook
