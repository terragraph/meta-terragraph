/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @format
 */

/**
 * Creating a sidebar enables you to:
 - create an ordered group of docs
 - render a sidebar for each doc of that group
 - provide next/previous navigation

 The sidebars can be generated from the filesystem, or explicitly defined here.

 Create as many sidebars as you want.
 */

 module.exports = {
  developerManualSidebar: [
    {
      type: "category",
      label: "Developer Manual",
      items: [
        "developer/Overview",
        {
          type: "category",
          label: "Architecture",
          items: [
            "developer/Communication_Protocol",
            "developer/Routing_Layer",
            "developer/Driver_Interface",
            "developer/Driver_Stack",
            "developer/VPP_Implementation",
            "developer/Timing_Synchronization",
            "developer/PTP_SyncE",
            "developer/WiFi",
          ],
          collapsed: true,
        },
        {
          type: "category",
          label: "Firmware Layer",
          items: [
            "developer/Beamforming_Link_Adaptation",
            "developer/MAC_PHY_Specification",
            "developer/PHY_Algorithms",
            "developer/Firmware_Stats",
          ],
          collapsed: true,
        },
        {
          type: "category",
          label: "End-to-End (E2E) Service",
          items: [
            "developer/Topology_Management",
            "developer/Network_Ignition",
            "developer/Software_Upgrade",
            "developer/Configuration_Management",
            "developer/Scans",
            "developer/Network_Measurements",
            "developer/Prefix_Allocation",
            "developer/Topology_Discovery",
          ],
          collapsed: true,
        },
        {
          type: "category",
          label: "Application Layer Modules",
          items: [
            "developer/Stats_Events_Logs",
            "developer/Terragraph_CLI",
            "developer/API_Service",
            "developer/Local_Web_Interface",
            "developer/LED_Agent",
          ],
          collapsed: true,
        },
        {
          type: "category",
          label: "System Management",
          items: [
            "developer/Service_Scripts",
            "developer/Watchdog",
            "developer/High_Availability",
            "developer/Security",
          ],
          collapsed: true,
        },
        {
          type: "category",
          label: "Version Control",
          items: [
            "developer/Release_Conventions",
            "developer/Firmware_Versioning",
          ],
          collapsed: true,
        },
      ],
      link: {
        type: "doc",
        id: "developer/README",
      },
    },
  ],
  runbookSidebar: [
    {
      type: "category",
      label: "Runbook",
      items: [
        "runbook/Overview",
        "runbook/Quick_Start",
        "runbook/Planning",
        "runbook/Deployment",
        "runbook/Maintenance",
        "runbook/Monitoring",
        "runbook/Routing",
        "runbook/Testing",
        "runbook/Troubleshooting",
        "runbook/Appendix",
      ],
      link: {
        type: "doc",
        id: "runbook/README",
      },
    },
  ],
};
