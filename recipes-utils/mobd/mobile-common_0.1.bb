SUMMARY = "Mobile App Common"
DESCRIPTION = "Mobile App Common"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src/common \
           file://LICENSE \
           "

S = "${WORKDIR}/src/common"

inherit cmake

FILES_${PN} += "${libdir}/*.so*"
FILES_SOLIBDEV = ""

DEPENDS = "folly \
           gflags \
           protobuf \
           protobuf-native \
           "
