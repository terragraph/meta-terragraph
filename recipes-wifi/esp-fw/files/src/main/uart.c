/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sio.h"
#include "terragraph.h"
#include <stdio.h>

// No flow control for any serial port
#define TG_UART_RTS (UART_PIN_NO_CHANGE)
#define TG_UART_CTS (UART_PIN_NO_CHANGE)

void
tg_uart_send(const void* uart, const char* buf, size_t sz) {
  if (uart == 0 || buf == 0 || sz == 0) {
    return;
  }
  const uart_task_param_t* tp = (const uart_task_param_t*)uart;
  uart_write_bytes(tp->uartNum, buf, sz);
}

static void
tg_uart_rx_task(void* p) {
  const uart_task_param_t* tp = (const uart_task_param_t*)p;

  // Read uart and make rx callbacks
  while (1) {
    int len = uart_read_bytes(
        tp->uartNum,
        (uint8_t*)tp->uartBuf,
        tp->uartBufSize,
        20 / portTICK_RATE_MS);
    if (len > 0) {
      tp->rx(tp->uartBuf, len);
    } else {
      // const TickType_t xDelay = 20 / portTICK_PERIOD_MS;
      vTaskDelay(1);
    }
  }
}

static void
tg_uart_init(const uart_task_param_t* tp, int baudRate) {
  uart_config_t uart_config = {.baud_rate = baudRate,
                               .data_bits = UART_DATA_8_BITS,
                               .parity = UART_PARITY_DISABLE,
                               .stop_bits = UART_STOP_BITS_1,
                               .flow_ctrl = UART_HW_FLOWCTRL_DISABLE};
  uart_param_config(tp->uartNum, &uart_config);
  uart_set_pin(tp->uartNum, tp->txGpio, tp->rxGpio, TG_UART_RTS, TG_UART_CTS);
  uart_driver_install(tp->uartNum, tp->uartBufSize * 2, 0, 0, NULL, 0);

  if (tp->rx != NULL) {
    xTaskCreate(tg_uart_rx_task, tp->taskName, 5120, (void*)tp, 10, NULL);
  }
}

const void*
tg_uart_telnet_init(uart_callback_t rx) {
  uart_task_param_t p = {.taskName = "uartTelnet",
                         .uartBufSize = 512,
                         .uartBuf = malloc(2 * 512),
                         .uartNum = UART_NUM_1,
                         .txGpio = GPIO_NUM_27, // proto1: GPIO_NUM_1;
                         .rxGpio = GPIO_NUM_25, // proto1: GPIO_NUM_3;
                         .rx = rx};

  uart_task_param_t* tp = malloc(sizeof(uart_task_param_t));
  *tp = p;
  tg_uart_init(tp, 115200);
  return tp;
}

// Note: UART_NUM_0 is either slip or a simple tcp data forwarding tunnel
const void*
tg_uart_tunnel_init(uart_callback_t rx) {
  uart_task_param_t p = {.taskName = "uartTunnel",
                         .uartBufSize = 512,
                         .uartBuf = malloc(2 * 512),
                         .uartNum = UART_NUM_0,
                         .txGpio = GPIO_NUM_1, // proto1: linux console;
                         .rxGpio = GPIO_NUM_3, // proto1: linux console;
                         .rx = rx};

  uart_task_param_t* tp = malloc(sizeof(uart_task_param_t));
  *tp = p;
  tg_uart_init(tp, 115200);
  return tp;
}

// Note: UART_NUM_0 is either slip or a simple tcp data forwarding tunnel
// TODO: larger uart buffer size (1500?) may be better for slip.
void
tg_uart_slip_init(int baudRate) {
  uart_task_param_t p = {.taskName = NULL,
                         .uartBufSize = 512,
                         .uartBuf = malloc(2 * 512),
                         .uartNum = UART_NUM_0,
                         .txGpio = GPIO_NUM_1,
                         .rxGpio = GPIO_NUM_3,
                         .rx = NULL};
  tg_uart_init(&p, baudRate);
}

// Implement NETIF-SLIP uart callbacks
//
// Not required by SLIP:
//  u8_t sio_recv(sio_fd_t fd) {
//    u8_t buf;
//    uart_read_bytes(UART_NUM_0, &buf, 1, 20 / portTICK_RATE_MS);
//    return 0;
// }

sio_fd_t
sio_open(u8_t devnum) {
  // Must not return NULL. See:
  // http://git.savannah.nongnu.org/cgit/lwip.git/tree/src/netif/slipif.c?h=STABLE-2_0_3_RELEASE#n390
  return (void*)1;
}

u32_t
sio_read(sio_fd_t fd, u8_t* data, u32_t len) {
  int c = uart_read_bytes(UART_NUM_0, data, len, 5 / portTICK_RATE_MS);
  return c < 0 ? 0 : c;
}

u32_t
sio_tryread(sio_fd_t fd, u8_t* data, u32_t len) {
  int c = uart_read_bytes(UART_NUM_0, data, len, 0);
  return c < 0 ? 0 : c;
}

void
sio_send(u8_t c, sio_fd_t fd) {
  uart_write_bytes(UART_NUM_0, (char*)&c, 1);
}
