inherit asan module kernel-extra-headers-export

SRC_URI = "file://fb_terragraph"
S = "${WORKDIR}/fb_terragraph"

TG_MODNAME_SUFFIX ??= "dhd"
export TG_MODNAME_SUFFIX

# Build driver to use common BH interface
TG_ENABLE_BH_COMMON ??= "n"
export TG_ENABLE_BH_COMMON

# Build driver to use WIL6120 slave/master interface
TG_ENABLE_QWILMASTER ??= "n"
export TG_ENABLE_QWILMASTER

# Disable PFIFOFC qdisc for transmit queueing per QoS by default
TG_ENABLE_PFIFOFC ??= "n"
export TG_ENABLE_PFIFOFC

# Build interface to NSS only on Armada39x
TG_ENABLE_NSS = "n"
TG_ENABLE_NSS_armada39x = "y"
export TG_ENABLE_NSS

# Build interface to PFE only on LS10XXa
TG_ENABLE_PFE = "n"
TG_ENABLE_PFE_ls1012a = "y"
export TG_ENABLE_PFE

# Build interface for DPAA API
TG_ENABLE_DPAA2 = "n"
TG_ENABLE_DPAA2_ls1088a = "y"
export TG_ENABLE_DPAA2
DEPENDS_append_ls1088a = " nsp-headers"

TG_ENABLE_GPS= "n"
TG_ENABLE_GPS_armada39x = "y"
TG_ENABLE_GPS_qoriq = "y"
export TG_ENABLE_GPS

TG_RX_EVENT_CPU_armada39x = "1"
export TG_RX_EVENT_CPU

TG_ENABLE_QUEUE_STATS ?= "n"
TG_ENABLE_QUEUE_STATS_armada39x = "y"
export TG_ENABLE_QUEUE_STATS

DEPENDS = "nl-driver-if-headers"
DEPENDS_append_armada39x = " kernel-module-nss"
