DESCRIPTION = "A collection of C++ HTTP libraries including an easy to use HTTP server."
HOMEPAGE = "https://github.com/facebook/proxygen"
SECTION = "core"
LICENSE = "BSD-3-Clause"
LIC_FILES_CHKSUM = "file://LICENSE;md5=53ac1776fc7ec1ed44f5674b9f72a223"

SRCREV = "ad6d3822c6506e58849e531a16ca64e892caf6be"
SRC_URI = "git://github.com/facebook/proxygen.git;protocol=https;branch=main"

S = "${WORKDIR}/git"

inherit cmake python3native

DEPENDS += "gflags glog gperf-native folly fizz openssl wangle zlib zstd"

FILES_${PN} += "${libdir}"

EXTRA_OECMAKE_append = " -DBUILD_SAMPLES=OFF"
EXTRA_OECMAKE_append = " -DCMAKE_CXX_STANDARD=17"
