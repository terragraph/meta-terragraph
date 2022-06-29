DESCRIPTION = "PKTGEN DPDK"
LICENSE = "BSD"
LIC_FILES_CHKSUM = "file://LICENSE;md5=0245ceedaef59ae0129500b0ce1e8a45"

DEPENDS += "libpcap dpdk lua libbsd numactl"

SRC_URI = "\
 git://dpdk.org/git/apps/pktgen-dpdk;protocol=http;nobranch=1 \
 file://0001-Optionally-include-wil6210-pmd-into-pktgen.patch \
 file://0002-Fix-build.patch \
 file://0003-Reduce-pkt-burst-size-from-64-to-32.patch \
 file://0004-Bumping-pktgen.capture-array-len-to-avoid-segfault.patch \
 file://0005-Adding-flags-to-enable-rx-tx-stat-log-support.patch \
 file://0006-Add-support-for-absolute-stats.patch \
 file://0007-Fix-transmit-deadlock-with-wil6210-PMD-driver.patch \
 file://0008-Prevent-max-stats-to-overflow.patch \
 file://0009-Support-setting-ToS-field-in-IPv6-packets.patch \
 file://0010-Pktgen-Terragraph-slowpath-and-WSEC-support.patch \
 file://0011-Add-ping6-to-Pktgen.patch \
 file://0012-Find-wigig-pipe-id-dynfield.patch \
 file://0013-Add-P2MP-to-pktgen.patch \
 file://0014-Add-Queue-Stats-Reporting.patch \
 file://0015-Add-link-weighting.patch \
 file://0016-Add-link-stats-page.patch \
"

# git tag pktgen-21.03.1
SRCREV = "30c772278b5f16cc7f60c83abe95dfe242e35f01"

S = "${WORKDIR}/git"

inherit meson

EXTRA_OEMESON += "-Denable-avx=false -Denable-avx2=false -Denable_lua=true"

# Pull in the wigig pmd
EXTRA_OEMESON += "-Dlink_wil6210_pmd=true"
DEPENDS_append_class-target += " wigig-dpdk"

export RTE_SDK="${STAGING_DIR_TARGET}/usr"

do_install_append() {
	install -d ${D}/usr/local/share/lua/5.3/
	cp -f ${S}/Pktgen.lua ${D}/usr/local/share/lua/5.3/
}

PACKAGE_ARCH = "${MACHINE_ARCH}"
PARALLEL_MAKE = ""

FILES_${PN} += "/usr/local/share/lua/5.3/"

PATCHTOOL = "git"
