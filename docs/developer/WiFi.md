# Wi-Fi
This document describes Terragraph's Wi-Fi architecture on Puma.

## Overview
Puma uses an ESP32 chip (ESP32-WROOM-32U) from Espressif Systems for Wi-Fi. Two
serial ports are used:
* `/dev/ttyS0` - Linux console access via Telnet
* `/dev/ttyS2` - SLIP port

The following IP addresses are configured by default:

| IP Address  | Description                              |
| ----------- | ---------------------------------------- |
| 192.168.4.1 | Wi-Fi AP                                 |
| 192.168.4.x | Client/station IP assigned by ESP32 DHCP |
| 192.168.5.2 | SLIP interface endpoint in ESP32         |
| 192.168.5.1 | SLIP interface endpoint in NXP           |

Wi-Fi is only used for administrative purposes and does not carry any user
traffic.

## ESP32 Firmware
The ESP32 firmware is built via `recipes-wifi/esp-fw/esp-fw_0.1.bb`. This uses
[ESP-IDF], the official development framework. Sources are located in
`recipes-wifi/esp-fw/files/src/`.

Updating ESP32 firmware is done through the following scripts:
* `make_esp32_cfg.sh` - Generates the binary config-partition image
  (`esp32config.bin`)
* `flash_esp32.sh` - Flashes the application, configuration, etc.

The default firmware binaries are installed in `/usr/share/esp32/`:

| Binary                     | Purpose         |
| -------------------------- | --------------- |
| `bootloader.bin`           | bootloader      |
| `partitions_singleapp.bin` | partition table |
| `wifi_softAP.bin`          | application     |

## Telnet
The ESP32 firmware includes a Telnet listener which accepts connections on
`192.168.4.1`. This can be used to access the Linux console. To disable the
serial console altogether (not only over Wi-Fi), use the node configuration
field `envParams.SERIAL_CONSOLE_DISABLE`.

## SLIP
SLIP (Serial Line Internet Protocol) is used to enable TCP/IP on the serial port
`/dev/ttyS2`. This is configured via node configuration keys `envParams.SLIP_*`,
along with two `sv` services installed through
`recipes-wifi/esp-slip/esp-slip_0.1.bb`:
* `slip` - wrapper for `slattach`
* `slip_route` - IP configuration (run once)

SLIP speeds are limited based on the configured baud rate. This is 115200 by
default, but can be increased to 576000 (through a patch to `slattach` in
`recipes-extended/net-tools/files/0000-Add-higher-baud-rates.patch`).

## Resources
* [ESP-IDF] - Espressif IoT Development Framework

[ESP-IDF]: https://github.com/espressif/esp-idf
