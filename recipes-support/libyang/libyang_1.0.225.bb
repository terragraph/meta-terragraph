SUMMARY = "libyang"
DESCRIPTION = "libyang is a YANG data modelling language parser and toolkit"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=2982272c97a8e417a844857ca0d303b1"

SRCREV = "8e9f87949974c7b2c6dc5e830c0e78e4160fbc50"
SRC_URI = "git://github.com/CESNET/libyang.git;protocol=https;nobranch=1"

S = "${WORKDIR}/git"

inherit cmake

DEPENDS += "pcre"

EXTRA_OECMAKE_append = " -DENABLE_LYD_PRIV=ON"

FILES_${PN} += "${libdir}"
