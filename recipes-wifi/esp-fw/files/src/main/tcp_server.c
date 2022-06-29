/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "terragraph.h"

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include <lwip/netdb.h>

static const char* TAG = "tcp_server";

int
tg_tcp_send(const void* tcp, const void* buf, size_t len) {
  if (tcp == 0 || buf == 0) {
    ESP_LOGE(TAG, "tcp %p buf %p len %d\n", tcp, buf, len);
    return -1;
  }
  const tcp_task_param_t* tp = (const tcp_task_param_t*)tcp;
  while (len > 0) {
    ssize_t ns = send(tp->sock, buf, len, 0);
    if (ns < 0) {
      ESP_LOGE(TAG, "Send failed, errno %d\n", errno);
      return (int)ns;
    }
    len -= ns;
  }
  return 0;
}

void
tg_tcp_server_main(void* pvParameters) {
  char addr_str[128];
  int addr_family;
  int ip_protocol;
  tcp_task_param_t* tp = (tcp_task_param_t*)pvParameters;

#ifdef CONFIG_EXAMPLE_IPV4
  struct sockaddr_in destAddr;
  destAddr.sin_addr.s_addr = htonl(INADDR_ANY);
  destAddr.sin_family = AF_INET;
  destAddr.sin_port = htons(tp->port);
  addr_family = AF_INET;
  ip_protocol = IPPROTO_IP;
  inet_ntoa_r(destAddr.sin_addr, addr_str, sizeof(addr_str) - 1);
#else // IPV6
  struct sockaddr_in6 destAddr;
  bzero(&destAddr.sin6_addr.un, sizeof(destAddr.sin6_addr.un));
  destAddr.sin6_family = AF_INET6;
  destAddr.sin6_port = htons(tp->port);
  addr_family = AF_INET6;
  ip_protocol = IPPROTO_IPV6;
  inet6_ntoa_r(destAddr.sin6_addr, addr_str, sizeof(addr_str) - 1);
#endif

  int listen_sock = socket(addr_family, SOCK_STREAM, ip_protocol);
  if (listen_sock < 0) {
    ESP_LOGE(TAG, "Unable to create socket: errno %d", errno);
  }
  ESP_LOGI(TAG, "Socket created");

  int err = bind(listen_sock, (struct sockaddr*)&destAddr, sizeof(destAddr));
  if (err != 0) {
    ESP_LOGE(TAG, "Socket unable to bind: errno %d", errno);
  }
  ESP_LOGI(TAG, "Socket binded");

  while (1) {
    err = listen(listen_sock, 1);
    if (err != 0) {
      ESP_LOGE(TAG, "Error occured during listen: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "Socket listening");

    struct sockaddr_in6 sourceAddr; // Large enough for both IPv4 or IPv6
    uint addrLen = sizeof(sourceAddr);
    tp->sock = accept(listen_sock, (struct sockaddr*)&sourceAddr, &addrLen);
    if (tp->sock < 0) {
      ESP_LOGE(TAG, "Unable to accept connection: errno %d", errno);
      break;
    }
    ESP_LOGI(TAG, "Socket accepted");

    if (tp->connected) {
      tp->connected();
    }

    while (1) {
      int len = recv(tp->sock, tp->rxBuf, tp->rxBufSize - 1, MSG_DONTWAIT);
      // Error occured during receiving
      if (len < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
          vTaskDelay(1);
          continue;
        }
        ESP_LOGE(TAG, "recv failed: errno %d", errno);
        break;
      }
      // Connection closed
      else if (len == 0) {
        ESP_LOGI(TAG, "Connection closed");
        break;
      }
      // Data received
      else {
        tp->rx(tp->rxBuf, len);
      }
    }

    if (tp->disconnected) {
      tp->disconnected();
    }

    tg_tcp_server_reset(tp);
  }
  vTaskDelete(NULL);
}

void
tg_tcp_server_reset(tcp_task_param_t *tp) {
  if (tp && tp->sock != -1) {
    ESP_LOGI(TAG, "Shutting down socket and restarting...");
    shutdown(tp->sock, 0);
    close(tp->sock);
    tp->sock = -1;
  }
}
