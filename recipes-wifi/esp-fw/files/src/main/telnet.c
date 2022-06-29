/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "libtelnet.h"
#include "terragraph.h"

#include <stdio.h>
#include <string.h>

static void telnet_event_handler(
    telnet_t* telnet, telnet_event_t* event, void* user_data);

static const char* TAG = "telnet";

static const telnet_telopt_t telopts[] = {
    {TELNET_TELOPT_ECHO, TELNET_WILL, TELNET_DONT},
    {TELNET_TELOPT_TTYPE, TELNET_WILL, TELNET_DONT},
    {TELNET_TELOPT_COMPRESS2, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_ZMP, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_MSSP, TELNET_WONT, TELNET_DO},
    {TELNET_TELOPT_BINARY, TELNET_WILL, TELNET_DO},
    {TELNET_TELOPT_NAWS, TELNET_WILL, TELNET_DONT},
    {-1, 0, 0}};

static telnet_t* telnet = 0; // telnet context
static const void* uart = 0; // uart context
static tcp_task_param_t* tcp = 0; // tcp server context

static void
telnet_uart_rx(const char* buf, size_t sz) {
  if (telnet) {
    telnet_send(telnet, buf, sz);
  }
}

static void
telnet_tcp_connected() {
  telnet = telnet_init(telopts, telnet_event_handler, 0, 0);
}

static void
telnet_tcp_disconnected() {
  telnet_t* tmp_telnet = telnet;
  telnet = 0;
  telnet_free(tmp_telnet);
}

static void
telnet_tcp_rx(const char* buf, int len) {
  if (telnet) {
    telnet_recv(telnet, buf, len);
  }
}

static void
telnet_event_handler(telnet_t* telnet, telnet_event_t* event, void* user_data) {
  int err;
  switch (event->type) {
    case TELNET_EV_DATA: // raw text has been received
      ESP_LOGI(TAG, "RX_RAW %d", event->data.size);
      tg_uart_send(uart, event->data.buffer, event->data.size);
      break;
    case TELNET_EV_SEND: // data needs to be sent to the peer
      ESP_LOGI(TAG, "TX_PEER %d", event->data.size);
      err = tg_tcp_send(tcp, event->data.buffer, event->data.size);
      if (err < 0) {
        ESP_LOGE(TAG, "Send failed: %d\n", err);
        break;
      }
      break;
    case TELNET_EV_ERROR:
      break;
    default:
      break;
      return;
  }
}

void
tg_telnet_init() {
  // Create the uart context and uart listener thread for telnet.
  // The uart context includes low level details that telnet has no
  // business knowing about, so it is created by the uart module.
  uart = tg_uart_telnet_init(telnet_uart_rx);

  // Create the tcp server thread for telnet
  tcp_task_param_t p = {.port = 23,
                        .sock = -1,
                        .rxBufSize = 128,
                        .rxBuf = malloc(128),
                        .rx = telnet_tcp_rx,
                        .connected = telnet_tcp_connected,
                        .disconnected = telnet_tcp_disconnected};
  tcp_task_param_t* tp = malloc(sizeof(tcp_task_param_t));
  *tp = p;
  tcp = tp; // persist the tcp context so telnet can send to connection socket
  xTaskCreate(tg_tcp_server_main, "telnet_server", 4096, (void*)tcp, 5, NULL);
}

void
tg_telnet_reset(tcp_task_param_t *tp) {
  if (tp)
    tg_tcp_server_reset(tcp);
}
