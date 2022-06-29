/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#ifndef TERRAGRAPH_H_
#define TERRAGRAPH_H_

#include <stdint.h>
#include <stdio.h> // for size_t

// Callback from a uart rx thread
typedef void (*uart_callback_t)(const char* buf, size_t sz);

// uart context
typedef struct {
  const char* taskName;
  size_t uartBufSize;
  char* uartBuf;
  int uartNum; // ESP uart interface: 0,1,2
  int txGpio; // tx pin
  int rxGpio; // rx pin
  uart_callback_t rx; // handle data from host on rx pin
} uart_task_param_t;

// Send data to a uart
void tg_uart_send(const void* uart, const char* buf, size_t sz);

// Create the uart thread for telnet
// rx - callback for uart serial data received from host
// Return: uart context
const void* tg_uart_telnet_init(uart_callback_t rx);

// Create the uart thread for the tcp tunnel.
// rx - callback for uart serial data received from host
// Return: uart context
const void* tg_uart_tunnel_init(uart_callback_t rx);

// Initialize the uart for slip
void tg_uart_slip_init();

// Start the telnet server thread
void tg_telnet_init();

// Reset the telnet server back to listening state
void tg_telnet_reset();

// Start the tcp tunnel thread
void tg_tunnel_init();

// Initialize the slip network interface
void tg_slip_init();

// Initialize the i2c interface
void tg_i2c_init();

// tcp server context
typedef struct {
  int port; // port for tcp server
  int sock; // connection socket
  size_t rxBufSize;
  char* rxBuf; // receive buffer
  void (*rx)(const char* data, int len); // data received on sock (required)
  void (*connected)(void); // connection socket created (0 is ok)
  void (*disconnected)(void); // connection socket closed (0 is ok)
} tcp_task_param_t;

// Send to a tcp connection socket
// tcp - tcp server context
int tg_tcp_send(const void* tcp, const void* buf, size_t len);

// tcp server main function
//  pvParameters - tcp server context
void tg_tcp_server_main(void* pvParameters);

// Disconnect existing connection and return to listening state
void tg_tcp_server_reset(tcp_task_param_t *tp);

// The ssid and password sizes are hardcoded in the esp-idf headers
// See:
// https://github.com/espressif/esp-idf/blob/master/components/esp_wifi/include/esp_wifi_types.h
#define TG_SSID_LEN 32
#define TG_PASSWORD_LEN 64
#define TG_MAC_LEN 6
#define TG_MAC_STR_LEN 20 // string representation of mac address

// Configuration persisted in flash
typedef struct {
  char ssid[TG_SSID_LEN]; // default: see get_default_ssid()
  char password[TG_PASSWORD_LEN]; // default: DEFAULT_WIFI_PASSWORD
  uint8_t mac[TG_MAC_LEN]; // wifi AP mac address, default: factory default
  uint8_t macValid; // 0-invalid, 1-valid (not saved in flash)
  int8_t slip; // 0 -tcp data forwarding (default), 1-slip
  int32_t slip_baud; // Baud rate for slip
  int8_t log2uart; // 0-disable uart logging (default), 1-enable uart logging
} tg_flash_config_t;

// Retrieve config from the flash.
// Returns sensible defaults if the flash is bad or keys are missing.
void read_flash_config(tg_flash_config_t* config);

// Convert string representation of mac to uint8_t
// s - expected format: xx:xx:xx:xx:xx:xx
// mac - at least 6 bytes (only updated if s is a valid mac address)
// Return: 0 - success, does not alter mac on failure
int str2mac(const char* s, uint8_t* mac);

// Get string representation of a mac address
// buf - at least 18 bytes
// Return: buf (xx:xx:xx:xx:xx:xx or ?? if conversion fails)
const char* mac2str(const uint8_t* mac, char* buf);

#endif
