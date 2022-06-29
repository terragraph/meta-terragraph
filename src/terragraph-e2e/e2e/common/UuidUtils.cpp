/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "UuidUtils.h"

#include <boost/uuid/uuid_io.hpp>

namespace facebook {
namespace terragraph {

boost::uuids::random_generator UuidUtils::gen;

std::string
UuidUtils::genUuid() {
  return boost::uuids::to_string(UuidUtils::gen());
}

} // namespace terragraph
} // namespace facebook
