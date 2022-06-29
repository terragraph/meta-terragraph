SUMMARY = "Network benchmark tool"
DESCRIPTION = "Measure network performance"
HOMEPAGE = "http://software.es.net/iperf/"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=4991c26fb646b69589d1eff3e958b06a"

SRC_URI = "git://github.com/esnet/iperf.git \
           file://0002-Remove-pg-from-profile_CFLAGS.patch \
           file://0003-configure-fix.patch \
          "

# 3.10
SRCREV = "c159c741243441be84f91d3130cbeef1e1d579e8"

S = "${WORKDIR}/git"

inherit autotools

DEPENDS = "openssl"
EXTRA_OECONF = "--with-openssl=${RECIPE_SYSROOT}${prefix}"
