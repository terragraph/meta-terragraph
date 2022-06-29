SUMMARY = "Facebook e2e client utilities"
DESCRIPTION = "Client utilities for e2e minion and stats interface"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/clients"

S = "${WORKDIR}/e2e/clients"

inherit cmake tgdev

DEPENDS += "e2e-if e2e-common fbzmq stats-common"

RDEPENDS_${PN} += "e2e-if e2e-common fbzmq stats-common"
