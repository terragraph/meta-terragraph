Sample Plugin for VPP
=====================

This example illustrates how to develop a VPP plugin with the Terragraph build
system.  For this example we use VPP's own sample plugin, read more
[here](https://wiki.fd.io/view/VPP/How_To_Build_The_Sample_Plugin).  This
example has been minimally modified to work with Terragraph links. The only
change introduced from the plugin that ships with vpp is:

```
--- sample/sample.c.orig        2020-02-27 13:43:41.285326883 -0800
+++ sample/sample.c     2020-02-27 21:07:10.171840192 -0800
@@ -235,5 +235,5 @@
 {
   .arc_name = "device-input",
   .node_name = "sample",
-  .runs_before = VNET_FEATURES ("ethernet-input"),
+  .runs_before = VNET_FEATURES ("tg-link-input"),
 };
```

## Build vpp sample plugin

To build the plugin, execute

```
bitbake vpp-plugin-example
```

This will create the rpm 

```
build/tmp/deploy/rpm/aarch64/vpp-plugin-example-1.0-r0.aarch64.rpm
```

## Install and test

Install the RPM as

```
remount -o rw,reload /
rpm -ivh vpp-plugin-example-1.0-r0.aarch64.rpm
```

then test with the VPP api test
```
vpp_api_test
```

check `/var/log/vpp/current` and see that the plugin was loaded
```
/usr/bin/vpp[8603]: load_one_vat_plugin:67: Loaded plugin: sample_test_plugin.so
```

Observe that the sample node is inserted correctly in the data path:
```
vat# sample_macswap_enable_disable Wigig0/1/0/0
vat# exec show interface Wigig0/1/0/0 features
Feature paths configured on Wigig0/1/0/0...
(..)
device-input:
  sample
  tg-link-input
(...)
```

Capture traffic, observe MAC addresses are swapped
```
vat# exec trace add dpdk-input 10
vat# exec show trace

(...)
Packet 20

00:16:06:309166: dpdk-input
  Wigig0/1/0/0 rx queue 0
  buffer 0x91c32: current data 2, length 86, free-list 0, clone-count 0, totlen-nifb 0, trace 0x13
                  ext-hdr-valid
                  l4-cksum-computed l4-cksum-correct
  PKT MBUF: port 5, nb_segs 1, pkt_len 86
    buf_len 2176, data_len 86, ol_flags 0x80, data_off 130, phys_addr 0x5c870d00
    packet_type 0x0 l2_len 0 l3_len 0 outer_l2_len 0 outer_l3_len 0
    rss 0x1c fdir.hi 0x0 fdir.lo 0x1c
    ts 0x0
    Packet Offload Flags
      PKT_RX_IP_CKSUM_GOOD (0x0080) IP cksum of RX pkt. is valid
  0xc86f: c6:f0:04:ce:14:fe -> 00:00:04:ce:14:fe
00:16:06:309187: sample
  SAMPLE: sw_if_index 8, next index 0                  <============================================
  new src 04:ce:14:fe:c6:f0 -> new dst 04:ce:14:fe:c8:6f
00:16:06:309195: Wigig0/1/0/0-output
  Wigig0/1/0/0
  IP6: 04:ce:14:fe:c6:f0 -> 04:ce:14:fe:c8:6f
  ICMP6: fe80::6ce:14ff:fefe:c86f -> ff02::1:fffe:c6f0
    tos 0x00, flow label 0x0, hop limit 255, payload length 32
  ICMP neighbor_solicitation checksum 0xd7a
    target address fe80::6ce:14ff:fefe:c6f0
00:16:06:309197: Wigig0/1/0/0-tx
  Wigig0/1/0/0 tx queue 0
  buffer 0x91c32: current data 2, length 86, free-list 0, clone-count 0, totlen-nifb 0, trace 0x13
                  ext-hdr-valid
                  l4-cksum-computed l4-cksum-correct
  PKT MBUF: port 5, nb_segs 1, pkt_len 86
    buf_len 2176, data_len 86, ol_flags 0x80, data_off 130, phys_addr 0x5c870d00
    packet_type 0x0 l2_len 0 l3_len 0 outer_l2_len 0 outer_l3_len 0
    rss 0x1c fdir.hi 0x0 fdir.lo 0x1c
    ts 0x0
    Packet Offload Flags
      PKT_RX_IP_CKSUM_GOOD (0x0080) IP cksum of RX pkt. is valid
  IP6: 04:ce:14:fe:c6:f0 -> 04:ce:14:fe:c8:6f
  ICMP6: fe80::6ce:14ff:fefe:c86f -> ff02::1:fffe:c6f0
    tos 0x00, flow label 0x0, hop limit 255, payload length 32
  ICMP neighbor_solicitation checksum 0xd7a
    target address fe80::6ce:14ff:fefe:c6f0
```
