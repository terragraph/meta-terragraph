SUMMARY = "Facebook e2e common utilities"
DESCRIPTION = "Common utilities used by multiple e2e modules"
SECTION = "base"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=ae79e563b8a09c8fc37978f18dbaa640"

FILESEXTRAPATHS_append := ":${META_TERRAGRAPH_DIR}/src/terragraph-e2e"

SRC_URI = "file://e2e/common"

S = "${WORKDIR}/e2e/common"

inherit cmake tgdev

DEPENDS += "e2e-if fbzmq curl openr"

RDEPENDS_${PN} += "e2e-if openr"
