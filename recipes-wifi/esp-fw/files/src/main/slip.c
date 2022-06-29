#include "lwip/ip.h"
#include "lwip/netif.h"
#include "netif/slipif.h"
#include "terragraph.h"

// Reference: https://github.com/martin-ger/esp_slip_router

static struct netif sl_netif;

static void
tg_slip_main(void* param) {
  while (1) {
    slipif_poll((struct netif*)param);
    vTaskDelay(1);
  }
}

// example linux/nxp slip setup
//   slattach -vL -p slip -s 115200 /dev/ttyS2 &
//   ifconfig sl0 192.168.5.1 pointopoint 192.168.5.2 mtu 1500
//   ip r a 192.168.4.0/24 via 192.168.5.2
//
// example mac slip config (assumes en0 interface is the ESP32 network)
// (optional, since 192.168.4.1 is the default gateway)
//   ip r a 192.168.5.0/24 via 192.168.4.1 dev en0
void
tg_slip_init(int baudRate) {
  ip4_addr_t ipaddr;
  ip4_addr_t netmask;
  ip4_addr_t gw;

  IP4_ADDR(&ipaddr, 192, 168, 5, 2);
  IP4_ADDR(&netmask, 255, 255, 255, 0);
  IP4_ADDR(&gw, 192, 168, 5, 1); // slip if ip address on the linux side

  tg_uart_slip_init(baudRate);

  netif_add(&sl_netif, &ipaddr, &netmask, &gw, NULL, slipif_init, ip_input);
  netif_set_link_up(&sl_netif);
  netif_set_up(&sl_netif);

  // TODO Higher slip-task priority may not be necessary
  xTaskCreate(tg_slip_main, "slip_poll", 4096, (void*)&sl_netif, 3, NULL);
}
