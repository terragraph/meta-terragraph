SUMMARY = "Mobile app server test client"
DESCRIPTION = "Mobile app server test client"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${WORKDIR}/LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

SRC_URI = "file://src \
           file://src/wifitest \
           file://sv \
           file://LICENSE \
           "

S = "${WORKDIR}/src/wifitest"

TARGET_CC_ARCH += "${LDFLAGS}"

inherit cmake

DEPENDS = "folly \
           gflags \
           protobuf \
           mobile-common \
           "
