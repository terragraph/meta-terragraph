/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "mobile_common/Const.h"
#include "mobile_common/Crypto.h"
#include "TcpServer.h"
#ifdef TG_PLATFORM
#include "Uart.h"
#endif
#include "mobile_common/UnitTest.h"
#include "mobile_common/Utils.h"
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#ifdef TG_PLATFORM
#include <folly/init/Init.h>
#endif

DEFINE_bool(encryption, false, "Enable enryption");
DEFINE_bool(unittest, false, "Run unit tests only"); // TODO: build-time gtest
DEFINE_bool(socket, true, "listen on a socket instead of uart");

using namespace facebook::terragraph::mobd;

#ifndef TG_PLATFORM
static void
uartListen() {
  LOG(ERROR) << "uart is not supported on this platform";
  exit(1);
}
#endif

int
main(int argc, char* argv[]) {

#ifndef TG_PLATFORM
  google::InitGoogleLogging(argv[0]);
  gflags::ParseCommandLineFlags(&argc, &argv, true);
#else
  folly::init(&argc, &argv); // folly is only needed in TG platforms
#endif

  FLAGS_logtostderr = true;

  if (FLAGS_unittest) {
    if (unitTest()) {
      LOG(INFO) << "Unit tests passed.";
      return 0;
    } else {
      LOG(INFO) << "Unit tests failed.";
      return 1;
    }
  }

  crypto::init(DEFAULT_KEYGEN_PHRASE);

  try {
    if (FLAGS_socket) {
      tcpServerStart();
    } else {
      uartListen();
    }
  } catch (std::exception const& e) {
    LOG(ERROR) << "Caught " << e.what();
  }

  return 0;
}
