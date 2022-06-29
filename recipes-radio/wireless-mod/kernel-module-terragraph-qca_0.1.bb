SUMMARY = "Facebook Terragraph Kernel Module for WIL6210"
DESCRIPTION = "Facebook Terragraph Kernel Module for WIL6210"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

require kernel-module-terragraph.inc

TG_MODNAME_SUFFIX = "qca"
export TG_MODNAME_SUFFIX

# Build driver to use WIL6120 slave/master interface
TG_ENABLE_QWILMASTER = "y"
export TG_ENABLE_QWILMASTER

# Enable PFIFOFC qdisc for transmit queueing per QoS for QCA
TG_ENABLE_PFIFOFC?= "y"

# Enable kernel queue stats thread
TG_ENABLE_QUEUE_STATS = "y"
export TG_ENABLE_QUEUE_STATS

# Need headers for qwilmaster interface
DEPENDS_append = " kernel-module-wil6210"
EXTRA_OEMAKE += 'CFLAGS_MODULE="-I${STAGING_INCDIR}/kernel-module-wil6210"'

# Disable DPAA2 offload
TG_ENABLE_DPAA2_forcevariable = "n"
TG_ENABLE_PFE_forcevariable = "n"
DEPENDS_remove = "nsp-headers"
