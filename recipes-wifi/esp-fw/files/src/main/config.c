/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "esp_flash_encrypt.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "terragraph.h"
#include <stdio.h>
#include <string.h>

#define DEFAULT_WIFI_PASSWORD "facebook" // max length: see TG_PASSWORD_LEN
#define NVS_NAMESPACE "default" // max length: 15

// Create a unique ssid from the factory-programmed base mac address.
// Example SSID: PM-24:0A:C4:4E:BB:B8
static void
get_default_ssid(char* ssid) {
  const char* ssid_prefix = "PM-";
  uint8_t mac[TG_MAC_LEN];
  memset(mac, 0, sizeof(mac));
  esp_efuse_mac_get_default(mac);
  char buf[TG_MAC_STR_LEN];
  snprintf(ssid, TG_SSID_LEN, "%s%s", ssid_prefix, mac2str(mac, buf));
}

void
read_flash_config(tg_flash_config_t* config) {
  // Fail hard if the NVS subsystem can not be initialized.
  ESP_ERROR_CHECK(nvs_flash_init());

  // Set default config in case flash is bad or keys are missing
  memset(config, 0, sizeof(*config));
  strncpy(config->password, DEFAULT_WIFI_PASSWORD, TG_PASSWORD_LEN);
  config->password[TG_PASSWORD_LEN - 1] = 0;
  get_default_ssid(config->ssid);

  // Try to open the default namespace
  nvs_handle handle;
  if (ESP_OK != nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle)) {
    return; // Bad or un-initialized config partition.
  }

  // Try to read each persisted key

  // ssid
  size_t len = TG_SSID_LEN;
  nvs_get_str(handle, "ssid", config->ssid, &len);

  // password
  len = TG_PASSWORD_LEN;
  nvs_get_str(handle, "password", config->password, &len);

  // mac address
  char mac[TG_MAC_STR_LEN];
  len = sizeof(mac);
  memset(mac, 0, len);
  if (ESP_OK == nvs_get_str(handle, "mac", mac, &len) &&
      0 == str2mac(mac, config->mac)) {
    config->macValid = 1;
  }

  // slip
  nvs_get_i8(handle, "slip", &config->slip);

  // slip baud
  config->slip_baud = 115200; // default
  nvs_get_i32(handle, "slip_baud", &config->slip_baud);

  // uart log suppression
  nvs_get_i8(handle, "log2uart", &config->log2uart);

  // Clean up
  nvs_close(handle);
}
