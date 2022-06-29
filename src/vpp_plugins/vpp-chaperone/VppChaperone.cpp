/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include <chrono>
#include <thread>

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "VppClient.h"
#include "VppConfigManager.h"

using namespace facebook::terragraph::vpp;

// Config files
DEFINE_string (node_config_env, "/data/cfg/config", "Config environment file");
DEFINE_string (node_config_file, "/data/cfg/node_config.json",
               "Node config JSON file");

// VAPI config
DEFINE_int32 (vapi_max_outstanding_requests, 64,
              "Max number of outstanding requests queued by the VAPI client");
DEFINE_int32 (vapi_response_queue_size, 32,
              "Size of the VAPI client's response queue");

int main (int argc, char **argv)
{
  // Program initialization
  google::InstallFailureSignalHandler ();
  gflags::ParseCommandLineFlags (&argc, &argv, true);
  google::InitGoogleLogging (argv[0]);
  FLAGS_logtostderr = true;

  // Parse config
  VppConfigManager vppConfigManager (FLAGS_node_config_env,
                                     FLAGS_node_config_file);

  // Create VPP connection
  VppClient vppClient (FLAGS_vapi_max_outstanding_requests,
                       FLAGS_vapi_response_queue_size);
  while (!vppClient.connect ())
    {
      LOG (INFO) << "Waiting for VPP to start...";
      std::this_thread::sleep_for (std::chrono::seconds (2));
    }
  LOG (INFO) << "Connected to VPP.";

  // Apply configs to VPP
  vppConfigManager.run (vppClient);

  LOG (INFO) << "Done.";
  return 0;
}
