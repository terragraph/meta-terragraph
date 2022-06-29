## Puma wifi - Factory Test Steps

1. **Power off the Puma unit**

2. **Insert main digital board jumpers**
  * Disconnect the linux serial console cable.
  * Short J154/J155 on the main digital board.
  * These jumpers are near the linux serial console cable header.

3. **Attach the wifi antenna**

4. **Power on the Puma unit, and ssh to it via nic0**
  * Keep this ssh session open during the wifi tests.
  * Note: test steps in this ssh session are tagged with "(ssh)" below.

5. **Generate configuration for the ESP32 (ssh)**

  * The configuration file will be created in the current directory
    * `esp32config.bin` <--- ESP32 configuration file
    * `esp32config.csv` <--- human readable version

```
  # Generate ESP32 configuration
  node-fe-19-44-01-00-49:~# make_esp32_cfg -w

  Creating CSV file esp32config.csv with the following config
    mac: not set, use ESP32 factory default
    password: not set, use the ESP32 f/w default (facebook)
    ssid: not set, use the ESP32 f/w default (factory mac address)
    uart logs: disabled
    slip: enabled
  ...
  Done!
```

6. **Flash the ESP32 with firmware and configuration (ssh)**

```
  node-fe-19-44-01-00-49:~# flash_esp32 -efc esp32config.bin

  Preparing to flash the following parts:
  0x10000 /usr/share/esp32/wifi_softAP.bin
  0x1000 /usr/share/esp32/bootloader.bin
  0x9000 esp32config.bin
  0x8000 /usr/share/esp32/partitions_singleapp.bin
  Setting serial port (/dev/ttyS2) to 115200 baud.
  Booting the ESP32 into serial bootloader mode
  Flashing
  ...
  Hash of data verified.

  Leaving...
  Staying in bootloader.
  Booting the ESP32 into normal execution mode
  Done
```

7. **Retrieve the advertised ESP32 SSID (ssh)**

  * The ssid is required for over the air testing in the following steps.
  * By default, the ESP32 advertises a unique ssid which includes its mac address.
    * Default ssid example: `PM-24:0a:c4:4e:bb:b8`
    * Note: it is possible to configure the ssid and the wifi mac address,
      but the default is simpler for testing and guaranteed to be unique.

```
  # Retrieve the advertised ESP32 SSID.
  # This ssid will be correct only if steps 5 and 6 were followed.

  node-fe-19-44-01-00-49:~# flash_esp32 -g
  Setting serial port (/dev/ttyS2) to 115200 baud.
  Booting the ESP32 into serial bootloader mode
  Getting configuration
  Default SSID: PM-24:0a:c4:4e:bb:b8  <--- take note
  Booting the ESP32 into normal execution mode
  Done
```


8. **Configure the slip network interface to the ESP32 (ssh)**

```
  node-fe-19-44-01-00-49:~# slattach -L -p slip -s 115200 /dev/ttyS2&
  node-fe-19-44-01-00-49:~# ifconfig sl0 192.168.5.1 pointopoint 192.168.5.2 up mtu 1500
  node-fe-19-44-01-00-49:~# ip r a 192.168.4.0/24 via 192.168.5.2
```

9. **Connect laptop or phone to the Puma wifi network (laptop/phone)**
  * Connect to the ssid that was retrieved in step 7.
  * Only one wifi client connection is supported.
  * Mac/Windows laptops, and Android phones have been tested.
    * The "termius" android phone app works well.

```
  Puma wifi parameters:
    SSID (example)     : PM-24:0A:C4:4E:BB:B8
    Password           : facebook

  Windows/mac client network settings:
    IP address     : 192.168.4.x   (assigned by ESP32 DHCP, x = 2,3,...)
    Subnet mask    : 255.255.255.0
    Gateway/router : 192.168.4.1

  ESP32 network settings:
    192.168.4.1 - wifi access point
    192.168.5.2 - SLIP interface endpoint in the ESP32
    192.168.5.1 - SLIP interface endpoint in the NXP

  It may be necessary to add a route to the 192.168.5.x network in windows/mac:

    # Windows example. Run the cmd shell as 'admin'.
    C:\> route add 192.168.5.0 mask 255.255.255.0 192.168.4.1
```

10. **telnet to the Puma unit over wifi (laptop/phone)**

  * telnet tests the ESP32 wifi in normal execution mode, and one of two serial
    interfaces - `/dev/ttyS0` - which was not exercised during the ESP32 upgrade.

```
  # Windows example. Other telnet clients will also work.
  C:\> telnet 192.168.4.1

  <-- Hit Enter a couple of times                                 -->
  <-- The mac address in the login prompt here should be the same -->
  <-- as the mac address displayed in the ssh session prompts     -->

  Facebook Terragraph Release RELEASE_M46_PRE-357-gb980bf864 node-fe-19-44-01-00-49 ttyS0
  node-fe-19-44-01-00-49 login:
```

11. **ping the Puma unit over wifi (laptop/phone)**

  * The pings test ESP32 wifi, and one of two ESP32/NXP serial interfaces,
    `/dev/ttyS2`, which is exercised here in normal execution mode.

```
# Windows example.
C:\>ping -n 3 192.168.5.1

Pinging 192.168.5.1 with 32 bytes of data:
Reply from 192.168.5.1: bytes=32 time=24ms TTL=63
Reply from 192.168.5.1: bytes=32 time=20ms TTL=63
Reply from 192.168.5.1: bytes=32 time=32ms TTL=63

Ping statistics for 192.168.5.1:
    Packets: Sent = 3, Received = 3, Lost = 0 (0% loss),
Approximate round trip times in milli-seconds:
    Minimum = 20ms, Maximum = 32ms, Average = 25ms
```

12. **(Optional) Remove the SLIP interface and its associated route (ssh)**

```
  node-fe-19-44-01-00-49:~# killall slattach
```
