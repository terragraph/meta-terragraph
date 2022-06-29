/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "terragraph.h"

#include <stdio.h>
#include <string.h>

static const char* TAG = "tunnel";

static const void* uart = 0; // uart context
static const void* tcp = 0; // tcp server context

static void
tunnel_uart_rx(const char* buf, size_t sz) {
  int err = tg_tcp_send(tcp, buf, sz);
  if (err < 0) {
    ESP_LOGE(TAG, "Send failed: %d\n", err);
  }
}

static void
tunnel_tcp_rx(const char* buf, int len) {
  tg_uart_send(uart, buf, len);
}

void
tg_tunnel_init() {
  // Create the uart context and uart listener thread for tunnel.
  // The uart context includes low level details that the tunnel has no
  // business knowing about, so it is created by the uart module.
  uart = tg_uart_tunnel_init(tunnel_uart_rx);

  // Create the tcp server thread for tunnel
  tcp_task_param_t p = {.port = 12345,
                        .sock = -1,
                        .rxBufSize = 256,
                        .rxBuf = malloc(256),
                        .rx = tunnel_tcp_rx,
                        .connected = 0,
                        .disconnected = 0};
  tcp_task_param_t* tp = malloc(sizeof(tcp_task_param_t));
  *tp = p;
  tcp = tp; // persist the tcp context so tunnel can send to connection socket
  xTaskCreate(tg_tcp_server_main, "tunnel_server", 4096, (void*)tcp, 5, NULL);
}
