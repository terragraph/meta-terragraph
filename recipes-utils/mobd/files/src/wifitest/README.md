# wifitest application

## Capabilities of wifitest
* Test the ESP32 TCP tunnel through the ESP RXD0/TXD0 serial pins, which is
  /dev/ttyS2 in the NXP.
* Test the mobile app server (mobd) request/reply messaging protocol and datapath.
* Execute runtime unit tests for the messaging protocol and the security handshake
  (to verify build sanity and debug the messaging protocol)

## 1. Pre-requisites
* Puma proto2+
* ESP32 flashed with f/w from a build that includes D19512721.
* (optional) For testing telnet concurrently, short J154/J155 on the main
  digital board. This will forward the linux serial console to the right pins
  (GPIO25/GPIO27) on the ESP32.
* Native build of wifitest. See [Building wifitest](##Building-wifitest) below.

## 2. Connect to the ESP32 wifi network with your laptop
* **Note: Multiple concurrent wifi connections are not supported."
* Connect to the ESP wifi network.
   * SSID: `tg-wifi-fw`
   * PSK: `facebook`
* The ESP32 wifi access point is `192.168.4.1`
* Observe the IP assigned to your laptop by the ESP32 DHCP. This is normally
  `192.168.4.2`
* Configure your laptop network settings as follows
    * IP: `192.168.4.2` <-- the DHCP IP assigned to your laptop
    * Subnet: `255.255.255.0`
    * Router/Gateway: `192.168.4.1` <-- works in most cases
    * Router/Gateway: `192.168.4.2` <-- try the assigned IP if 192.168.4.1 does not work

## 3. Launch the mobile app server (mobd) in Puma
* ssh to the Puma unit and launch mobd with verbose logs
```
  # -timeout is the number of idle seconds after which the
  # server resets state and flushes the serial port
  # -baud 115200 is the default
  # -v 2 most verbose logs, dump every received/sent byte
  # -socket=false listen on a uart instead of a socket
  #
  mobd -socket=false -port /dev/ttyS2 -v 2 -timeout 10000
```

### (optional) Launch the message server from a telnet session instead of ssh
* Note: J154 and J155 have to be shorted on the main digital board
* open a telnet session from your laptop to the ESP32 and log in as root
```
telnet 192.168.4.1
# The login prompt will appear after you hit enter a couple of times.
# Some control and special keys won't work. Notably Ctrl-C does not work
# so you will have to kill running apps via ssh.
```

### Example mobile app server (mobd) logs

* The `-v 2` verbosity shows every received byte, and the types of valid messages
(ex. `NODE_INFO`)

```
mobd -socket=false -port /dev/ttyS2 -v 2 -timeout 1000
I1121 04:38:29.253465 13500 Uart.cpp:125] Listening on /dev/ttyS2
I1121 04:38:42.909337 13500 Uart.cpp:96] RX 8 bytes
I1121 04:38:42.909397 13500 Utils.cpp:204] 000000: 54 47 00 06 08 02 10 00  TG......
I1121 04:38:42.909967 13500 Uart.cpp:96] RX 4 bytes
I1121 04:38:42.909988 13500 Utils.cpp:204] 000000: 28 0a 4f e2              (.O.
I1121 04:38:42.933151 13500 MessageHandler.cpp:67] RX NODE_INFO
I1121 04:38:42.933244 13500 Uart.cpp:109] response size 195
I1121 04:38:42.933310 13500 Uart.cpp:78] TX 195 bytes. TX req done.
I1121 04:44:08.738914 13500 Uart.cpp:96] RX 8 bytes
I1121 04:44:08.738978 13500 Utils.cpp:204] 000000: 54 47 00 0a 08 04 10 00  TG......
I1121 04:44:08.739547 13500 Uart.cpp:96] RX 8 bytes
I1121 04:44:08.739571 13500 Utils.cpp:204] 000000: 22 02 08 0a 28 0a 01 71  "...(..q
I1121 04:44:08.761602 13500 MessageHandler.cpp:116] RX TEST
```

## wifitest command line examples

###  Send the node info request (this is the default request) and display the response.

```
wifitest -v 1
stevenfalud-mbp:cmake-build-release stevenfaludi$ wifitest -v 1
I0123 00:02:49.152788 399302080 main.cpp:63] Connecting to 192.168.4.1:12345
I0123 00:02:49.163164 399302080 main.cpp:68] Connected!
I0123 00:02:49.163192 399302080 main.cpp:71] Starting test.
I0123 00:02:49.163208 399302080 TestUtils.cpp:73]  ---- REQUEST -----
I0123 00:02:49.163223 399302080 TestUtils.cpp:27] message_type: NODE_INFO cookie: 0 wdog_disable_minutes: 10
I0123 00:02:49.163580 399302080 TestUtils.cpp:29] No message_body
I0123 00:02:49.163714 399302080 TestUtils.cpp:78] Serialized request is 12 bytes
I0123 00:02:49.163777 399302080 TestUtils.cpp:83] Sent 12 request bytes
I0123 00:02:49.163787 399302080 main.cpp:99] Wating for response...
I0123 00:02:49.163794 399302080 TestUtils.cpp:92]  ---- RESPONSE -----
I0123 00:02:49.240624 399302080 TestUtils.cpp:97] Received 256 response bytes
I0123 00:02:49.240697 399302080 TestUtils.cpp:97] Received 49 response bytes
I0123 00:02:49.240731 399302080 TestUtils.cpp:27] message_type: NODE_INFO cookie: 0 message_body: "\n\035Terragraph Uboot Version: 1.0\022mFacebook Terragraph Release RELEASE_M46_PRE-357-gb980bf864 (stevenfaludi@devvm6354 2020-01-21 22:03:43+00:00)\032#NXP TG Board LS1048A (PUMA Proto2)\000\"\000*mFacebook Terragraph Release RELEASE_M46_PRE-120-g838d45872 (ssreedev@devvm1152 Wed Nov 20 20:38:37 UTC 2019)\n"
I0123 00:02:49.240793 399302080 TestUtils.cpp:43] uboot_version: "Terragraph Uboot Version: 1.0" tg_version: "Facebook Terragraph Release RELEASE_M46_PRE-357-gb980bf864 (stevenfaludi@devvm6354 2020-01-21 22:03:43+00:00)" dtb_version: "NXP TG Board LS1048A (PUMA Proto2)\000" nic0_mac_address: "" tg_secondary_version: "Facebook Terragraph Release RELEASE_M46_PRE-120-g838d45872 (ssreedev@devvm1152 Wed Nov 20 20:38:37 UTC 2019)\n"
```

### Send the "test data" request for 5 bytes with the message cookie (sequence number) set to 12345.

* The cookie is always echoed by the server in the response.

```
wifitest -test 10 -cookie 12345 -v 1
I0123 00:10:45.014915 284540352 main.cpp:63] Connecting to 192.168.4.1:12345
I0123 00:10:45.025497 284540352 main.cpp:68] Connected!
I0123 00:10:45.025533 284540352 main.cpp:71] Starting test.
I0123 00:10:45.025570 284540352 TestUtils.cpp:73]  ---- REQUEST -----
I0123 00:10:45.025581 284540352 TestUtils.cpp:27] message_type: TEST cookie: 12345 message_body: "\010\n" wdog_disable_minutes: 10
I0123 00:10:45.025959 284540352 TestUtils.cpp:55] num_test_bytes: 10
I0123 00:10:45.026103 284540352 TestUtils.cpp:78] Serialized request is 17 bytes
I0123 00:10:45.026208 284540352 TestUtils.cpp:83] Sent 17 request bytes
I0123 00:10:45.026238 284540352 main.cpp:99] Wating for response...
I0123 00:10:45.026247 284540352 TestUtils.cpp:92]  ---- RESPONSE -----
I0123 00:10:45.072841 284540352 TestUtils.cpp:97] Received 27 response bytes
I0123 00:10:45.072928 284540352 TestUtils.cpp:27] message_type: TEST cookie: 12345 message_body: "\010\n\022\n\000\001\002\003\004\005\006\007\010\t"
I0123 00:10:45.072957 284540352 TestUtils.cpp:55] num_test_bytes: 10 test_data: "\000\001\002\003\004\005\006\007\010\t"
```

### Keep requesting 11,000 "test data" bytes for ay least 3 seconds
```
wifitest -test 11000 -duration 3
I0123 00:12:23.856694 331906496 main.cpp:63] Connecting to 192.168.4.1:12345
I0123 00:12:23.866976 331906496 main.cpp:68] Connected!
I0123 00:12:23.867007 331906496 main.cpp:71] Starting test.
I0123 00:12:23.867113 331906496 TestUtils.cpp:73]  ---- REQUEST -----
I0123 00:12:23.867130 331906496 TestUtils.cpp:27] message_type: TEST cookie: 0 message_body: "\010\370U" wdog_disable_minutes: 10
I0123 00:12:23.867460 331906496 TestUtils.cpp:55] num_test_bytes: 11000
I0123 00:12:23.867669 331906496 main.cpp:99] Wating for response...
I0123 00:12:23.867687 331906496 TestUtils.cpp:92]  ---- RESPONSE -----
I0123 00:12:24.870080 331906496 TestUtils.cpp:20] message_type: TEST cookie: 0
I0123 00:12:24.870137 331906496 TestUtils.cpp:73]  ---- REQUEST -----
I0123 00:12:24.870149 331906496 TestUtils.cpp:27] message_type: TEST cookie: 1 message_body: "\010\370U" wdog_disable_minutes: 10
I0123 00:12:24.870180 331906496 TestUtils.cpp:55] num_test_bytes: 11000
I0123 00:12:24.870312 331906496 main.cpp:99] Wating for response...
I0123 00:12:24.870328 331906496 TestUtils.cpp:92]  ---- RESPONSE -----
I0123 00:12:25.881861 331906496 TestUtils.cpp:20] message_type: TEST cookie: 1
I0123 00:12:25.881935 331906496 TestUtils.cpp:73]  ---- REQUEST -----
I0123 00:12:25.881948 331906496 TestUtils.cpp:27] message_type: TEST cookie: 2 message_body: "\010\370U" wdog_disable_minutes: 10
I0123 00:12:25.881980 331906496 TestUtils.cpp:55] num_test_bytes: 11000
I0123 00:12:25.882064 331906496 main.cpp:99] Wating for response...
I0123 00:12:25.882079 331906496 TestUtils.cpp:92]  ---- RESPONSE -----
I0123 00:12:26.891144 331906496 TestUtils.cpp:20] message_type: TEST cookie: 2
I0123 00:12:26.891209 331906496 main.cpp:116] Completed 3 second test
```

### Run all the unit tests, including encrypted serialization and the security handshake.

```
wifitest --encryption --unittest
I0123 00:07:52.839632 338423232 UnitTest.cpp:36] protoBufSanityTest passed
I0123 00:07:52.840301 338423232 UnitTest.cpp:65] cryptoTest passed
I0123 00:07:52.840354 338423232 UnitTest.cpp:79] serialize len 23
I0123 00:07:52.840364 338423232 UnitTest.cpp:100] adequateBufSizeSerializeTest passed
I0123 00:07:52.840371 338423232 UnitTest.cpp:79] serialize len 23
E0123 00:07:52.840378 338423232 Utils.cpp:76] Bad buffer sizes
I0123 00:07:52.840382 338423232 UnitTest.cpp:79] serialize len 0
I0123 00:07:52.840389 338423232 UnitTest.cpp:109] inadequateBufSizeSerializeTest passed
I0123 00:07:52.840392 338423232 UnitTest.cpp:161] Send a request before handshake.
I0123 00:07:52.840396 338423232 UnitTest.cpp:124] Start handshake.
I0123 00:07:52.840400 338423232 MessageHandler.cpp:53] RX HELLO, state 0
I0123 00:07:52.840425 338423232 UnitTest.cpp:124] Start handshake.
I0123 00:07:52.840430 338423232 MessageHandler.cpp:53] RX HELLO, state 0
I0123 00:07:52.840436 338423232 MessageHandler.cpp:53] RX HELLO, state 1
I0123 00:07:52.840440 338423232 UnitTest.cpp:212] Send request after authentication
I0123 00:07:52.840445 338423232 MessageHandler.cpp:67] RX NODE_INFO
I0123 00:07:52.840451 338423232 UnitTest.cpp:220] Initiate duplicate handshake after authentication
I0123 00:07:52.840454 338423232 UnitTest.cpp:228] securityHandshakeTest passed
I0123 00:07:52.840458 338423232 main.cpp:50] Unit tests passed.
```

## Building wifitest

`CMakeLists.txt` has been tested with **[CLion](https://www.jetbrains.com/clion)** on Mac and Windows.

### Mac pre-requisites
1. `brew install boost gflags glog openssl protobuf`
2. Install [CLion](https://www.jetbrains.com/clion/)

### Mac build
1. Point CLion at the tester app in the source tree
  * File -> Open -> meta-terragraph/recipes-utils/mobd/files/src/wifitest
2. Create the "Release" build-profile
  * Navigate to CLion -> Preferences -> Build, Execution -> CMake
    * At the bottom of the Profiles section, click + and then Apply
3. Reset the CMake cache
  * Tools -> CMake -> Reset Cache and Reload Project
4. Build
  * Select the "Release" profile for building.
    * Click "Release" from the "wifitest" dropdown on the right hand side of the top toolbar.
  * Build -> Rebuild All In 'Release'
    * Ignore link warnings
5. The standalone `wifitest` binary is created in `meta-terragraph/recipes-utils/mobd/files/src/wifitest/cmake-build-release`

### Windows pre-requisites
1. Install [Visual Studio](https://visualstudio.microsoft.com/)
2. Install [vcpkg](https://github.com/microsoft/vcpkg)
3. `vcpkg install boost` (slow)
4. `vcpkg install gflags glog openssl protobuf` (slow)
5. `vcpkg integrate install`
6. Install [CLion](https://www.jetbrains.com/clion/)
7. Confirm that Visual Studio is the default toolchain in CLion
  *  Navigate to File -> Settings -> Build, Execution --> Toolchains

### Windows build
1. Point CLion at the tester app in the source tree
  * File -> Open -> meta-terragraph/recipes-utils/mobd/files/src/wifitest
2. Create the "Release" build-profile
  * Navigate to File -> Settings -> Build, Execution -> CMake
    * At the bottom of the Profiles section, click + and then Apply
3. Set CMake options for the "Release" and "Debug" profiles. Both must be set.
  * Select the profiles in File -> Settings -> Build, Execution -> CMake -> Profiles
  * Add the following to "CMake Options"<br>
  `-DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake`
  * Click Ok
4. Reset the CMake cache
  * Tools -> CMake -> Reset Cache and Reload Project
5. Build
  * Select the "Release" profile for building.
    * Click "Release" from the "wifitest" dropdown on the right hand side of the top toolbar.
  * Build -> Rebuild All In 'Release'
    * Ignore compiler and linker warnings
6. The `wifitest` binary and all the _required_ dll's are created in `meta-terragraph/recipes-utils/mobd/files/src/wifitest/cmake-build-release`
    * `gflags.dll`
    * `glog.dll`
    * `LIBEAY32.dll`
    * `libprotobuf.dll`
    * `wifitest.exe`<br>

### Visual Studio runtime library requirement
* The Windows binaries require Visual Studio runtime libraries, which may be missing from some Windows boxes.
* The redistributable packages for these libraries can be downloaded from [Microsoft](https://support.microsoft.com/en-us/help/2977003/the-latest-supported-visual-c-downloads). Look for `vc_redist_x86.exe` and `vc_redist.x64.exe`.
