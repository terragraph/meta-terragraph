/*  WiFi softAP Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "terragraph.h"
#include <stdio.h>
#include <string.h>

#define MAX_STA_CONN 1

// FreeRTOS event group to signal when we are connected
static EventGroupHandle_t s_wifi_event_group;

static const char* TAG = "main";

static vprintf_like_t esp_default_logger;

// ESP-IDF log handler
//
// Note: We are using both physical serial ports, so the ESP-IDF logs can't be
// easily observed in normal operating mode. By default the logs are output to
// ESP UART0 (NXP /dev/ttyS2) - which is the uart used by the tunnel to the
// Installer App message server in Puma.
// Ideas: introduce an option to forward ESP-IDF logs through telnet, or free
//        up UART0 by switching to SPI.
static int
tg_logger(const char* format, va_list v) {
  return 0; // discard
}

static esp_err_t
event_handler(void* ctx, system_event_t* event) {
  switch (event->event_id) {
    case SYSTEM_EVENT_AP_STACONNECTED:
      ESP_LOGI(
          TAG,
          "station:" MACSTR " join, AID=%d",
          MAC2STR(event->event_info.sta_connected.mac),
          event->event_info.sta_connected.aid);
      /*
       *  We need to reset the server here in case a STA might reassociate
       * after disappearing without raising a leave event.  If not connected,
       * this is just a noop
       */
      tg_telnet_reset();
      break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
      ESP_LOGI(
          TAG,
          "station:" MACSTR " leave, AID=%d",
          MAC2STR(event->event_info.sta_disconnected.mac),
          event->event_info.sta_disconnected.aid);
      tg_telnet_reset();
      break;
    default:
      break;
  }
  return ESP_OK;
}

// Note:
//   * bit 0 in the first mac octet must be 0
//   * esp_wifi_set_mac must be called when the interface is (still) disabled
// See:
// https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/network/esp_wifi.html#_CPPv416esp_wifi_set_mac16wifi_interface_tAL6E_K7uint8_t
static void
tg_wifi_set_mac(const tg_flash_config_t* config) {
  if (config->macValid == 0) {
    return;
  }
  char buf[TG_MAC_STR_LEN];
  esp_err_t err = esp_wifi_set_mac(ESP_IF_WIFI_AP, config->mac);
  if (err == ESP_OK) {
    ESP_LOGI(TAG, "AP mac set to %s", mac2str(config->mac, buf));
  } else {
    ESP_LOGE(
        TAG,
        "esp_wifi_set_mac(%s) - err=%d(%s)",
        mac2str(config->mac, buf),
        err,
        esp_err_to_name(err));
  }
}

static void
wifi_init_softap(const tg_flash_config_t* flash_config) {
  s_wifi_event_group = xEventGroupCreate();

  tcpip_adapter_init();
  ESP_ERROR_CHECK(esp_event_loop_init(event_handler, NULL));

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  tg_wifi_set_mac(flash_config);

  wifi_config_t wifi_config;
  memset(&wifi_config, 0, sizeof(wifi_config));
  wifi_config.ap.max_connection = MAX_STA_CONN;
  if (strlen(flash_config->password) == 0) {
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
  } else {
    wifi_config.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;
  }
  strcpy((char*)wifi_config.ap.password, flash_config->password);
  strcpy((char*)wifi_config.ap.ssid, flash_config->ssid);
  wifi_config.ap.ssid_len = strlen(flash_config->ssid);

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &wifi_config));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(
      TAG,
      "wifi_init_softap finished.SSID:%s password:%s",
      wifi_config.ap.ssid,
      wifi_config.ap.password);
}

void
app_main() {
  // Load config saved in flash and initialize the NVS susbsystem.
  tg_flash_config_t flash_config;
  read_flash_config(&flash_config);

  // Install our ESP-IDF log handler
  if (flash_config.log2uart == 0) {
    esp_default_logger = esp_log_set_vprintf(tg_logger);
  }

  ESP_LOGI(TAG, "ESP_WIFI_MODE_AP");
  wifi_init_softap(&flash_config);

  tg_i2c_init(); // initialize i2c slave
  tg_telnet_init(); // telnet listener
  if (flash_config.slip != 0) {
    tg_slip_init(flash_config.slip_baud); // slip interface
  } else {
    tg_tunnel_init(); // tcp tunnel thread
  }
}
