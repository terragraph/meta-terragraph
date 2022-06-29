DESCRIPTION = "Vector Packet Processing"
HOMEPAGE = "http://fd.io"
LICENSE = "Apache-2.0"
LIC_FILES_CHKSUM = "file://${S}/../LICENSE;md5=175792518e4ac015ab6696d16c4f607e"

DEPENDS = "openssl python3-ply-native"
DEPENDS_append_class-target = " virtual/kernel dpdk util-linux numactl kernel-module-direct-vpp"

SRC_URI = "git://source.codeaurora.org/external/qoriq/qoriq-components/vpp;protocol=https;nobranch=1 \
           file://0001-optionally-include-wil6210-pmd-in-dpdk.patch \
           file://0002-cmake-find-dpdk-better.patch \
           file://0003-dpdk-teach-plugin-about-wigig-interfaces.patch \
           file://0004-dpdk-plugin-Fix-duplicate-interface-name.patch \
           file://0005-Throttle-log-only-VPP-internal-log-buffer.patch \
           file://0006-Split-show-trace-to-CLI-and-functional-parts.patch \
           file://0007-Revert-deprecate-tapcli.patch \
           file://0008-Allow-tuntap-API-to-specify-MAC-address-for-kernel-s.patch \
           file://0009-Accept-optional-name-for-vpp-side-of-tapcli.patch \
           file://0010-Do-not-call-clib_warning-from-signal_handler.patch \
           file://0011-Run-interface-add-callbacks-after-device-is-setup.patch \
           file://0012-Add-perf-analysis-command-and-perf-counters.patch \
           file://0013-Set-the-router-flag-in-neighbor-advertisements.patch \
           file://0014-dpdk-check-if-device-start-works-before-setting-devi.patch \
           file://0015-Implement-simple-src-address-selection-for-ICMPv6.patch \
           file://0016-interface-output-node-should-trace-packets.patch \
           file://0017-Exit-instead-of-crash-when-heap-alloc-fails.patch \
           file://0018-Add-command-to-configure-device-burst-parameters.patch \
           file://0019-Set-device-flag-down-before-stop.patch \
           file://0020-Tune-DPDK-burst-size-params-for-best-performance.patch \
           file://0021-Properly-format-dpdk-devargs.patch \
           file://0022-Pass-PIR-to-the-policer-config-instead-of-EIR.patch \
           file://0023-CLI-for-set-link-local-address.patch \
           file://0024-Add-VAPI-kernel-tap-MAC-address.patch \
           file://0025-VxLAN-QoS-Marking-for-IPv6.patch \
           file://0026-VxLAN-QoS-outer-IPv6-marking-for-IPv4-packets.patch \
           file://0027-NAT64-Only-translate-IPv6-packets-matching-the-NAT-p.patch \
           file://0028-Adjust-packet-offsets-for-tracing-in-dpdk-input.patch \
           file://0029-SRv6-policy-support-for-VLANs-and-sub-interfaces.patch \
           file://0030-Add-ethernet-interface-flag-and-checks-to-drop-non-E.patch \
           file://0031-Handle-VLAN-header-in-inner-packet-QoS-translation.patch \
           file://0032-Add-promiscuous-mode-counter.patch \
           file://0033-SRv6-QoS-Preserve-inner-IP-DSCP-value-for-outer-IP-h.patch \
           file://0034-cli-remove-unix_cli_new_session_process.patch \
           file://0035-Redirect-802.1x-authentication-frame.patch \
           file://0036-dpdk-add-dynfield-for-wil6210.patch \
           file://0037-Make-ip6-headers-and-function-available-for-plugins.patch \
           file://0038-Plugins-fix-add_vpp_plugin-cmake-macro.patch \
           file://0039-Change-plugin-logger-calls-back-to-clib_warning.patch \
           file://0040-vlib-fix-buffer-pool-alignment-size.patch \
           file://0041-buffers-revert-to-freelist-with-dpdk-buffer-callback.patch \
           file://0042-dpdk-don-t-set-hardware-intf-flag-for-L3-DMAC-filter.patch \
           file://0043-dhcpv6-remote-id-and-subscriber-id-options-for-proxy.patch \
           file://0044-dhcpv6-Parse-DHCPv6-addresses-and-update-routes.patch \
           file://0045-policer-classify-unrecognized-packets-as-default-TC3.patch \
           file://0046-fix-vpp-for-2R3C-policers.patch \
           file://0047-properly-respond-to-solicited-forus-ICMPv6-NS-messag.patch \
           "

# TGHQoS and TG features that rely on hqos implementation
SRC_URI += "\
           file://1000-tghqos-RED-dropper.patch \
           file://1001-tghqos-hierarchical-scheduler-module.patch \
           file://1002-tghqos-hqos-port-management-and-API.patch \
           file://1003-tghqos-cli-for-stats-and-configurations.patch \
           file://1004-tghqos-add-cli-command-dump-pipe-tc-stats.patch \
           file://1005-tghqos-set-4-traffic-classes-and-1-queue-per-traffic.patch \
           file://1006-tghqos-add-weighted-round-robin-scheduling-option.patch \
           file://1007-tghqos-add-configuration-and-cli-commands-for-wrr.patch \
           file://1008-tghqos-integrate-TGHQoS-in-dpdk-plugin.patch \
           file://1009-dpdk-restore-API-to-use-with-tghqos.patch \
           file://1010-Use-dpdk-input-polling-to-send-Terragraph-queue-stat.patch \
           file://1011-Support-jumbo-frame-transmission-for-wil6210-pmd.patch \
           "

# TODO: originally based on 19.01.1, needs to be reworked for VPP 21.01
# Direct VPP patches
#SRC_URI += "\
#           file://2000-Direct-VPP.patch \
#           file://2001-Add-DVPP-local-packet-inject-node.patch \
#           file://2002-support-for-multi-segments-packets.patch \
#           file://2003-Add-API-to-configure-port-to-thread-mapping.patch \
#           "

# v21.01
SRCREV = "2dc49092878205ab062a00d4d46240aa0759ba4e"

PATCHTOOL = "git"

# Some shared objs are built with duplicate rpaths.
# Since this is a benign problem, just silence this error and move on.
INSANE_SKIP_${PN} += "useless-rpaths"
INSANE_SKIP_${PN}-all-plugins += "useless-rpaths"
INSANE_SKIP_${PN}-vat-plugins += "useless-rpaths"

inherit cmake pkgconfig python3native

S = "${WORKDIR}/git/src"

VPP_PLATFORM ?= "vpp"
VPP_PLATFORM_ls1088a = "dpaa"

export PLATFORM = "${VPP_PLATFORM}"
export DPDK_PATH = "${STAGING_DIR_TARGET}"
export OPENSSL_PATH = "${STAGING_DIR_TARGET}"

# Configure VPP without any buildroot intervention
CFLAGS_append = " -DCLIB_LOG2_CACHE_LINE_BYTES=6"

# GCC10 now defaults to -fno-common and older packages fail to build.
CFLAGS_append = " -fcommon"

# Ensure we don't use CCACHE if available
EXTRA_OECMAKE_append = " -DVPP_USE_CCACHE=OFF"

do_install_append() {
    # We don't need VPP's startup.conf or sysctl example files
    rm -rf ${D}/usr/etc
}

# Package vpp plugins we currently use into the base package, and make a
# separate package for all plugins.
# TODO: this was just the list of non-disabled plugins from vpp 19.01.1 and
# other dependent plugins, verify what else we can eliminate to save space
VPP_PLUGINS_USED = "acl_plugin.so det44_plugin.so dhcp_plugin.so dns_plugin.so \
                    dpdk_plugin.so flowprobe_plugin.so igmp_plugin.so \
                    ila_plugin.so ioam_plugin.so l2e_plugin.so lacp_plugin.so \
                    lb_plugin.so mactime_plugin.so map_plugin.so \
                    memif_plugin.so nat_plugin.so nsh_plugin.so nsim_plugin.so \
                    ping_plugin.so pppoe_plugin.so srv6ad_plugin.so \
                    srv6am_plugin.so srv6as_plugin.so tlsopenssl_plugin.so"

python () {
    used = d.getVar("VPP_PLUGINS_USED").split()
    pn = d.getVar("PN")
    libdir = d.getVar("libdir")
    for plugin in used:
        d.setVar(f"FILES_{pn}", d.getVar(f"FILES_{pn}") + f" {libdir}/vpp_plugins/{plugin}")
}

PACKAGES += "${PN}-all-plugins"
FILES_${PN}-all-plugins = "${libdir}/vpp_plugins"

# VPP API test plugins
PACKAGES += "${PN}-vat-plugins"
FILES_${PN}-vat-plugins = "${libdir}/vpp_api_test_plugins"

# Enable extra safety checks in debug builds
DEBUG_OPTIMIZATION += "-DCLIB_DEBUG"

# Part of development package?
FILES_${PN}-dev += " \
    ${datadir}/vpp \
    ${bindir}/vapi_c_gen.py \
    ${bindir}/vapi_cpp_gen.py \
    ${bindir}/vapi_json_parser.py \
    ${bindir}/vppapigen \
    "

# The DVPP kernel module is build only for target build
# TARGET_CFLAGS_append = " -DHAS_DVPP_MODULE_HEADER"

BBCLASSEXTEND = "native"
