SUMMARY = "Facebook E2E Driver Interface"
DESCRIPTION = "Facebook E2E Driver Interface"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/driver-if"

S = "${WORKDIR}/e2e/driver-if"

inherit cmake tgdev

DEPENDS += " \
           e2e-common \
           openr \
           fbzmq \
           gtest \
           libnl \
           nl-driver-if-headers \
           wireless-fw-interface \
           curl \
           gpsd \
           "

RDEPENDS_${PN} += "e2e-common wireless-fw-interface"

FILES_${PN}-dbg += "/usr/sbin/tests/e2e/.debug"

# Separate out driver_if_daemon as it overlaps with e2e_minion.
PACKAGES =+ "${PN}-daemon"
RDEPENDS_${PN}-daemon += "${PN}"
FILES_${PN}-daemon = "/usr/sbin/driver_if_daemon"
