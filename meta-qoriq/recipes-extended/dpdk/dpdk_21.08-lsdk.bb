DESCRIPTION = "Data Plane Development Kit"
HOMEPAGE = "http://dpdk.org"
LICENSE = "BSD & LGPLv2 & GPLv2"
LIC_FILES_CHKSUM = "\
	file://license/gpl-2.0.txt;md5=b234ee4d69f5fce4486a80fdaf4a4263 \
	file://license/lgpl-2.1.txt;md5=4b54a1fd55a448865a0b32d41598759d \
	file://license/bsd-3-clause.txt;md5=0f00d99239d922ffd13cabef83b33444 \
"

DEPENDS_class-target += "virtual/kernel openssl"

inherit meson module-base kernel-module-split

SRC_URI = "git://source.codeaurora.org/external/qoriq/qoriq-components/dpdk;protocol=https;nobranch=1 \
           file://0001-meson-build-fixes.patch \
           file://0002-fix-vfio.patch \
           file://0003-Fix-interrupt-type-selection-for-MSI.patch \
           file://0004-dpdk-dpaa2-implement-tx-timestamping.patch \
           file://0005-Add-driver-xstats-and-perf-monitoring-API.patch \
           file://0006-Add-RTE-Transmit-Feedback-API.patch \
           file://0007-Add-Ethernet-TX-Send-Pending-Stats-API.patch \
           file://0008-Add-API-to-get-per-pipe-TX-queue-length.patch \
           file://0009-dpaa2-enable-rx-IEEE1588PTP-HW-Detection-flag.patch \
           file://0010-dpaa2-use-mac_filter_entries-1-when-mac-filter-is-di.patch \
           file://0011-rte_eal-don-t-check-existing-affinity-when-setting-c.patch \
"

# v20.11.2
SRCREV = "f74b8bd5ab1c6ff76e956fc202a56aea2d200270"

S = "${WORKDIR}/git"

# GCC10 now defaults to -fno-common and older packages fail to build.
TARGET_CFLAGS += "-fcommon"

DPAA_VER ?= "dpaa"

# dpdk-native provides dpdk-pmdinfogen which is needed for the target build
BBCLASSEXTEND = "native"
DEPENDS_class-target += "${PN}-native"
EXTRA_OEMESON_class-native += "-Dbuildtools_only=true -Denable_kmods=false"
PACKAGES += "${PN}-pmdinfogen"

# Optionally build and package example applications
DPDK_EXAMPLES ??= "l2fwd l3fwd l2fwd-crypto ipsec-secgw kni ip_fragmentation ip_reassembly"
PACKAGES += "${@'${PN}-examples' if d.getVar('DPDK_EXAMPLES').strip() else ''}"

# Utils such as dpdk-devbind broken out into a separate package
PACKAGES += "${PN}-utils"
RDEPENDS_${PN}-utils = "python3-core"

# All of the drivers are installed by default. The only hardware-specific
# drivers to keep are dpaa2 and dpaax drivers. Disable all other
# hardware-specific drivers, including dpaa drivers. Also disable all drivers
# associated with specific virtualization platforms. Keep drivers that are only
# associated with software constructs, e.g. AF_PACKET, Memif, etc.

DPDK_DISABLED_DRIVERS := "baseband/null baseband/turbo_sw baseband/fpga_lte_fec \
baseband/fpga_5gnr_fec baseband/acc100"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} bus/dpaa bus/ifpga bus/vmbus"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} common/cpt common/iavf \
common/mlx5 common/mvep common/octeontx common/octeontx2 common/sfc_efx"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} compress/isal \
compress/octeontx compress/zlib compress/qat"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} crypto/aesni_gcm \
crypto/aesni_mb crypto/armv8 crypto/bcmfs crypto/caam_jr crypto/ccp \
crypto/dpaa_sec crypto/kasumi crypto/mvsam crypto/nitrox crypto/null \
crypto/octeontx crypto/octeontx2 crypto/openssl crypto/scheduler crypto/snow3g \
crypto/virtio crypto/zuc "

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} event/dlb event/dlb2 \
event/dpaa event/octeontx2 event/opdl event/skeleton"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} mempool/dpaa \
mempool/octeontx mempool/octeontx2"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} net/ark net/atlantic net/avp \
net/axgbe net/bnx2x net/bnxt net/cxgbe net/e1000 net/ena net/enetc net/enetfec \
net/enic net/dpaa net/fm10k net/i40e net/hinic net/hns3 net/iavf net/ice \
net/igc net/ipn3ke net/ixgbe net/liquidio net/mlx4 net/mlx5 net/mvneta \
net/mvpp2 net/netvsc net/nfb net/nfp net/octeontx net/octeontx2 net/pfe \
net/qede net/sfc net/szedata2 net/thunderx net/txgbe net/vdev_netvsc \
net/virtio net/vmxnet3"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} raw/dpaa_qdma raw/ifpga \
raw/ioat raw/ntb raw/octeontx2_dma raw/octeontx2_ep raw/skeleton"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} regex/mlx5 regex/octeontx2"

DPDK_DISABLED_DRIVERS := "${DPDK_DISABLED_DRIVERS} vdpa/mlx5 vdpa/octeontx2"

EXTRA_OEMESON_class-target += "\
	${@'-Dexamples='+d.getVar('DPDK_EXAMPLES').replace(' ',',')} \
	-Dkernel_dir=${STAGING_KERNEL_DIR} \
	-Dkernel_install_dir=/lib/modules/${KERNEL_VERSION}/dpdk \
	-Denable_kmods=true \
	-Ddisable_drivers=${@','.join(d.getVar('DPDK_DISABLED_DRIVERS').split())} \
"

do_write_config_append() {
	sed -i "/\[properties\]/aimplementor_id='${DPAA_VER}'" "${WORKDIR}/meson.cross"
}

do_install_append() {
	install -d ${D}${datadir}/usertools
	install -m 755 ${S}/usertools/cpu_layout.py ${D}${datadir}/usertools
	install -m 755 ${S}/usertools/dpdk-devbind.py ${D}${datadir}/usertools
	install -m 755 ${S}/usertools/dpdk-pmdinfo.py ${D}${datadir}/usertools
	install -m 755 ${S}/usertools/dpdk-telemetry-client.py ${D}${datadir}/usertools

	install -d ${D}${sbindir}
	install -m 755 ${S}/usertools/dpdk-devbind.py ${D}${sbindir}/dpdk-devbind
	# don't need these examples source files
	rm -rf ${D}${datadir}/dpdk
}

FILES_${PN}-pmdinfogen = " \
	${bindir}/dpdk-pmdinfogen \
"

FILES_${PN}-examples = " \
	${@' '.join(['${bindir}/dpdk-' + e for e in d.getVar('DPDK_EXAMPLES').split()])} \
"

FILES_${PN}-utils = " \
	${bindir} \
        ${sbindir} \
	${datadir}/usertools \
"

FILES_${PN} = " \
	${libdir}/lib*${SOLIBS} \
	${libdir}/dpdk/pmds-21.0/lib*${SOLIBS} \
"

FILES_${PN}-dev += " \
	${libdir}/dpdk/pmds-21.0/lib*${SOLIBSDEV} \
"

PATCHTOOL = "git"
